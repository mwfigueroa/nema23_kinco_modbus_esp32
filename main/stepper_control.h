/**
 * stepper_control.h — Control del NEMA23 via FastAccelStepper + RMT
 *
 * Opera el motor paso a paso NEMA23 usando:
 *   - FastAccelStepper para generación de curvas de aceleración
 *   - RMT para generación precisa de pulsos STEP/DIR/EN
 *
 * Comandos disponibles:
 *   - Posicionamiento absoluto/relativo
 *   - Velocidad constante (modo velocidad)
 *   - Homing (con finales de carrera)
 *   - Parada de emergencia
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STEPPER_STATE_IDLE = 0,
    STEPPER_STATE_RUNNING,
    STEPPER_STATE_HOMING,
    STEPPER_STATE_ERROR,
    STEPPER_STATE_ESTOP
} stepper_state_t;

typedef struct {
    uint8_t step_gpio;
    uint8_t dir_gpio;
    uint8_t en_gpio;
    uint8_t limit_min_gpio;
    uint8_t limit_max_gpio;
    uint32_t rmt_resolution_hz;
    uint32_t max_speed_steps_per_sec;       // Velocidad máxima (steps/s)
    uint32_t acceleration_steps_per_sec2;   // Aceleración (steps/s²)
    uint32_t steps_per_rev;                 // Steps por revolución (microstepping)
    bool en_active_low;                     // true = EN bajo activa el driver
    bool dir_invert;                        // Invertir dirección
} stepper_config_t;

/**
 * Inicializa el controlador del stepper.
 * Debe llamarse una sola vez.
 */
esp_err_t stepper_control_init(const stepper_config_t *config);

/**
 * Mueve el motor a una posición absoluta (en steps).
 * No bloqueante — llamar stepper_control_get_state() para monitorear.
 */
esp_err_t stepper_control_move_to(int32_t target_steps);

/**
 * Mueve el motor una cantidad relativa de steps.
 */
esp_err_t stepper_control_move_relative(int32_t delta_steps);

/**
 * Inicia movimiento continuo a velocidad constante.
 * @param speed_steps_per_sec  positivo = CW, negativo = CCW
 */
esp_err_t stepper_control_run_speed(int32_t speed_steps_per_sec);
esp_err_t stepper_control_set_move_speed(uint32_t speed_steps_per_sec);

/**
 * Detiene el motor con desaceleración controlada.
 */
esp_err_t stepper_control_stop(void);

/**
 * Parada de emergencia (inmediata, sin deceleración).
 */
esp_err_t stepper_control_estop(void);

/**
 * Ejecuta la secuencia de homing.
 */
esp_err_t stepper_control_home(void);

/**
 * Habilita/deshabilita el driver del motor.
 */
esp_err_t stepper_control_enable(bool enable);

/**
 * Devuelve el estado actual del stepper.
 */
stepper_state_t stepper_control_get_state(void);

/**
 * Devuelve la posición actual en steps.
 */
int32_t stepper_control_get_position(void);

/**
 * Devuelve la velocidad actual en steps/s.
 */
int32_t stepper_control_get_current_speed(void);
uint32_t stepper_control_get_move_speed(void);

/**
 * Configuración por defecto para NEMA23 con driver externo.
 */
stepper_config_t stepper_control_get_default_config(void);

#ifdef __cplusplus
}
#endif
