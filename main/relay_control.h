/**
 * relay_control.h — Salidas de relé ("topes")
 *
 * Maneja RELAY_COUNT salidas digitales (GPIO 13/14) que activan relés.
 * El nivel activo se define en pin_config.h (RELAY_ACTIVE_LEVEL).
 *
 * El control se expone vía Modbus coils desde bridge_rs485.cpp:
 *   - coil 0 → relé 1 (GPIO 13)
 *   - coil 1 → relé 2 (GPIO 14)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configura los GPIO de relé como salidas y los deja desactivados.
 * Debe llamarse una sola vez al arranque.
 */
esp_err_t relay_control_init(void);

/**
 * Activa/desactiva un relé.
 * @param index  0..RELAY_COUNT-1
 * @param on     true = relé activado, false = desactivado
 */
esp_err_t relay_control_set(uint8_t index, bool on);

/**
 * Devuelve el estado lógico actual de un relé (true = activado).
 * Fuera de rango devuelve false.
 */
bool relay_control_get(uint8_t index);

#ifdef __cplusplus
}
#endif
