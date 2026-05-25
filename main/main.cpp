/**
 * main.cpp — NEMA23 LILYGO T-CAN485 Gateway
 *
 * Funcionalidades:
 *   - WiFi AP+STA con servidor HTTP integrado
 *   - Puente Modbus TCP ↔ RS485
 *   - CAN bus (TWAI) opcional
 *   - Control de motor NEMA23 vía FastAccelStepper + RMT
 *   - Página web de control embebida
 *
 * Arquitectura:
 *   ┌─────────────────────────────────────────────┐
 *   │  WiFi AP (192.168.4.1) + STA (opcional)      │
 *   │  └─ HTTP Server :80                          │
 *   │     ├─ GET  /          → panel de control    │
 *   │     ├─ GET  /api/status → JSON estado        │
 *   │     └─ POST /api/command → comandos JSON     │
 *   │                                              │
 *   │  Modbus TCP Server :502                      │
 *   │  └─ Puente ↔ RS485 (UART2, GPIO 21/22)      │
 *   │                                              │
 *   │  CAN Bus (TWAI, GPIO 26/27) [opcional]       │
 *   │                                              │
 *   │  Stepper Control (RMT, GPIO 5/18/25)         │
 *   └─────────────────────────────────────────────┘
 */

#include "wifi_manager.h"
#include "bridge_rs485.h"
#include "can_bus.h"
#include "stepper_control.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "main";

/* ================================================================
 * Tarea de procesamiento de comandos locales
 * ================================================================ */

static void cmd_processor_task(void *arg)
{
    QueueHandle_t bridge_rx_queue = bridge_rs485_get_rx_queue();
    QueueHandle_t can_rx_queue = can_bus_get_rx_queue();
    uint8_t *msg = nullptr;

    ESP_LOGI(TAG, "Procesador de comandos iniciado");

    while (1) {
        /* Procesar comandos del puente Modbus (dirigidos al slave local) */
        if (bridge_rx_queue && xQueueReceive(bridge_rx_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (msg) {
                ESP_LOGI(TAG, "Comando Modbus local: func=%02X", msg[1]);
                /* TODO: dispatch según function code */
                free(msg);
            }
        }

        /* Procesar mensajes CAN */
        if (can_rx_queue) {
            twai_message_t can_msg;
            if (xQueueReceive(can_rx_queue, &can_msg, 0) == pdTRUE) {
                ESP_LOGI(TAG, "CAN RX: ID=0x%lx DLC=%d", can_msg.identifier, can_msg.data_length_code);
                /* TODO: dispatch CAN messages */
            }
        }
    }
}

/* ================================================================
 * Tarea de monitoreo de estado
 * ================================================================ */

static void status_monitor_task(void *arg)
{
    while (1) {
        stepper_state_t st = stepper_control_get_state();
        int32_t pos = stepper_control_get_position();
        int32_t spd = stepper_control_get_current_speed();

        ESP_LOGI(TAG, "Stepper: state=%d pos=%ld speed=%ld steps/s",
                 (int)st, (long)pos, (long)spd);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ================================================================
 * Entry Point
 * ================================================================ */

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " NEMA23 LILYGO Gateway v1.0");
    ESP_LOGI(TAG, " ESP32 + T-CAN485 + FastAccelStepper");
    ESP_LOGI(TAG, " WiFi AP + Modbus TCP ↔ RS485 + CAN");
    ESP_LOGI(TAG, "============================================");

    /* ——— 1. Habilitar boost converter (alimentación 5V para RS485/CAN) ——— */
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BOOST_EN_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)BOOST_EN_GPIO, 1);
    ESP_LOGI(TAG, "Boost converter habilitado (GPIO %d)", BOOST_EN_GPIO);

    /* ——— 2. Inicializar WiFi (AP + STA al laboratorio) ——— */
    wifi_config_user_t wifi_cfg = {};
    strcpy(wifi_cfg.ap_ssid, "NEMA23_Gateway");
    strcpy(wifi_cfg.ap_password, "12345678");
    strcpy(wifi_cfg.sta_ssid, "NS-LAB");
    strcpy(wifi_cfg.sta_password, "@L4b0r4t0r10@");
    wifi_cfg.enable_ap = true;
    wifi_cfg.enable_sta = true;

    esp_err_t ret = wifi_manager_init(&wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando WiFi: %s", esp_err_to_name(ret));
        return;
    }

    /* Esperar a que el AP esté listo */
    EventGroupHandle_t wifi_evt = wifi_manager_get_event_group();
    xEventGroupWaitBits(wifi_evt, WIFI_AP_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));

    /* ——— 3. Inicializar servidor HTTP ——— */
    ret = http_server_start(80);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTP server no pudo iniciar: %s", esp_err_to_name(ret));
    }

    /* ——— 4. Inicializar Stepper ——— */
    stepper_config_t stepper_cfg = stepper_control_get_default_config();
    ret = stepper_control_init(&stepper_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando stepper: %s", esp_err_to_name(ret));
    } else {
        stepper_control_enable(true);
    }

    /* ——— 5. Inicializar puente Modbus TCP ↔ RS485 ——— */
    bridge_config_t bridge_cfg = {};
    bridge_cfg.mode = BRIDGE_MODE_MODBUS_TCP_RS485;
    bridge_cfg.modbus_tcp_port = 502;
    bridge_cfg.rs485_baud = 115200;
    bridge_cfg.rs485_uart_num = RS485_UART_PORT;
    bridge_cfg.slave_id = 0x01;         /* ID local para comandos directos */
    bridge_cfg.enable_filter = false;    /* Evita consumir el ID 1 hasta implementar dispatch local */

    ret = bridge_rs485_init(&bridge_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Puente RS485 no iniciado: %s", esp_err_to_name(ret));
    }

    /* ——— 6. Inicializar CAN bus (opcional) ——— */
    can_bus_config_t can_cfg = can_bus_get_default_config();
    ret = can_bus_init(&can_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CAN bus no iniciado: %s", esp_err_to_name(ret));
    } else {
        can_bus_start_rx_task(4096, 3);
    }

    /* ——— 7. Arrancar tareas ——— */
    xTaskCreate(cmd_processor_task, "cmd_proc", 6144, nullptr, 4, nullptr);
    xTaskCreate(status_monitor_task, "status_mon", 4096, nullptr, 2, nullptr);

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " Sistema listo.");
    ESP_LOGI(TAG, " AP local : SSID=%s -> http://192.168.4.1", wifi_cfg.ap_ssid);
    ESP_LOGI(TAG, " STA      : conectando a %s (IP via DHCP)", wifi_cfg.sta_ssid);
    ESP_LOGI(TAG, " Modbus TCP en puerto 502 -> RS485");
    ESP_LOGI(TAG, "============================================");

    /* Loop principal — dormir */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
