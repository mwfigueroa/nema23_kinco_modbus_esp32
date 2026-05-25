#pragma once

#include "esp_err.h"
#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t resolution;        // RMT tick resolution (Hz)
    uint32_t total_steps;       // Cantidad total de pulsos de la curva accel/decel
    uint32_t start_freq_hz;     // Frecuencia STEP inicial (Hz)
    uint32_t end_freq_hz;       // Frecuencia STEP final de la curva (Hz)
} stepper_motor_curve_encoder_config_t;

typedef struct {
    uint32_t resolution;        // RMT tick resolution (Hz)
} stepper_motor_uniform_encoder_config_t;

esp_err_t rmt_new_stepper_motor_curve_encoder(const stepper_motor_curve_encoder_config_t *config,
                                              rmt_encoder_handle_t *ret_encoder);

esp_err_t rmt_new_stepper_motor_uniform_encoder(const stepper_motor_uniform_encoder_config_t *config,
                                                rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif
