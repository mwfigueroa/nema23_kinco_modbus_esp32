/**
 * relay_control.cpp — Salidas de relé ("topes")
 *
 * Ver relay_control.h. Activo-alto/bajo según RELAY_ACTIVE_LEVEL (pin_config.h).
 */

#include "relay_control.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "relay";

static const gpio_num_t s_relay_gpio[RELAY_COUNT] = {
    (gpio_num_t)RELAY1_GPIO,
    (gpio_num_t)RELAY2_GPIO,
};
static bool s_relay_state[RELAY_COUNT] = { false, false };

/* Nivel GPIO que corresponde a "relé activado", según la polaridad configurada. */
static inline int level_for(bool on)
{
    return on ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL;
}

esp_err_t relay_control_init(void)
{
    gpio_config_t io = {};
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    uint64_t mask = 0;
    for (int i = 0; i < RELAY_COUNT; i++) {
        mask |= (1ULL << s_relay_gpio[i]);
    }
    io.pin_bit_mask = mask;

    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando GPIO de relés: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Arrancar con todos los relés desactivados. */
    for (int i = 0; i < RELAY_COUNT; i++) {
        gpio_set_level(s_relay_gpio[i], level_for(false));
        s_relay_state[i] = false;
    }

    ESP_LOGI(TAG, "Relés inicializados: R1=GPIO%d R2=GPIO%d (activo-%s)",
             RELAY1_GPIO, RELAY2_GPIO, RELAY_ACTIVE_LEVEL ? "alto" : "bajo");
    return ESP_OK;
}

esp_err_t relay_control_set(uint8_t index, bool on)
{
    if (index >= RELAY_COUNT) return ESP_ERR_INVALID_ARG;
    gpio_set_level(s_relay_gpio[index], level_for(on));
    s_relay_state[index] = on;
    ESP_LOGI(TAG, "Relé %u -> %s", (unsigned)(index + 1), on ? "ON" : "OFF");
    return ESP_OK;
}

bool relay_control_get(uint8_t index)
{
    if (index >= RELAY_COUNT) return false;
    return s_relay_state[index];
}
