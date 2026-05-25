/**
 * wifi_manager.cpp — Implementación WiFi AP+STA + HTTP Server
 */

#include "wifi_manager.h"
#include "pin_config.h"
#include "stepper_control.h"
#include "bridge_rs485.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static const char *TAG = "wifi_mgr";

static EventGroupHandle_t s_wifi_event_group = nullptr;
static httpd_handle_t s_http_server = nullptr;
static wifi_config_user_t s_config = {};
static esp_netif_t *s_ap_netif = nullptr;
static esp_netif_t *s_sta_netif = nullptr;

/* ================================================================
 * Handlers HTTP
 * ================================================================ */

/* ----------------------------------------------------------------
 * Parser JSON mínimo (sin dependencias). Reconoce {"cmd":"...","arg":N}
 * con arg numérico o entre comillas. Suficiente para el panel de control;
 * para cargas arbitrarias conviene migrar a cJSON.
 * ---------------------------------------------------------------- */

static bool json_find_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ':' || *p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool json_find_int(const char *json, const char *key, long *out)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ':' || *p == ' ' || *p == '\t' || *p == '"') p++;
    char *end = nullptr;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    *out = v;
    return true;
}

/* Ejecuta el comando JSON y arma la respuesta. */
static void dispatch_command(const char *json, char *resp, size_t resp_sz)
{
    char cmd[32] = {0};
    if (!json_find_str(json, "cmd", cmd, sizeof(cmd))) {
        snprintf(resp, resp_sz, "{\"result\":\"error\",\"msg\":\"falta cmd\"}");
        return;
    }

    long arg = 0;
    bool has_arg = json_find_int(json, "arg", &arg);
    esp_err_t r = ESP_OK;
    bool known = true;

    if (strcmp(cmd, "move_to") == 0) {
        r = stepper_control_move_to((int32_t)arg);
    } else if (strcmp(cmd, "move_rel") == 0) {
        r = stepper_control_move_relative((int32_t)arg);
    } else if (strcmp(cmd, "run_speed") == 0) {
        r = stepper_control_run_speed((int32_t)arg);
    } else if (strcmp(cmd, "stop") == 0) {
        r = stepper_control_stop();
    } else if (strcmp(cmd, "estop") == 0) {
        r = stepper_control_estop();
    } else if (strcmp(cmd, "home") == 0) {
        r = stepper_control_home();
    } else if (strcmp(cmd, "enable") == 0) {
        r = stepper_control_enable(has_arg ? (arg != 0) : true);
    } else if (strcmp(cmd, "rs485_mode") == 0) {
        r = bridge_rs485_set_mode(BRIDGE_MODE_MODBUS_TCP_RS485);
    } else if (strcmp(cmd, "can_mode") == 0) {
        r = bridge_rs485_set_mode(BRIDGE_MODE_LOCAL_ONLY);
    } else {
        known = false;
    }

    if (!known) {
        snprintf(resp, resp_sz, "{\"result\":\"error\",\"msg\":\"cmd desconocido\",\"cmd\":\"%s\"}", cmd);
        return;
    }

    snprintf(resp, resp_sz,
             "{\"result\":\"%s\",\"cmd\":\"%s\",\"pos\":%ld,\"state\":%d}",
             (r == ESP_OK) ? "ok" : "error", cmd,
             (long)stepper_control_get_position(),
             (int)stepper_control_get_state());
}

static esp_err_t http_get_status_handler(httpd_req_t *req)
{
    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"device\":\"NEMA23_Gateway\",\"mode\":\"ap+sta\","
             "\"pos\":%ld,\"speed\":%ld,\"state\":%d}",
             (long)stepper_control_get_position(),
             (long)stepper_control_get_current_speed(),
             (int)stepper_control_get_state());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_post_command_handler(httpd_req_t *req)
{
    char buf[512] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Comando recibido: %s", buf);

    char resp[160];
    dispatch_command(buf, resp, sizeof(resp));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_get_root_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NEMA23 Gateway</title>"
        "<style>body{font-family:sans-serif;max-width:600px;margin:2em auto;padding:0 1em;background:#1a1a2e;color:#e0e0e0}"
        "h1{color:#e94560}button{background:#0f3460;color:white;border:none;padding:12px 24px;margin:4px;border-radius:6px;cursor:pointer;font-size:16px}"
        "button:hover{background:#e94560}.status{background:#16213e;padding:1em;border-radius:8px;margin:1em 0}"
        ".row{display:flex;gap:8px;flex-wrap:wrap}"
        "input{background:#0f3460;color:white;border:1px solid #e94560;padding:8px 12px;border-radius:6px;width:100px}"
        "</style></head><body>"
        "<h1>⚙️ NEMA23 Gateway</h1>"
        "<div class='status'><strong>Estado:</strong> <span id='status'>Online</span><br>"
        "<strong>Posición:</strong> <span id='pos'>0</span> steps</div>"
        "<div class='row'>"
        "<button onclick='send(\"move_rel\",1000)'>▶ +1000</button>"
        "<button onclick='send(\"move_rel\",-1000)'>◀ -1000</button>"
        "</div>"
        "<div class='row'>"
        "<button onclick='send(\"home\")'>🏠 Home</button>"
        "<button onclick='send(\"stop\")'>⏹ Stop</button>"
        "<button onclick='send(\"estop\")'>🛑 E-Stop</button>"
        "</div>"
        "<div class='row' style='margin-top:8px'>"
        "<input id='speed' value='5000' placeholder='steps/s'>"
        "<button onclick='send(\"run_speed\",document.getElementById(\"speed\").value)'>⚡ Run</button>"
        "</div>"
        "<div class='row' style='margin-top:8px'>"
        "<button onclick='send(\"rs485_mode\")'>🔀 RS485</button>"
        "<button onclick='send(\"can_mode\")'>🔀 CAN</button>"
        "</div>"
        "<script>async function send(cmd,arg){let b={cmd:cmd};if(arg)b.arg=arg;"
        "let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
        "let j=await r.json();document.getElementById('status').textContent=JSON.stringify(j)}"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t s_uri_handlers[] = {
    {.uri = "/",              .method = HTTP_GET,  .handler = http_get_root_handler},
    {.uri = "/api/status",    .method = HTTP_GET,  .handler = http_get_status_handler},
    {.uri = "/api/command",   .method = HTTP_POST, .handler = http_post_command_handler},
};

/* ================================================================
 * Event Handlers WiFi
 * ================================================================ */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_AP_STACONNECTED: {
            auto *evt = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Cliente conectado al AP: " MACSTR, MAC2STR(evt->mac));
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            auto *evt = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Cliente desconectado del AP: " MACSTR, MAC2STR(evt->mac));
            break;
        }
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi STA desconectado, reintentando...");
            /* Limpiar el bit para que el estado de conectividad (y el LED) refleje
             * la realidad; se vuelve a setear en IP_EVENT_STA_GOT_IP al reconectar. */
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            esp_wifi_connect();
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            auto *evt = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "WiFi STA IP: " IPSTR, IP2STR(&evt->ip_info.ip));
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        }
        default:
            break;
        }
    }
}

/* ================================================================
 * API Pública
 * ================================================================ */

esp_err_t wifi_manager_init(const wifi_config_user_t *config)
{
    if (config) {
        s_config = *config;
    } else {
        strcpy(s_config.ap_ssid, WIFI_AP_SSID_DEFAULT);
        strcpy(s_config.ap_password, WIFI_AP_PASS_DEFAULT);
        s_config.enable_ap = true;
        s_config.enable_sta = false;
    }

    s_wifi_event_group = xEventGroupCreate();

    /* Inicializar NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Inicializar TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Crear interfaces */
    if (s_config.enable_ap) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (s_config.enable_sta) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    /* Configurar WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Registrar event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr));

    /* Configurar AP si está habilitado */
    if (s_config.enable_ap) {
        wifi_config_t ap_cfg = {};
        strcpy((char *)ap_cfg.ap.ssid, s_config.ap_ssid);
        strcpy((char *)ap_cfg.ap.password, s_config.ap_password);
        ap_cfg.ap.max_connection = WIFI_AP_MAX_CONN;
        ap_cfg.ap.authmode = (strlen(s_config.ap_password) > 0)
                             ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        ap_cfg.ap.channel = 1;

        ESP_ERROR_CHECK(esp_wifi_set_mode(
            s_config.enable_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    }

    /* Configurar STA si está habilitado y tiene SSID */
    if (s_config.enable_sta && strlen(s_config.sta_ssid) > 0) {
        wifi_config_t sta_cfg = {};
        strcpy((char *)sta_cfg.sta.ssid, s_config.sta_ssid);
        strcpy((char *)sta_cfg.sta.password, s_config.sta_password);
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(
            s_config.enable_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }

    /* Arrancar WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    if (s_config.enable_ap) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_AP_STARTED_BIT);
        ESP_LOGI(TAG, "AP iniciado: SSID=%s", s_config.ap_ssid);
    }

    return ESP_OK;
}

EventGroupHandle_t wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}

esp_err_t http_server_start(uint16_t port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 16;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_http_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    for (size_t i = 0; i < sizeof(s_uri_handlers) / sizeof(s_uri_handlers[0]); i++) {
        httpd_register_uri_handler(s_http_server, &s_uri_handlers[i]);
    }

    ESP_LOGI(TAG, "HTTP server iniciado en puerto %u", port);
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_http_server) {
        esp_err_t ret = httpd_stop(s_http_server);
        s_http_server = nullptr;
        return ret;
    }
    return ESP_OK;
}

httpd_handle_t http_server_get_handle(void)
{
    return s_http_server;
}
