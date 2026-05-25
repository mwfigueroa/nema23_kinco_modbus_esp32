/**
 * bridge_rs485.h — Puente Modbus TCP ↔ RS485
 *
 * Recibe tramas Modbus TCP por WiFi y las reenvía al bus RS485.
 * Las respuestas del bus RS485 se devuelven al cliente TCP.
 *
 * También permite:
 *   - Inyectar comandos locales (control del NEMA23)
 *   - Modo sniffer para debug
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BRIDGE_MODE_MODBUS_TCP_RS485 = 0,   // Modbus TCP ↔ RS485 (default)
    BRIDGE_MODE_RAW_TCP_RS485,           // Raw TCP ↔ RS485 transparente
    BRIDGE_MODE_LOCAL_ONLY,              // Solo comandos locales (motor)
    BRIDGE_MODE_COUNT
} bridge_mode_t;

typedef struct {
    bridge_mode_t mode;
    uint16_t modbus_tcp_port;       // default 502
    uint32_t rs485_baud;
    uint8_t rs485_uart_num;
    uint8_t slave_id;               // Modbus slave ID local
    bool enable_filter;             // Filtrar comandos locales vs puente
} bridge_config_t;

/**
 * Inicializa el puente Modbus TCP ↔ RS485.
 * Arranca el servidor TCP Modbus y la UART RS485.
 */
esp_err_t bridge_rs485_init(const bridge_config_t *config);

/**
 * Cambia el modo del puente en caliente.
 */
esp_err_t bridge_rs485_set_mode(bridge_mode_t mode);

/**
 * Devuelve el modo actual.
 */
bridge_mode_t bridge_rs485_get_mode(void);

/**
 * Envía una trama directamente al bus RS485 (para comandos locales).
 * @param data  Buffer con la trama Modbus RTU/ASCII
 * @param len   Longitud de la trama
 */
esp_err_t bridge_rs485_send(const uint8_t *data, size_t len);

/**
 * Cola de comandos recibidos por Modbus TCP que deben procesarse localmente.
 * Cada elemento es una trama Modbus RTU completa.
 */
QueueHandle_t bridge_rs485_get_rx_queue(void);

#ifdef __cplusplus
}
#endif
