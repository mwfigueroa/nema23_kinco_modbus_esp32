/**
 * bridge_rs485.cpp — Puente Modbus TCP ↔ RS485
 *
 * Arquitectura:
 *   - Servidor TCP escucha en puerto 502 (Modbus TCP)
 *   - Cada conexión entrante se maneja en un socket dedicado
 *   - Las tramas Modbus TCP se convierten a RTU y se envían por RS485
 *   - Las respuestas RS485 se encapsulan en TCP y se devuelven
 *   - Comandos dirigidos al slave ID local se procesan internamente
 */

#include "bridge_rs485.h"
#include "pin_config.h"
#include "relay_control.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "bridge_rs485";

static bridge_config_t s_bridge_config = {};
static QueueHandle_t s_rx_queue = nullptr;
static TaskHandle_t s_tcp_task = nullptr;
static bool s_running = false;

/* ================================================================
 * Inicialización RS485 (UART)
 * ================================================================ */

/**
 * Habilita el transceiver MAX13487: ambos pines de control en HIGH.
 *   RS485_EN_GPIO (/RE) y RS485_SE_GPIO (SHDN) en HIGH = transceiver activo.
 * El MAX13487 conmuta TX/RX automáticamente (auto-direction), por eso no se
 * usa el modo RS485 half-duplex de la UART (que requeriría un pin DE/RTS).
 */
static void rs485_enable_transceiver(void)
{
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << RS485_EN_GPIO) | (1ULL << RS485_SE_GPIO);
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)RS485_EN_GPIO, 1);
    gpio_set_level((gpio_num_t)RS485_SE_GPIO, 1);
    ESP_LOGI(TAG, "Transceiver RS485 habilitado (EN=%d, SE=%d en HIGH)",
             RS485_EN_GPIO, RS485_SE_GPIO);
}

/**
 * CRC16 Modbus RTU (polinomio 0xA001, init 0xFFFF, byte de orden bajo primero).
 */
static uint16_t modbus_crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static esp_err_t rs485_uart_init(void)
{
    /* Habilitar el transceiver ANTES de transmitir nada. */
    rs485_enable_transceiver();

    uart_config_t uart_cfg = {
        .baud_rate = (int)s_bridge_config.rs485_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t ret = uart_driver_install((uart_port_t)s_bridge_config.rs485_uart_num,
                                         RS485_UART_BUF_SIZE * 2,
                                         RS485_UART_BUF_SIZE * 2,
                                         0, nullptr, 0);
    if (ret != ESP_OK) return ret;

    ret = uart_param_config((uart_port_t)s_bridge_config.rs485_uart_num, &uart_cfg);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin((uart_port_t)s_bridge_config.rs485_uart_num,
                        RS485_TX_GPIO, RS485_RX_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;

    /* Modo UART normal: el MAX13487 maneja la dirección automáticamente
     * (auto-direction) sensando la línea TX, por lo que NO se usa el modo
     * RS485 half-duplex de la UART (que controlaría un pin DE/RTS inexistente
     * en este cableado). */
    ret = uart_set_mode((uart_port_t)s_bridge_config.rs485_uart_num, UART_MODE_UART);
    return ret;
}

/* ================================================================
 * Conversión Modbus TCP → RTU
 * ================================================================ */

/**
 * Extrae la trama Modbus RTU de una trama Modbus TCP (mbap header).
 * @param tcp_frame  Trama Modbus TCP completa
 * @param tcp_len    Longitud de la trama TCP
 * @param rtu_out    Buffer de salida para la trama RTU
 * @param rtu_len    Longitud de la trama RTU resultante
 * @return ESP_OK si la conversión fue exitosa
 */
static esp_err_t modbus_tcp_to_rtu(const uint8_t *tcp_frame, size_t tcp_len,
                                    uint8_t *rtu_out, size_t *rtu_len)
{
    /* MBAP header: TransactionID(2) + ProtocolID(2) + Length(2) + UnitID(1) = 7 bytes */
    if (tcp_len < 8) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* La trama RTU es: UnitID(1) + PDU + CRC16(2). El MBAP TCP no lleva CRC,
     * así que copiamos UnitID+PDU y le anexamos el CRC16 que el slave exige.
     * Reservamos 2 bytes para el CRC -> el payload no puede pasar de 254. */
    size_t pdu_len = tcp_len - 6;  /* removemos TransactionID + ProtocolID + Length */
    if (pdu_len > 254) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(rtu_out, tcp_frame + 6, pdu_len);

    /* Anexar CRC16 Modbus RTU (byte bajo primero). */
    uint16_t crc = modbus_crc16(rtu_out, pdu_len);
    rtu_out[pdu_len]     = (uint8_t)(crc & 0xFF);
    rtu_out[pdu_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    *rtu_len = pdu_len + 2;
    return ESP_OK;
}

/**
 * Envuelve una trama Modbus RTU en TCP (MBAP header).
 * La trama RTU entrante incluye CRC16 al final; se valida y se descarta,
 * porque Modbus TCP no transporta CRC.
 */
static esp_err_t modbus_rtu_to_tcp(const uint8_t *rtu_frame, size_t rtu_len,
                                    uint8_t *tcp_out, size_t *tcp_len,
                                    uint16_t transaction_id)
{
    /* Mínimo: UnitID(1) + func(1) + CRC(2) = 4 bytes. */
    if (rtu_len < 4) return ESP_ERR_INVALID_SIZE;

    /* Validar el CRC16 de la respuesta RTU. */
    size_t pdu_len = rtu_len - 2;  /* UnitID + PDU, sin CRC */
    uint16_t rx_crc = (uint16_t)rtu_frame[rtu_len - 2] |
                      ((uint16_t)rtu_frame[rtu_len - 1] << 8);
    if (rx_crc != modbus_crc16(rtu_frame, pdu_len)) {
        ESP_LOGW(TAG, "CRC RTU inválido, descartando respuesta");
        return ESP_ERR_INVALID_CRC;
    }

    /* MBAP: TransactionID(2) + ProtocolID(2) + Length(2) + UnitID(1) + PDU */
    size_t total = pdu_len + 6;
    if (total > 260) return ESP_ERR_INVALID_SIZE;

    tcp_out[0] = (transaction_id >> 8) & 0xFF;
    tcp_out[1] = transaction_id & 0xFF;
    tcp_out[2] = 0x00;  /* Protocol ID = 0 (Modbus) */
    tcp_out[3] = 0x00;
    uint16_t len_field = pdu_len;  /* UnitID + PDU length */
    tcp_out[4] = (len_field >> 8) & 0xFF;
    tcp_out[5] = len_field & 0xFF;
    memcpy(tcp_out + 6, rtu_frame, pdu_len);

    *tcp_len = total;
    return ESP_OK;
}

/* ================================================================
 * Servidor Modbus local — control de relés vía coils
 * ================================================================ */

/* Códigos de excepción Modbus. */
#define MB_EXC_ILLEGAL_FUNCTION  0x01
#define MB_EXC_ILLEGAL_ADDRESS   0x02
#define MB_EXC_ILLEGAL_VALUE     0x03

/** Anexa el CRC16 RTU a una trama y devuelve la longitud total (con CRC). */
static size_t modbus_finalize(uint8_t *frame, size_t pdu_len)
{
    uint16_t crc = modbus_crc16(frame, pdu_len);
    frame[pdu_len]     = (uint8_t)(crc & 0xFF);
    frame[pdu_len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    return pdu_len + 2;
}

/** Construye una respuesta de excepción Modbus (FC | 0x80). */
static size_t modbus_exception(uint8_t *resp, uint8_t slave, uint8_t fc, uint8_t code)
{
    resp[0] = slave;
    resp[1] = fc | 0x80;
    resp[2] = code;
    return modbus_finalize(resp, 3);
}

/**
 * Procesa una trama Modbus RTU dirigida al gateway local (control de relés).
 * Soporta:
 *   - FC 0x01 Read Coils         → lee el estado de los relés
 *   - FC 0x05 Write Single Coil  → enciende/apaga un relé (0xFF00=ON, 0x0000=OFF)
 *   - FC 0x0F Write Multiple Coils → escribe varios relés de una
 * Los coils 0..RELAY_COUNT-1 mapean a los relés. Genera la respuesta RTU
 * completa (con CRC) en `resp`/`resp_len`.
 */
static esp_err_t handle_local_modbus(const uint8_t *req, size_t req_len,
                                     uint8_t *resp, size_t *resp_len)
{
    if (req_len < 4) return ESP_ERR_INVALID_SIZE;

    uint8_t slave = req[0];
    uint8_t fc    = req[1];

    switch (fc) {
    case 0x01: {  /* Read Coils */
        if (req_len < 8) { *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_VALUE); break; }
        uint16_t addr = ((uint16_t)req[2] << 8) | req[3];
        uint16_t qty  = ((uint16_t)req[4] << 8) | req[5];
        if (qty == 0 || (uint32_t)addr + qty > RELAY_COUNT) {
            *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_ADDRESS);
            break;
        }
        uint8_t byte_count = (uint8_t)((qty + 7) / 8);
        resp[0] = slave;
        resp[1] = fc;
        resp[2] = byte_count;
        memset(resp + 3, 0, byte_count);
        for (uint16_t i = 0; i < qty; i++) {
            if (relay_control_get((uint8_t)(addr + i))) {
                resp[3 + i / 8] |= (uint8_t)(1 << (i % 8));
            }
        }
        *resp_len = modbus_finalize(resp, 3 + byte_count);
        break;
    }

    case 0x05: {  /* Write Single Coil */
        if (req_len < 8) { *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_VALUE); break; }
        uint16_t addr = ((uint16_t)req[2] << 8) | req[3];
        uint16_t val  = ((uint16_t)req[4] << 8) | req[5];
        if (addr >= RELAY_COUNT) {
            *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_ADDRESS);
            break;
        }
        if (val != 0x0000 && val != 0xFF00) {
            *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_VALUE);
            break;
        }
        relay_control_set((uint8_t)addr, val == 0xFF00);
        /* La respuesta a 0x05 es el eco de la petición (sin CRC original). */
        memcpy(resp, req, 6);
        *resp_len = modbus_finalize(resp, 6);
        break;
    }

    case 0x0F: {  /* Write Multiple Coils */
        if (req_len < 9) { *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_VALUE); break; }
        uint16_t addr       = ((uint16_t)req[2] << 8) | req[3];
        uint16_t qty        = ((uint16_t)req[4] << 8) | req[5];
        uint8_t  byte_count = req[6];
        if (qty == 0 || (uint32_t)addr + qty > RELAY_COUNT) {
            *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_ADDRESS);
            break;
        }
        if (byte_count != (qty + 7) / 8 || req_len < (size_t)(7 + byte_count + 2)) {
            *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_VALUE);
            break;
        }
        for (uint16_t i = 0; i < qty; i++) {
            bool on = (req[7 + i / 8] >> (i % 8)) & 0x01;
            relay_control_set((uint8_t)(addr + i), on);
        }
        /* Respuesta: eco de slave+fc+dirección+cantidad. */
        memcpy(resp, req, 6);
        *resp_len = modbus_finalize(resp, 6);
        break;
    }

    default:
        *resp_len = modbus_exception(resp, slave, fc, MB_EXC_ILLEGAL_FUNCTION);
        break;
    }

    return ESP_OK;
}

static int recv_exact(int sock, uint8_t *buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        int ret = recv(sock, buf + received, len - received, 0);
        if (ret <= 0) {
            return ret;
        }
        received += ret;
    }
    return (int)received;
}

/* ================================================================
 * Tarea del servidor TCP
 * ================================================================ */

static void tcp_server_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "No se pudo crear socket TCP");
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(s_bridge_config.modbus_tcp_port);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind fallido en puerto %u", s_bridge_config.modbus_tcp_port);
        close(listen_sock);
        vTaskDelete(nullptr);
        return;
    }

    if (listen(listen_sock, 4) < 0) {
        ESP_LOGE(TAG, "Listen fallido");
        close(listen_sock);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Servidor Modbus TCP escuchando en puerto %u", s_bridge_config.modbus_tcp_port);

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock < 0) {
            continue;
        }

        ESP_LOGI(TAG, "Cliente conectado: %s", inet_ntoa(client_addr.sin_addr));

        /* Manejar cliente en un loop */
        uint8_t tcp_buf[260];
        uint8_t rtu_buf[256];
        while (s_running) {
            if (s_bridge_config.mode == BRIDGE_MODE_RAW_TCP_RS485) {
                int recv_len = recv(client_sock, tcp_buf, sizeof(tcp_buf), 0);
                if (recv_len <= 0) break;

                uart_write_bytes((uart_port_t)s_bridge_config.rs485_uart_num,
                                 tcp_buf, recv_len);

                uint8_t resp[256];
                int resp_len = uart_read_bytes((uart_port_t)s_bridge_config.rs485_uart_num,
                                               resp, sizeof(resp),
                                               pdMS_TO_TICKS(200));
                if (resp_len > 0) {
                    send(client_sock, resp, resp_len, 0);
                }
                continue;
            }

            /* Leer una trama Modbus TCP completa: MBAP(6) + UnitID/PDU. */
            int recv_len = recv_exact(client_sock, tcp_buf, 6);
            if (recv_len <= 0) break;

            uint16_t mbap_len = ((uint16_t)tcp_buf[4] << 8) | tcp_buf[5];
            if (mbap_len == 0 || mbap_len > sizeof(tcp_buf) - 6) {
                ESP_LOGW(TAG, "Length MBAP invalido: %u", mbap_len);
                break;
            }

            recv_len = recv_exact(client_sock, tcp_buf + 6, mbap_len);
            if (recv_len <= 0) break;

            size_t tcp_frame_len = 6 + mbap_len;

            /* Verificar si es para el slave local o para RS485 */
            uint8_t slave_id = tcp_buf[6];  /* UnitID está en offset 6 */

            if (s_bridge_config.enable_filter && slave_id == s_bridge_config.slave_id) {
                /* Comando dirigido al gateway local — procesar coils (relés) y
                 * responder de forma síncrona para que el master no dé timeout. */
                size_t rtu_len;
                if (modbus_tcp_to_rtu(tcp_buf, tcp_frame_len, rtu_buf, &rtu_len) == ESP_OK) {
                    uint8_t rtu_resp[260];
                    size_t  rtu_resp_len = 0;
                    if (handle_local_modbus(rtu_buf, rtu_len, rtu_resp, &rtu_resp_len) == ESP_OK
                        && rtu_resp_len > 0) {
                        uint8_t tcp_resp[260];
                        size_t  tcp_resp_len;
                        uint16_t tid = (tcp_buf[0] << 8) | tcp_buf[1];
                        if (modbus_rtu_to_tcp(rtu_resp, rtu_resp_len, tcp_resp,
                                              &tcp_resp_len, tid) == ESP_OK) {
                            send(client_sock, tcp_resp, tcp_resp_len, 0);
                        }
                    }
                }
            } else if (s_bridge_config.mode == BRIDGE_MODE_MODBUS_TCP_RS485) {
                /* Forward al bus RS485 */
                size_t rtu_len;
                if (modbus_tcp_to_rtu(tcp_buf, tcp_frame_len, rtu_buf, &rtu_len) == ESP_OK) {
                    uart_write_bytes((uart_port_t)s_bridge_config.rs485_uart_num,
                                     rtu_buf, rtu_len);

                    /* Esperar respuesta del bus RS485 */
                    uint8_t resp[256];
                    int resp_len = uart_read_bytes((uart_port_t)s_bridge_config.rs485_uart_num,
                                                   resp, sizeof(resp),
                                                   pdMS_TO_TICKS(200));
                    if (resp_len > 0) {
                        /* Envolver en TCP y devolver */
                        uint8_t tcp_resp[260];
                        size_t tcp_resp_len;
                        uint16_t tid = (tcp_buf[0] << 8) | tcp_buf[1];
                        if (modbus_rtu_to_tcp(resp, resp_len, tcp_resp, &tcp_resp_len, tid) == ESP_OK) {
                            send(client_sock, tcp_resp, tcp_resp_len, 0);
                        }
                    }
                }
            }
        }

        close(client_sock);
        ESP_LOGI(TAG, "Cliente desconectado");
    }

    close(listen_sock);
    vTaskDelete(nullptr);
}

/* ================================================================
 * API Pública
 * ================================================================ */

esp_err_t bridge_rs485_init(const bridge_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    s_bridge_config = *config;
    s_running = true;

    /* Inicializar UART RS485 cambio*/
    esp_err_t ret = rs485_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando RS485 UART: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Crear cola de comandos locales */
    s_rx_queue = xQueueCreate(16, sizeof(uint8_t *));
    if (!s_rx_queue) {
        ESP_LOGE(TAG, "Error creando cola RX");
        return ESP_ERR_NO_MEM;
    }

    /* Arrancar servidor TCP */
    xTaskCreate(tcp_server_task, "bridge_tcp", 6144, nullptr, 5, &s_tcp_task);

    ESP_LOGI(TAG, "Puente Modbus TCP ↔ RS485 iniciado (modo %d)", config->mode);
    return ESP_OK;
}

esp_err_t bridge_rs485_set_mode(bridge_mode_t mode)
{
    if (mode >= BRIDGE_MODE_COUNT) return ESP_ERR_INVALID_ARG;
    s_bridge_config.mode = mode;
    ESP_LOGI(TAG, "Modo puente cambiado a: %d", mode);
    return ESP_OK;
}

bridge_mode_t bridge_rs485_get_mode(void)
{
    return s_bridge_config.mode;
}

esp_err_t bridge_rs485_send(const uint8_t *data, size_t len)
{
    int written = uart_write_bytes((uart_port_t)s_bridge_config.rs485_uart_num,
                                    data, len);
    return (written == len) ? ESP_OK : ESP_FAIL;
}

QueueHandle_t bridge_rs485_get_rx_queue(void)
{
    return s_rx_queue;
}
