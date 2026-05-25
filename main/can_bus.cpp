/**
 * can_bus.cpp — Interfaz CAN Bus (TWAI) para SN65HVD231
 */

#include "can_bus.h"
#include "pin_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "can_bus";

static bool s_initialized = false;
static QueueHandle_t s_rx_queue = nullptr;

/* ================================================================
 * Configuración por defecto
 * ================================================================ */

can_bus_config_t can_bus_get_default_config(void)
{
    can_bus_config_t cfg = {};
    cfg.tx_gpio = CAN_TX_GPIO;
    cfg.rx_gpio = CAN_RX_GPIO;
    cfg.speed_mode_gpio = CAN_SPEED_MODE_GPIO;
    cfg.enable = true;

    /* Timing para 500 kbps con clock de 80 MHz */
    cfg.timing = TWAI_TIMING_CONFIG_500KBITS();

    /* Aceptar todas las tramas */
    cfg.filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    return cfg;
}

/* ================================================================
 * Inicialización
 * ================================================================ */

esp_err_t can_bus_init(const can_bus_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "CAN ya inicializado");
        return ESP_OK;
    }

    if (!config || !config->enable) {
        ESP_LOGI(TAG, "CAN deshabilitado");
        return ESP_OK;
    }

    /* Configurar pin de velocidad (HIGH = high-speed mode) */
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << config->speed_mode_gpio);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    /* SN65HVD231: pin Rs/SE en LOW = modo high-speed; HIGH = standby (transceiver
     * apagado). El ejemplo oficial de LilyGO lo maneja en LOW. */
    gpio_set_level((gpio_num_t)config->speed_mode_gpio, 0);  /* LOW = high-speed mode */

    /* Instalar driver TWAI */
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)config->tx_gpio,
        (gpio_num_t)config->rx_gpio,
        TWAI_MODE_NORMAL
    );

    esp_err_t ret = twai_driver_install(&g_config, &config->timing, &config->filter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error instalando driver TWAI: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error arrancando TWAI: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Crear cola de recepción */
    s_rx_queue = xQueueCreate(32, sizeof(twai_message_t));
    if (!s_rx_queue) {
        ESP_LOGE(TAG, "Error creando cola RX CAN");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "CAN bus iniciado: TX=%d RX=%d SE=%d(LOW) @ 500kbps",
             config->tx_gpio, config->rx_gpio, config->speed_mode_gpio);

    return ESP_OK;
}

/* ================================================================
 * Envío / Recepción
 * ================================================================ */

esp_err_t can_bus_send(const twai_message_t *msg)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return twai_transmit(msg, pdMS_TO_TICKS(100));
}

esp_err_t can_bus_receive(twai_message_t *msg, TickType_t timeout)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return twai_receive(msg, timeout);
}

/* ================================================================
 * Tarea de recepción
 * ================================================================ */

static void can_rx_task(void *arg)
{
    twai_message_t msg;
    while (s_initialized) {
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
            if (s_rx_queue) {
                xQueueSend(s_rx_queue, &msg, 0);
            }
            ESP_LOGD(TAG, "CAN RX: ID=0x%lx DLC=%d",
                     msg.identifier, msg.data_length_code);
        }
    }
    vTaskDelete(nullptr);
}

esp_err_t can_bus_start_rx_task(uint32_t stack_size, UBaseType_t priority)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    xTaskCreate(can_rx_task, "can_rx", stack_size, nullptr, priority, nullptr);
    return ESP_OK;
}

QueueHandle_t can_bus_get_rx_queue(void)
{
    return s_rx_queue;
}
