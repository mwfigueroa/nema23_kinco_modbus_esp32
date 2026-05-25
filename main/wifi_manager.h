/**
 * wifi_manager.h — Gestión WiFi AP + STA + HTTP Server
 *
 * Modos de operación:
 *   - AP mode: Crea un access point "NEMA23_Gateway" para conexión directa
 *   - STA mode: Se conecta a una red existente
 *   - AP+STA: Ambos simultáneos (por defecto)
 *
 * El servidor HTTP se usa para:
 *   - Recibir comandos REST/JSON
 *   - Servir página web de control básica
 *   - Puente Modbus TCP ↔ RS485
 */

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Event bits para sincronización */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_AP_STARTED_BIT BIT2

/* Configuración por defecto — sobreescribible vía NVS o comandos */
#define WIFI_AP_SSID_DEFAULT    "NEMA23_Gateway"
#define WIFI_AP_PASS_DEFAULT    "12345678"
#define WIFI_AP_MAX_CONN        4

typedef struct {
    char ap_ssid[32];
    char ap_password[64];
    char sta_ssid[32];
    char sta_password[64];
    bool enable_ap;
    bool enable_sta;
} wifi_config_user_t;

/**
 * Inicializa WiFi en modo AP+STA.
 * Si no hay STA configurado, solo levanta el AP.
 */
esp_err_t wifi_manager_init(const wifi_config_user_t *config);

/**
 * Devuelve el event group para sincronización.
 */
EventGroupHandle_t wifi_manager_get_event_group(void);

/**
 * Inicia el servidor HTTP en el puerto especificado.
 * Registra los handlers para:
 *   - GET  /api/status      → estado del sistema
 *   - POST /api/command     → comandos al motor/puente
 *   - GET  /                → página web de control
 */
esp_err_t http_server_start(uint16_t port);

/**
 * Detiene el servidor HTTP.
 */
esp_err_t http_server_stop(void);

/**
 * Devuelve el handle del HTTP server (para registro de handlers extra).
 */
httpd_handle_t http_server_get_handle(void);

#ifdef __cplusplus
}
#endif
