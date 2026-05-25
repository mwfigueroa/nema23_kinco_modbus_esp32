/**
 * status_led.cpp — Indicador de estado en el WS2812B (GPIO 4)
 *
 * Ver status_led.h para la jerarquía de prioridades. El driver es
 * espressif/led_strip sobre el periférico RMT (declarado en idf_component.yml).
 */

#include "status_led.h"
#include "pin_config.h"
#include "wifi_manager.h"
#include "stepper_control.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "status_led";

/* Periodo del tick de refresco de la tarea (ms). */
#define LED_TICK_MS         25

/* Brillo máximo de la respiración (0-255). El WS2812B es muy luminoso; un
 * tope bajo evita que moleste y reduce el consumo. */
#define LED_BREATH_MAX      70

/* Periodo de la respiración completa (subida + bajada), en ticks. */
#define LED_BREATH_PERIOD   120     /* 120 * 25 ms = 3.0 s */

/* Periodo del parpadeo de E-STOP (cada cuántos ticks alterna on/off). */
#define LED_ESTOP_TOGGLE    4       /* 4 * 25 ms = 100 ms -> ~5 Hz (on+off) */
#define LED_ESTOP_MAX       180     /* rojo bien visible */

/* Brillo del cian FIJO de "motor moviéndose". Sólido (sin respiración) para
 * distinguirlo del cian que respira de "solo AP". */
#define LED_RUNNING_LEVEL   70

static led_strip_handle_t s_strip = nullptr;

/**
 * Onda triangular 0 -> max -> 0 a partir del contador de ticks, para el efecto
 * de "respiración". Devuelve un brillo en [0, max].
 */
static uint8_t breath_level(uint32_t tick, uint8_t max)
{
    uint32_t pos = tick % LED_BREATH_PERIOD;
    uint32_t half = LED_BREATH_PERIOD / 2;
    uint32_t up = (pos < half) ? pos : (LED_BREATH_PERIOD - pos);  /* 0..half */
    return (uint8_t)((up * max) / half);
}

static inline void led_show(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void status_led_task(void *arg)
{
    EventGroupHandle_t wifi_evt = wifi_manager_get_event_group();
    uint32_t tick = 0;

    while (true) {
        stepper_state_t stepper = stepper_control_get_state();
        EventBits_t bits = wifi_evt ? xEventGroupGetBits(wifi_evt) : 0;

        /* ——— Prioridad 1: E-STOP — rojo, parpadeo rápido ——— */
        if (stepper == STEPPER_STATE_ESTOP) {
            bool on = ((tick / LED_ESTOP_TOGGLE) & 1) == 0;
            led_show(on ? LED_ESTOP_MAX : 0, 0, 0);
        }
        /* ——— Prioridad 2: motor moviéndose — cian fijo ——— */
        else if (stepper == STEPPER_STATE_RUNNING) {
            led_show(0, LED_RUNNING_LEVEL, LED_RUNNING_LEVEL);
        }
        /* ——— Prioridad 3: conectividad WiFi — respiración suave ——— */
        else {
            uint8_t level = breath_level(tick, LED_BREATH_MAX);
            if (bits & WIFI_CONNECTED_BIT) {
                /* STA conectado a la red del laboratorio → verde */
                led_show(0, level, 0);
            } else if (bits & WIFI_AP_STARTED_BIT) {
                /* Solo AP propio levantado, sin STA → azul */
                led_show(0, 0, level);
            } else {
                /* Arrancando / sin red todavía → blanco tenue */
                uint8_t dim = level / 3;
                led_show(dim, dim, dim);
            }
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

esp_err_t status_led_init(void)
{
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = WS2812B_DATA_GPIO;
    strip_config.max_leds = WS2812B_LED_COUNT;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;  /* 10 MHz */
    rmt_config.mem_block_symbols = 64;
    rmt_config.flags.with_dma = false;

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear el led_strip: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_strip);

    BaseType_t ok = xTaskCreate(status_led_task, "status_led", 3072, nullptr, 2, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea status_led");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Indicador WS2812B iniciado (GPIO %d)", WS2812B_DATA_GPIO);
    return ESP_OK;
}
