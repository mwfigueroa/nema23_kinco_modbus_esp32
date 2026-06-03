/**
 * status_led.cpp — Indicador de estado en el WS2812B (GPIO 4)
 *
 * Ver status_led.h para la jerarquía de prioridades.
 * Driver: espressif/led_strip sobre RMT.
 */

#include "status_led.h"
#include "pin_config.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "status_led";

/* ── Timing ──────────────────────────────────────────────────── */
#define LED_TICK_MS              25
#define LED_BREATH_SLOW_PERIOD   120     /* 120 * 25ms = 3.0s — verde/azul */
#define LED_BREATH_FAST_PERIOD   40      /*  40 * 25ms = 1.0s — amarillo motor */
#define LED_BLINK_PERIOD         80      /*  80 * 25ms = 2.0s — naranja blink */
#define LED_ERROR_HOLD_TICKS     200     /* 200 * 25ms = 5.0s — latch mínimo error */
#define LED_FLASH_TICKS          2       /*   2 * 25ms = 50ms — overlay MODBUS */

/* ── Colores base por estado ─────────────────────────────────── */
#define CLR_ERROR_R     180
#define CLR_ERROR_G     0
#define CLR_ERROR_B     0

#define CLR_MOVING_R    200
#define CLR_MOVING_G    100
#define CLR_MOVING_B    0

#define CLR_READY_R     0
#define CLR_READY_G     70
#define CLR_READY_B     0

#define CLR_NOT_READY_R 180
#define CLR_NOT_READY_G 60
#define CLR_NOT_READY_B 0

#define CLR_WIFI_R      0
#define CLR_WIFI_G      0
#define CLR_WIFI_B      50

#define CLR_BOOT_R      25
#define CLR_BOOT_G      25
#define CLR_BOOT_B      25

/* ── Brillo máximo respiración ───────────────────────────────── */
#define BREATH_MAX      70

/* ── Estado global ────────────────────────────────────────────── */
static led_strip_handle_t s_strip = nullptr;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static status_led_state_t s_current_state = STATUS_LED_STATE_BOOT;
static uint32_t s_error_ticks = 0;       /* contador para latch de error */

/* overlay MODBUS */
static uint8_t  s_flash_r = 0, s_flash_g = 0, s_flash_b = 0;
static uint32_t s_flash_ticks = 0;

/* ── Helpers ──────────────────────────────────────────────────── */

static uint8_t breath_level(uint32_t tick, uint32_t period, uint8_t max)
{
    uint32_t pos = tick % period;
    uint32_t half = period / 2;
    uint32_t up = (pos < half) ? pos : (period - pos);
    return (uint8_t)((up * max) / half);
}

static inline void led_show(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

/* ── Tarea principal ──────────────────────────────────────────── */

static void status_led_task(void *arg)
{
    uint32_t tick = 0;

    while (true) {
        /* 1. Leer estado + overlay con prioridad */
        status_led_state_t state;
        uint32_t err_ticks;
        uint8_t fr, fg, fb;
        uint32_t ft;

        portENTER_CRITICAL(&s_mux);
        state = s_current_state;
        err_ticks = s_error_ticks;
        fr = s_flash_r; fg = s_flash_g; fb = s_flash_b;
        ft = s_flash_ticks;
        portEXIT_CRITICAL(&s_mux);

        /* 2. Decidir color según prioridad */
        uint8_t r = 0, g = 0, b = 0;

        if (err_ticks > 0) {
            /* PRIO 1: ERROR — rojo fijo mientras dure el latch */
            r = CLR_ERROR_R; g = CLR_ERROR_G; b = CLR_ERROR_B;
        }
        else {
            switch (state) {
            case STATUS_LED_STATE_MOTOR_MOVING: {
                /* PRIO 2: amarillo respiración rápida */
                uint8_t lv = breath_level(tick, LED_BREATH_FAST_PERIOD, BREATH_MAX);
                r = (uint8_t)((uint32_t)CLR_MOVING_R * lv / BREATH_MAX);
                g = (uint8_t)((uint32_t)CLR_MOVING_G * lv / BREATH_MAX);
                b = (uint8_t)((uint32_t)CLR_MOVING_B * lv / BREATH_MAX);
                break;
            }
            case STATUS_LED_STATE_SYSTEM_READY: {
                /* PRIO 3: verde respiración lenta */
                uint8_t lv = breath_level(tick, LED_BREATH_SLOW_PERIOD, BREATH_MAX);
                r = 0;
                g = lv;
                b = 0;
                break;
            }
            case STATUS_LED_STATE_NOT_READY: {
                /* PRIO 4: naranja blink lento (on 50%, off 50%) */
                uint32_t pos = tick % LED_BLINK_PERIOD;
                if (pos < LED_BLINK_PERIOD / 2) {
                    r = CLR_NOT_READY_R; g = CLR_NOT_READY_G; b = CLR_NOT_READY_B;
                }
                break;
            }
            case STATUS_LED_STATE_WIFI_ONLY: {
                /* PRIO 5: azul respiración */
                uint8_t lv = breath_level(tick, LED_BREATH_SLOW_PERIOD, BREATH_MAX / 3);
                r = 0; g = 0; b = lv;
                break;
            }
            case STATUS_LED_STATE_BOOT:
            default: {
                /* PRIO 6: blanco tenue */
                uint8_t lv = breath_level(tick, LED_BREATH_SLOW_PERIOD, BREATH_MAX / 4);
                r = lv; g = lv; b = lv;
                break;
            }
            }
        }

        /* 3. Aplicar overlay MODBUS (solo si hay flash pendiente) */
        if (ft > 0) {
            /* mezcla 50% overlay + 50% base para no perder contexto */
            r = (uint8_t)(((uint32_t)r + (uint32_t)fr) / 2);
            g = (uint8_t)(((uint32_t)g + (uint32_t)fg) / 2);
            b = (uint8_t)(((uint32_t)b + (uint32_t)fb) / 2);

            portENTER_CRITICAL(&s_mux);
            if (s_flash_ticks > 0) s_flash_ticks--;
            portEXIT_CRITICAL(&s_mux);
        }

        led_show(r, g, b);

        /* 4. Decrementar latch de error */
        portENTER_CRITICAL(&s_mux);
        if (s_error_ticks > 0) s_error_ticks--;
        portEXIT_CRITICAL(&s_mux);

        tick++;
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

/* ── API pública ──────────────────────────────────────────────── */

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
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.mem_block_symbols = 64;
    rmt_config.flags.with_dma = false;

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear led_strip: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_strip);

    BaseType_t ok = xTaskCreate(status_led_task, "status_led", 3072, nullptr, 2, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear tarea status_led");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WS2812B iniciado (GPIO %d)", WS2812B_DATA_GPIO);
    return ESP_OK;
}

esp_err_t status_led_set_state(status_led_state_t state)
{
    if (state > STATUS_LED_STATE_ERROR) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_mux);
    s_current_state = state;

    /* Si es ERROR, iniciar latch de 5s */
    if (state == STATUS_LED_STATE_ERROR) {
        s_error_ticks = LED_ERROR_HOLD_TICKS;
    }
    /* Si es estado normal y no hay latch activo, mantener limpio */
    /* (el latch se decrementa solo en el task) */
    portEXIT_CRITICAL(&s_mux);

    return ESP_OK;
}

esp_err_t status_led_modbus_activity(status_led_modbus_event_t event)
{
    uint8_t r = 0, g = 0, b = 0;

    switch (event) {
    case STATUS_LED_MODBUS_READ:
        r = 0;   g = 120; b = 120;   /* cyan */
        break;
    case STATUS_LED_MODBUS_WRITE:
        r = 120; g = 0;   b = 80;    /* magenta */
        break;
    case STATUS_LED_MODBUS_ERROR:
        r = 180; g = 0;   b = 0;     /* rojo */
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_mux);
    s_flash_r = r;
    s_flash_g = g;
    s_flash_b = b;
    s_flash_ticks = LED_FLASH_TICKS;
    portEXIT_CRITICAL(&s_mux);

    return ESP_OK;
}
