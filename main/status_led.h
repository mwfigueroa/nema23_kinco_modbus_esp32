/**
 * status_led.h — Indicador de estado en el WS2812B (GPIO 4)
 *
 * Un único LED RGB no puede mostrar todos los estados a la vez, así que la
 * tarea interna resuelve por PRIORIDAD: los estados críticos pisan a los
 * informativos. Hoy se distinguen:
 *
 *   Prioridad 1 (crítico):  E-STOP del motor       → rojo, parpadeo rápido
 *   Prioridad 2 (actividad): motor moviéndose       → cian FIJO (sólido)
 *   Prioridad 3 (reposo):   conectividad WiFi       → respiración suave
 *                             - STA conectado        → verde
 *                             - solo AP (sin STA)    → azul
 *                             - arrancando / sin red → blanco tenue
 *
 * La tarea lee los estados directamente de stepper_control y del event group
 * de WiFi, por lo que no requiere que nadie le "empuje" cambios.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializa el WS2812B y arranca la tarea de indicación de estado.
 * Debe llamarse una sola vez, después de inicializar WiFi y el stepper.
 */
esp_err_t status_led_init(void);

#ifdef __cplusplus
}
#endif
