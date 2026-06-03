/**
 * main.cpp — NEMA23 LILYGO T-CAN485 Gateway
 *
 * Funcionalidades:
 *   - WiFi AP+STA con servidor HTTP integrado
 *   - Puente Modbus TCP ↔ RS485
 *   - CAN bus (TWAI) opcional
 *   - Control de PLC Kinco por Modbus RTU
 *   - Página web de control embebida
 *
 * Arquitectura:
 *   ┌─────────────────────────────────────────────┐
 *   │  WiFi AP (192.168.4.1) + STA (opcional)      │
 *   │  └─ HTTP Server :80                          │
 *   │     ├─ GET  /          → panel de control    │
 *   │     ├─ GET  /api/status → JSON estado        │
 *   │     └─ POST /api/command → comandos JSON     │
 *   │                                              │
 *   │  Modbus TCP Server :502                      │
 *   │  └─ Puente ↔ RS485 (UART2, GPIO 21/22)      │
 *   │                                              │
 *   │  CAN Bus (TWAI, GPIO 26/27) [opcional]       │
 *   │                                              │
 *   └─────────────────────────────────────────────┘
 */

#include "wifi_manager.h"
#include "bridge_rs485.h"
#include "can_bus.h"
#include "status_led.h"
#include "relay_control.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "main";

/* ================================================================
 * Prueba RS485 / Modbus RTU contra PLC Kinco MK043E-20DT
 * ================================================================ */

#define PLC_RS485_TEST_ENABLE       0
#define PLC_RS485_TEST_BAUD         9600
#define PLC_RS485_TEST_SLAVE_ID     1
#define PLC_RS485_TEST_REGISTER     50      /* Kinco eje 0 control word: 40051 -> address base 0 = 50 */
#define PLC_RS485_TEST_VALUE        0x0001
#define PLC_RS485_TEST_TIMEOUT_MS   500

static uint16_t modbus_crc16_local(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static size_t modbus_append_crc(uint8_t *frame, size_t len_without_crc)
{
    uint16_t crc = modbus_crc16_local(frame, len_without_crc);
    frame[len_without_crc] = (uint8_t)(crc & 0xFF);
    frame[len_without_crc + 1] = (uint8_t)(crc >> 8);
    return len_without_crc + 2;
}

static bool modbus_crc_ok(const uint8_t *frame, size_t len)
{
    if (len < 4) return false;
    uint16_t rx_crc = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    return rx_crc == modbus_crc16_local(frame, len - 2);
}

static void log_frame_hex(const char *prefix, const uint8_t *data, size_t len)
{
    char hex[3 * 32 + 1] = {};
    size_t shown = len < 32 ? len : 32;
    for (size_t i = 0; i < shown; i++) {
        snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", data[i]);
    }
    ESP_LOGI(TAG, "%s len=%u %s%s", prefix, (unsigned)len, hex, len > shown ? "..." : "");
}

static void __attribute__((unused)) plc_rs485_test_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));

    uint8_t req[8] = {
        PLC_RS485_TEST_SLAVE_ID,
        0x06,
        (uint8_t)(PLC_RS485_TEST_REGISTER >> 8),
        (uint8_t)(PLC_RS485_TEST_REGISTER & 0xFF),
        (uint8_t)(PLC_RS485_TEST_VALUE >> 8),
        (uint8_t)(PLC_RS485_TEST_VALUE & 0xFF),
        0,
        0,
    };
    size_t req_len = modbus_append_crc(req, 6);

    ESP_LOGI(TAG, "PLC RS485 test: slave=%u FC06 holding_reg=%u value=%u baud=%u",
             PLC_RS485_TEST_SLAVE_ID, PLC_RS485_TEST_REGISTER,
             PLC_RS485_TEST_VALUE, PLC_RS485_TEST_BAUD);
    log_frame_hex("PLC TX", req, req_len);

    uint8_t resp[64] = {};
    size_t resp_len = 0;
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, PLC_RS485_TEST_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PLC RS485 test sin respuesta: %s", esp_err_to_name(ret));
        vTaskDelete(nullptr);
        return;
    }

    log_frame_hex("PLC RX", resp, resp_len);

    if (!modbus_crc_ok(resp, resp_len)) {
        ESP_LOGE(TAG, "PLC RS485 test: CRC de respuesta invalido");
    } else if (resp_len == req_len && memcmp(resp, req, req_len) == 0) {
        ESP_LOGI(TAG, "PLC RS485 test OK: el PLC hizo eco de FC06");
    } else if (resp_len >= 5 && resp[0] == PLC_RS485_TEST_SLAVE_ID && resp[1] == (0x06 | 0x80)) {
        ESP_LOGE(TAG, "PLC RS485 exception: code=0x%02X", resp[2]);
    } else {
        ESP_LOGW(TAG, "PLC RS485 test: respuesta valida pero no coincide con el eco esperado");
    }

    vTaskDelete(nullptr);
}

/* ================================================================
 * Tarea de procesamiento de comandos locales
 * ================================================================ */

static void cmd_processor_task(void *arg)
{
    QueueHandle_t bridge_rx_queue = bridge_rs485_get_rx_queue();
    QueueHandle_t can_rx_queue = can_bus_get_rx_queue();
    uint8_t *msg = nullptr;

    ESP_LOGI(TAG, "Procesador de comandos iniciado");

    while (1) {
        /* Procesar comandos del puente Modbus (dirigidos al slave local) */
        if (bridge_rx_queue && xQueueReceive(bridge_rx_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (msg) {
                ESP_LOGI(TAG, "Comando Modbus local: func=%02X", msg[1]);
                /* El dispatch de comandos locales se maneja via HTTP API (wifi_manager).
                   Este bloque recibe tramas Modbus TCP dirigidas al slave ID local;
                   por ahora solo se loguean. */
                free(msg);
            }
        }

        /* Procesar mensajes CAN */
        if (can_rx_queue) {
            twai_message_t can_msg;
            if (xQueueReceive(can_rx_queue, &can_msg, 0) == pdTRUE) {
                ESP_LOGI(TAG, "CAN RX: ID=0x%lx DLC=%d", can_msg.identifier, can_msg.data_length_code);
                /* CAN bus disponible para integracion futura (ej. IPC-CFX, sensores externos).
                   Por ahora solo se loguean los mensajes entrantes. */
            }
        }
    }
}

/* ================================================================
 * Tarea de monitoreo de estado
 * ================================================================ */

/* ================================================================
 * Entry Point
 * ================================================================ */

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " NEMA23 LILYGO Gateway v1.0");
    ESP_LOGI(TAG, " ESP32 + T-CAN485 + Kinco Modbus");
    ESP_LOGI(TAG, " WiFi AP + Modbus TCP ↔ RS485 + CAN");
    ESP_LOGI(TAG, "============================================");

    /* ——— 1. Habilitar boost converter (alimentación 5V para RS485/CAN) ——— */
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BOOST_EN_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)BOOST_EN_GPIO, 1);
    ESP_LOGI(TAG, "Boost converter habilitado (GPIO %d)", BOOST_EN_GPIO);

    /* ——— 2. Inicializar WiFi (solo STA, se conecta a la red del laboratorio) ——— */
    wifi_config_user_t wifi_cfg = {};
    wifi_cfg.ap_ssid[0] = '\0';          /* AP deshabilitado */
    wifi_cfg.ap_password[0] = '\0';
    strcpy(wifi_cfg.sta_ssid, "NS-LAB");
    strcpy(wifi_cfg.sta_password, "@L4b0r4t0r10@");
    wifi_cfg.enable_ap = false;          /* sin Access Point */
    wifi_cfg.enable_sta = true;          /* solo cliente (STA) */

    esp_err_t ret = wifi_manager_init(&wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando WiFi: %s", esp_err_to_name(ret));
        return;
    }

    /* Esperar a que la conexión STA obtenga IP */
    EventGroupHandle_t wifi_evt = wifi_manager_get_event_group();
    EventBits_t wifi_bits = xEventGroupWaitBits(wifi_evt, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (!(wifi_bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi STA todavia sin IP; HTTP arrancara y quedara disponible al conectar");
    }

    /* ——— 3. Inicializar servidor HTTP ——— */
    ret = http_server_start(80);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTP server no pudo iniciar: %s", esp_err_to_name(ret));
    }

    /* ——— 5. Inicializar salidas de relé ("topes") ——— */
    ret = relay_control_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Relés no iniciados: %s", esp_err_to_name(ret));
    }

    /* ——— 6. Inicializar puente Modbus TCP ↔ RS485 ——— */
    bridge_config_t bridge_cfg = {};
    bridge_cfg.mode = BRIDGE_MODE_MODBUS_TCP_RS485;
    bridge_cfg.modbus_tcp_port = 502;
    bridge_cfg.rs485_baud = PLC_RS485_TEST_BAUD;
    bridge_cfg.rs485_uart_num = RS485_UART_PORT;
    bridge_cfg.slave_id = 0xF7;          /* ID 247: gateway local (control de relés vía coils) */
    bridge_cfg.enable_filter = true;     /* Las tramas a 0xF7 se procesan local (no van al bus RS485) */

    ret = bridge_rs485_init(&bridge_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Puente RS485 no iniciado: %s", esp_err_to_name(ret));
    }

#if PLC_RS485_TEST_ENABLE
    if (ret == ESP_OK) {
        xTaskCreate(plc_rs485_test_task, "plc_rs485_test", 4096, nullptr, 4, nullptr);
    }
#endif

    /* ——— 7. Inicializar CAN bus (opcional) ——— */
    can_bus_config_t can_cfg = can_bus_get_default_config();
    ret = can_bus_init(&can_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CAN bus no iniciado: %s", esp_err_to_name(ret));
    } else {
        can_bus_start_rx_task(4096, 3);
    }

    /* ——— 8. Indicador de estado WS2812B ——— */
    ret = status_led_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Indicador WS2812B no iniciado: %s", esp_err_to_name(ret));
    }

    /* ——— 9. Arrancar tareas ——— */
    xTaskCreate(cmd_processor_task, "cmd_proc", 6144, nullptr, 4, nullptr);

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " Sistema listo.");
    ESP_LOGI(TAG, " STA      : SSID=%s (IP via DHCP; ver log 'WiFi STA IP')", wifi_cfg.sta_ssid);
    ESP_LOGI(TAG, " Modbus TCP en puerto 502 -> RS485");
    ESP_LOGI(TAG, "============================================");

    /* Loop principal — dormir */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
