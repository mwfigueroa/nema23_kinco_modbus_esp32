#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_check.h"
#include "esp_log.h"
#include "stepper_motor_encoder.h"

static const char *TAG = "stepper_enc";

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    uint32_t resolution;
    uint32_t total_steps;
    uint32_t start_freq_hz;
    uint32_t end_freq_hz;
    uint32_t current_step;
} stepper_motor_curve_encoder_t;

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    uint32_t resolution;
} stepper_motor_uniform_encoder_t;

static size_t rmt_encode_stepper_motor_curve(rmt_encoder_t *encoder,
                                             rmt_channel_handle_t channel,
                                             const void *primary_data,
                                             size_t data_size,
                                             rmt_encode_state_t *ret_state)
{
    (void)primary_data;
    (void)data_size;
    stepper_motor_curve_encoder_t *curve = __containerof(encoder, stepper_motor_curve_encoder_t, base);
    rmt_encoder_handle_t copy = curve->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    size_t encoded = 0;

    while (curve->current_step < curve->total_steps) {
        const uint32_t denominator = (curve->total_steps > 1) ? (curve->total_steps - 1) : 1;
        const float progress = (float)curve->current_step / (float)denominator;

        /*
         * Perfil S sobre la posicion normalizada.
         *
         * smoothstep(p) = p^2 * (3 - 2p)
         *
         * Esto reduce la pendiente al inicio y al final de la rampa. En la practica
         * hace mucho mas visible y mas suave el arranque desde reposo que una rampa
         * lineal o una de aceleracion constante pura.
         */
        const float shaped_progress = progress * progress * (3.0f - 2.0f * progress);

        /*
         * Interpolamos sobre frecuencia^2 para conservar una relacion razonable entre
         * la forma de la curva y la dinamica del motor.
         */
        const float start_freq_sq = (float)curve->start_freq_hz * (float)curve->start_freq_hz;
        const float end_freq_sq = (float)curve->end_freq_hz * (float)curve->end_freq_hz;
        float freq_sq = start_freq_sq + (end_freq_sq - start_freq_sq) * shaped_progress;
        if (freq_sq < 1.0f) {
            freq_sq = 1.0f;
        }
        float freq = sqrtf(freq_sq);
        if (freq < 1.0f) {
            freq = 1.0f;
        }

        uint32_t ticks_period = (uint32_t)((float)curve->resolution / freq);
        uint32_t ticks_half = ticks_period / 2;
        if (ticks_half == 0) {
            ticks_half = 1;
        }

        rmt_symbol_word_t sym = {
            .level0 = 1,
            .duration0 = ticks_half,
            .level1 = 0,
            .duration1 = ticks_period - ticks_half,
        };
        encoded += copy->encode(copy, channel, &sym, sizeof(sym), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            curve->current_step++;
            session_state = RMT_ENCODING_RESET;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
            return encoded;
        }
    }

    *ret_state = RMT_ENCODING_COMPLETE;
    curve->current_step = 0; // listo para reuso
    return encoded;
}

static esp_err_t rmt_del_stepper_motor_curve(rmt_encoder_t *encoder)
{
    stepper_motor_curve_encoder_t *curve = __containerof(encoder, stepper_motor_curve_encoder_t, base);
    rmt_del_encoder(curve->copy_encoder);
    free(curve);
    return ESP_OK;
}

static esp_err_t rmt_reset_stepper_motor_curve(rmt_encoder_t *encoder)
{
    stepper_motor_curve_encoder_t *curve = __containerof(encoder, stepper_motor_curve_encoder_t, base);
    rmt_encoder_reset(curve->copy_encoder);
    curve->current_step = 0;
    return ESP_OK;
}

esp_err_t rmt_new_stepper_motor_curve_encoder(const stepper_motor_curve_encoder_config_t *config,
                                              rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    stepper_motor_curve_encoder_t *curve = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid arg");
    ESP_GOTO_ON_FALSE(config->total_steps > 0,
                      ESP_ERR_INVALID_ARG, err, TAG, "total_steps fuera de rango");
    ESP_GOTO_ON_FALSE(config->start_freq_hz > 0 && config->end_freq_hz > 0,
                      ESP_ERR_INVALID_ARG, err, TAG, "frecuencias invalidas");

    curve = calloc(1, sizeof(stepper_motor_curve_encoder_t));
    ESP_GOTO_ON_FALSE(curve, ESP_ERR_NO_MEM, err, TAG, "no mem curve");

    curve->base.encode = rmt_encode_stepper_motor_curve;
    curve->base.del = rmt_del_stepper_motor_curve;
    curve->base.reset = rmt_reset_stepper_motor_curve;
    curve->resolution = config->resolution;
    curve->total_steps = config->total_steps;
    curve->start_freq_hz = config->start_freq_hz;
    curve->end_freq_hz = config->end_freq_hz;

    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &curve->copy_encoder),
                      err, TAG, "copy encoder fail");

    *ret_encoder = &curve->base;
    return ESP_OK;

err:
    if (curve) {
        if (curve->copy_encoder) rmt_del_encoder(curve->copy_encoder);
        free(curve);
    }
    return ret;
}

// ====================== Uniform encoder ======================

static size_t rmt_encode_stepper_motor_uniform(rmt_encoder_t *encoder,
                                               rmt_channel_handle_t channel,
                                               const void *primary_data,
                                               size_t data_size,
                                               rmt_encode_state_t *ret_state)
{
    stepper_motor_uniform_encoder_t *u = __containerof(encoder, stepper_motor_uniform_encoder_t, base);
    uint32_t freq_hz = *(const uint32_t *)primary_data;
    (void)data_size;

    uint32_t ticks_period = u->resolution / (freq_hz ? freq_hz : 1);
    uint32_t ticks_half = ticks_period / 2;
    if (ticks_half == 0) ticks_half = 1;

    rmt_symbol_word_t sym = {
        .level0 = 1, .duration0 = ticks_half,
        .level1 = 0, .duration1 = ticks_period - ticks_half,
    };

    return u->copy_encoder->encode(u->copy_encoder, channel, &sym, sizeof(sym), ret_state);
}

static esp_err_t rmt_del_stepper_motor_uniform(rmt_encoder_t *encoder)
{
    stepper_motor_uniform_encoder_t *u = __containerof(encoder, stepper_motor_uniform_encoder_t, base);
    rmt_del_encoder(u->copy_encoder);
    free(u);
    return ESP_OK;
}

static esp_err_t rmt_reset_stepper_motor_uniform(rmt_encoder_t *encoder)
{
    stepper_motor_uniform_encoder_t *u = __containerof(encoder, stepper_motor_uniform_encoder_t, base);
    rmt_encoder_reset(u->copy_encoder);
    return ESP_OK;
}

esp_err_t rmt_new_stepper_motor_uniform_encoder(const stepper_motor_uniform_encoder_config_t *config,
                                                rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    stepper_motor_uniform_encoder_t *u = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid arg");

    u = calloc(1, sizeof(stepper_motor_uniform_encoder_t));
    ESP_GOTO_ON_FALSE(u, ESP_ERR_NO_MEM, err, TAG, "no mem uniform");

    u->base.encode = rmt_encode_stepper_motor_uniform;
    u->base.del = rmt_del_stepper_motor_uniform;
    u->base.reset = rmt_reset_stepper_motor_uniform;
    u->resolution = config->resolution;

    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &u->copy_encoder), err, TAG, "copy encoder fail");

    *ret_encoder = &u->base;
    return ESP_OK;

err:
    if (u) free(u);
    return ret;
}
