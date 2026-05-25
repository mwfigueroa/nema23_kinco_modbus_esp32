/**
 * can_bus.h — Interfaz CAN Bus (TWAI) para ESP32
 *
 * Usa el periférico TWAI (Two-Wire Automotive Interface) del ESP32
 * compatible con CAN 2.0B a través del transceiver SN65HVD231.
 *
 * Permite:
 *   - Enviar/recibir tramas CAN
 *   - Puente opcional Modbus TCP ↔ CAN
 *   - Control del motor vía CAN
 */

#pragma once

#include "esp_err.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    twai_timing_config_t timing;
    twai_filter_config_t filter;
    uint8_t tx_gpio;
    uint8_t rx_gpio;
    uint8_t speed_mode_gpio;    // HIGH = high-speed
    bool enable;
} can_bus_config_t;

/**
 * Inicializa el bus CAN con el transceiver SN65HVD231.
 */
esp_err_t can_bus_init(const can_bus_config_t *config);

/**
 * Envía un mensaje CAN.
 */
esp_err_t can_bus_send(const twai_message_t *msg);

/**
 * Recibe un mensaje CAN (no bloqueante).
 * @return ESP_OK si hay mensaje, ESP_ERR_TIMEOUT si no hay.
 */
esp_err_t can_bus_receive(twai_message_t *msg, TickType_t timeout);

/**
 * Arranca la tarea de recepción CAN que encola mensajes.
 * Los mensajes se pueden leer desde can_bus_get_rx_queue().
 */
esp_err_t can_bus_start_rx_task(uint32_t stack_size, UBaseType_t priority);

/**
 * Cola de mensajes CAN recibidos.
 */
QueueHandle_t can_bus_get_rx_queue(void);

/**
 * Configuración por defecto para SN65HVD231 a 500 kbps.
 */
can_bus_config_t can_bus_get_default_config(void);

#ifdef __cplusplus
}
#endif
