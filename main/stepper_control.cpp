/**
 * stepper_control.cpp — Control NEMA23 con FastAccelStepper + RMT
 *
 * Usa FastAccelStepper para generar curvas de aceleración suaves
 * y RMT del ESP32 para los pulsos STEP/DIR precisos.
 *
 * Dependencias:
 *   - components/FastAccelStepper (copia simbólica del proyecto nema23_p4)
 *   - stepper_motor_encoder.c/h (encoder RMT custom)
 */

#include "stepper_control.h"
#include "pin_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cmath>

static const char *TAG = "stepper";

/* ——— Si FastAccelStepper está disponible ——— */
#if __has_include("FastAccelStepper.h")
  #define HAS_FAST_ACCEL_STEPPER 1
#else
  #define HAS_FAST_ACCEL_STEPPER 0
  #warning "FastAccelStepper no encontrado — usando control básico RMT"
#endif

#if HAS_FAST_ACCEL_STEPPER
  #include "FastAccelStepper.h"
#endif

static stepper_config_t s_cfg = {};
static stepper_state_t s_state = STEPPER_STATE_IDLE;
static int32_t s_current_position = 0;
static int32_t s_current_speed = 0;
static bool s_enabled = false;

#if HAS_FAST_ACCEL_STEPPER
  static FastAccelStepperEngine *s_engine = nullptr;
  static FastAccelStepper *s_stepper = nullptr;
#else
  static rmt_channel_handle_t s_rmt_channel = nullptr;
  static rmt_encoder_handle_t s_rmt_encoder = nullptr;
#endif

/* ================================================================
 * Configuración por defecto
 * ================================================================ */

stepper_config_t stepper_control_get_default_config(void)
{
    stepper_config_t cfg = {};
    cfg.step_gpio = STEPPER_STEP_GPIO;
    cfg.dir_gpio = STEPPER_DIR_GPIO;
    cfg.en_gpio = STEPPER_EN_GPIO;
    cfg.limit_min_gpio = LIMIT_SW_MIN_GPIO;
    cfg.limit_max_gpio = LIMIT_SW_MAX_GPIO;
    cfg.rmt_resolution_hz = STEPPER_RMT_RESOLUTION;
    cfg.max_speed_steps_per_sec = 50000;        // 50 kHz step rate
    cfg.acceleration_steps_per_sec2 = 100000;   // 100 kHz/s²
    cfg.steps_per_rev = 200 * 16;               // 200 steps/rev * 16 microsteps = 3200
    cfg.en_active_low = true;                   // La mayoría de drivers
    cfg.dir_invert = false;
    return cfg;
}

/* ================================================================
 * Inicialización
 * ================================================================ */

esp_err_t stepper_control_init(const stepper_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_cfg = *config;

    /* Configurar GPIOs de dirección y enable */
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    uint64_t out_mask = (1ULL << s_cfg.dir_gpio) | (1ULL << s_cfg.en_gpio);
    io_conf.pin_bit_mask = out_mask;
    gpio_config(&io_conf);

    /* Inicialmente deshabilitado */
    gpio_set_level((gpio_num_t)s_cfg.en_gpio, s_cfg.en_active_low ? 1 : 0);
    gpio_set_level((gpio_num_t)s_cfg.dir_gpio, s_cfg.dir_invert ? 1 : 0);

    /* Configurar finales de carrera como entrada con pull-up */
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pin_bit_mask = (1ULL << s_cfg.limit_min_gpio) | (1ULL << s_cfg.limit_max_gpio);
    gpio_config(&io_conf);

#if HAS_FAST_ACCEL_STEPPER
    /* Inicializar FastAccelStepper */
    s_engine = new FastAccelStepperEngine();
    s_engine->init();

    s_stepper = s_engine->stepperConnectToPin((gpio_num_t)s_cfg.step_gpio);
    if (!s_stepper) {
        ESP_LOGE(TAG, "Error conectando FastAccelStepper al pin %d", s_cfg.step_gpio);
        return ESP_FAIL;
    }

    s_stepper->setDirectionPin((gpio_num_t)s_cfg.dir_gpio, s_cfg.dir_invert);
    s_stepper->setEnablePin((gpio_num_t)s_cfg.en_gpio, s_cfg.en_active_low);
    s_stepper->setAutoEnable(true);

    s_stepper->setSpeedInHz(s_cfg.max_speed_steps_per_sec);
    s_stepper->setAcceleration(s_cfg.acceleration_steps_per_sec2);

    ESP_LOGI(TAG, "FastAccelStepper inicializado: STEP=%d DIR=%d EN=%d",
             s_cfg.step_gpio, s_cfg.dir_gpio, s_cfg.en_gpio);
#else
    /* Modo fallback: RMT básico */
    ESP_LOGI(TAG, "Usando control RMT básico: STEP=%d DIR=%d EN=%d",
             s_cfg.step_gpio, s_cfg.dir_gpio, s_cfg.en_gpio);

    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)s_cfg.step_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = s_cfg.rmt_resolution_hz,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_new_tx_channel(&tx_chan_config, &s_rmt_channel);
    rmt_enable(s_rmt_channel);
#endif

    s_state = STEPPER_STATE_IDLE;
    return ESP_OK;
}

/* ================================================================
 * Comandos de movimiento
 * ================================================================ */

esp_err_t stepper_control_move_to(int32_t target_steps)
{
    if (s_state == STEPPER_STATE_ESTOP) return ESP_ERR_INVALID_STATE;

    s_state = STEPPER_STATE_RUNNING;

#if HAS_FAST_ACCEL_STEPPER
    s_stepper->moveTo(target_steps);
#else
    /* Fallback: movimiento simple */
    int32_t delta = target_steps - s_current_position;
    if (delta == 0) {
        s_state = STEPPER_STATE_IDLE;
        return ESP_OK;
    }
    gpio_set_level((gpio_num_t)s_cfg.dir_gpio,
                   (delta > 0) ^ s_cfg.dir_invert ? 1 : 0);

    /* Generar pulsos manualmente (mejor usar FastAccelStepper!) */
    uint32_t abs_delta = abs(delta);
    uint32_t delay_us = 1000000 / s_cfg.max_speed_steps_per_sec / 2;
    for (uint32_t i = 0; i < abs_delta; i++) {
        gpio_set_level((gpio_num_t)s_cfg.step_gpio, 1);
        esp_rom_delay_us(delay_us);
        gpio_set_level((gpio_num_t)s_cfg.step_gpio, 0);
        esp_rom_delay_us(delay_us);
    }
    s_current_position = target_steps;
    s_state = STEPPER_STATE_IDLE;
#endif

    return ESP_OK;
}

esp_err_t stepper_control_move_relative(int32_t delta_steps)
{
    return stepper_control_move_to(s_current_position + delta_steps);
}

esp_err_t stepper_control_run_speed(int32_t speed_steps_per_sec)
{
    if (s_state == STEPPER_STATE_ESTOP) return ESP_ERR_INVALID_STATE;

    s_current_speed = speed_steps_per_sec;
    s_state = STEPPER_STATE_RUNNING;

#if HAS_FAST_ACCEL_STEPPER
    uint32_t speed_hz = (speed_steps_per_sec < 0)
                            ? (uint32_t)(-speed_steps_per_sec)
                            : (uint32_t)speed_steps_per_sec;
    if (speed_hz == 0) {
        return stepper_control_stop();
    }
    s_stepper->setSpeedInHz(speed_hz);
    if (speed_steps_per_sec >= 0) {
        s_stepper->runForward();
    } else {
        s_stepper->runBackward();
    }
#endif

    ESP_LOGI(TAG, "Velocidad: %ld steps/s", (long)speed_steps_per_sec);
    return ESP_OK;
}

esp_err_t stepper_control_stop(void)
{
#if HAS_FAST_ACCEL_STEPPER
    s_stepper->stopMove();
#endif
    s_current_speed = 0;
    s_state = STEPPER_STATE_IDLE;
    ESP_LOGI(TAG, "Motor detenido");
    return ESP_OK;
}

esp_err_t stepper_control_estop(void)
{
#if HAS_FAST_ACCEL_STEPPER
    s_stepper->forceStop();
#endif
    s_current_speed = 0;
    s_state = STEPPER_STATE_ESTOP;
    ESP_LOGI(TAG, "EMERGENCY STOP");
    return ESP_OK;
}

esp_err_t stepper_control_home(void)
{
    s_state = STEPPER_STATE_HOMING;
    ESP_LOGI(TAG, "Homing iniciado...");

    /* TODO: implementar secuencia de homing con finales de carrera */
    /* Mover hacia atrás hasta activar limit_min, luego avanzar un offset */

    s_state = STEPPER_STATE_IDLE;
    s_current_position = 0;
    ESP_LOGI(TAG, "Homing completado, posición=0");
    return ESP_OK;
}

esp_err_t stepper_control_enable(bool enable)
{
    s_enabled = enable;
    gpio_set_level((gpio_num_t)s_cfg.en_gpio,
                   enable ^ s_cfg.en_active_low ? 1 : 0);
    ESP_LOGI(TAG, "Driver %s", enable ? "HABILITADO" : "DESHABILITADO");
    return ESP_OK;
}

/* ================================================================
 * Getters
 * ================================================================ */

stepper_state_t stepper_control_get_state(void)
{
#if HAS_FAST_ACCEL_STEPPER
    if (s_state != STEPPER_STATE_ESTOP) {
        if (s_stepper->isRunning()) {
            s_state = STEPPER_STATE_RUNNING;
        } else if (s_state == STEPPER_STATE_RUNNING || s_state == STEPPER_STATE_HOMING) {
            s_state = STEPPER_STATE_IDLE;
        }
        s_current_position = s_stepper->getCurrentPosition();
    }
#endif
    return s_state;
}

int32_t stepper_control_get_position(void)
{
    stepper_control_get_state();  /* Actualiza posición */
    return s_current_position;
}

int32_t stepper_control_get_current_speed(void)
{
    return s_current_speed;
}
