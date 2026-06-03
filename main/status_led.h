/**
 * status_led.h — Indicador de estado en el WS2812B (GPIO 4)
 *
 * Un único LED RGB jerarquizado por PRIORIDAD de estado del sistema:
 *
 *   PRIO 1 (máxima): ERROR        → rojo fijo (5s latch)
 *         2:         MOTOR_MOVING → amarillo respiración rápida
 *         3:         SYSTEM_READY → verde respiración lenta
 *         4:         NOT_READY    → naranja blink lento
 *         5:         WIFI_ONLY    → azul respiración (sin PLC aún)
 *         6 (mínima): BOOT        → blanco tenue (arrancando)
 *
 * La actividad MODBUS produce solo un micro-flash overlay (50ms) que
 * no reemplaza el color base del estado del motor.
 *
 * La tarea interna decide qué mostrar según el estado más prioritario
 * seteado con status_led_set_state().
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_LED_STATE_BOOT = 0,       /* blanco tenue — arrancando */
    STATUS_LED_STATE_WIFI_ONLY,      /* azul respiración — sin PLC */
    STATUS_LED_STATE_NOT_READY,      /* naranja blink — falta HomeOK/enable */
    STATUS_LED_STATE_SYSTEM_READY,   /* verde respiración — listo */
    STATUS_LED_STATE_MOTOR_MOVING,   /* amarillo respiración rápida — PABS/HOME/auto */
    STATUS_LED_STATE_ERROR,          /* rojo fijo — PabsErr/HomeErr/timeout */
} status_led_state_t;

typedef enum {
    STATUS_LED_MODBUS_READ,
    STATUS_LED_MODBUS_WRITE,
    STATUS_LED_MODBUS_ERROR,
} status_led_modbus_event_t;

/**
 * Inicializa el WS2812B y arranca la tarea de indicación de estado.
 */
esp_err_t status_led_init(void);

/**
 * Establece el estado del sistema que el LED debe reflejar.
 * La tarea interna resuelve por prioridad: el estado más alto
 * entre todas las llamadas pendientes es el que se muestra.
 * Estados de menor prioridad se ignoran si hay uno mayor activo.
 */
esp_err_t status_led_set_state(status_led_state_t state);

/**
 * Muestra un micro-flash overlay de actividad MODBUS (50ms).
 * No cambia el estado base del LED.
 */
esp_err_t status_led_modbus_activity(status_led_modbus_event_t event);

#ifdef __cplusplus
}
#endif
