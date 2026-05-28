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
 * Prueba PLC Kinco por Modbus RTU
 * ================================================================ */

#define PLC_STEP_SLAVE_ID       1
#define PLC_STEP_REGISTER       100     /* Kinco K5: VW0 como holding register de prueba */
#define PLC_VW2_REGISTER        101     /* VW2: word alto de la variable de 32 bits */
#define PLC_STEP_TIMEOUT_MS     500

static uint16_t modbus_crc16_local(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static size_t modbus_append_crc(uint8_t *frame, size_t len_without_crc)
{
    uint16_t crc = modbus_crc16_local(frame, len_without_crc);
    frame[len_without_crc] = (uint8_t)(crc & 0xFF);
    frame[len_without_crc + 1] = (uint8_t)(crc >> 8);
    return len_without_crc + 2;
}

static bool modbus_crc_ok(const uint8_t *frame, size_t len)
{
    if (len < 4) return false;
    uint16_t rx_crc = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    return rx_crc == modbus_crc16_local(frame, len - 2);
}

static esp_err_t plc_write_holding_registers(uint16_t start_reg, const uint16_t *values,
                                             uint16_t quantity, uint8_t *exception_code)
{
    if (!values || quantity == 0 || quantity > 16) {
        return ESP_ERR_INVALID_ARG;
    }
    if (exception_code) *exception_code = 0;

    uint8_t req[40] = {};
    size_t payload_len = 7 + (size_t)quantity * 2;
    if (payload_len + 2 > sizeof(req)) {
        return ESP_ERR_INVALID_ARG;
    }

    req[0] = PLC_STEP_SLAVE_ID;
    req[1] = 0x10;
    req[2] = (uint8_t)(start_reg >> 8);
    req[3] = (uint8_t)(start_reg & 0xFF);
    req[4] = (uint8_t)(quantity >> 8);
    req[5] = (uint8_t)(quantity & 0xFF);
    req[6] = (uint8_t)(quantity * 2);
    for (uint16_t i = 0; i < quantity; i++) {
        req[7 + i * 2] = (uint8_t)(values[i] >> 8);
        req[8 + i * 2] = (uint8_t)(values[i] & 0xFF);
    }
    size_t req_len = modbus_append_crc(req, payload_len);

    uint8_t resp[64] = {};
    size_t resp_len = 0;
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, PLC_STEP_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == PLC_STEP_SLAVE_ID && resp[1] == (0x10 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        return ESP_FAIL;
    }

    if (resp_len != 8 || resp[0] != PLC_STEP_SLAVE_ID || resp[1] != 0x10 ||
        resp[2] != (uint8_t)(start_reg >> 8) || resp[3] != (uint8_t)(start_reg & 0xFF) ||
        resp[4] != (uint8_t)(quantity >> 8) || resp[5] != (uint8_t)(quantity & 0xFF)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static esp_err_t plc_write_int32_vw0_vw2(int32_t value, uint8_t *exception_code)
{
    uint32_t raw = (uint32_t)value;
    uint16_t words[2] = {
        (uint16_t)(raw & 0xFFFF),    /* VW0: word bajo */
        (uint16_t)(raw >> 16),       /* VW2: word alto */
    };
    return plc_write_holding_registers(PLC_STEP_REGISTER, words, 2, exception_code);
}

static esp_err_t plc_read_holding_registers(uint16_t start_reg, uint16_t quantity,
                                            uint16_t *values, uint8_t *exception_code)
{
    if (!values || quantity == 0 || quantity > 16) {
        return ESP_ERR_INVALID_ARG;
    }
    if (exception_code) *exception_code = 0;

    uint8_t req[8] = {
        PLC_STEP_SLAVE_ID,
        0x03,
        (uint8_t)(start_reg >> 8),
        (uint8_t)(start_reg & 0xFF),
        (uint8_t)(quantity >> 8),
        (uint8_t)(quantity & 0xFF),
        0,
        0,
    };
    size_t req_len = modbus_append_crc(req, 6);

    uint8_t resp[64] = {};
    size_t resp_len = 0;
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, PLC_STEP_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == PLC_STEP_SLAVE_ID && resp[1] == (0x03 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        return ESP_FAIL;
    }

    size_t expected_len = 5 + (size_t)quantity * 2;
    if (resp_len != expected_len || resp[0] != PLC_STEP_SLAVE_ID ||
        resp[1] != 0x03 || resp[2] != quantity * 2) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        values[i] = ((uint16_t)resp[3 + i * 2] << 8) | resp[4 + i * 2];
    }

    return ESP_OK;
}

static int32_t plc_words_to_int32(uint16_t vw0, uint16_t vw2)
{
    uint32_t raw = ((uint32_t)vw2 << 16) | vw0;
    return (int32_t)raw;
}

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
    long speed = 0;
    bool has_speed = json_find_int(json, "speed", &speed);
    esp_err_t r = ESP_OK;
    bool known = true;

    if (strcmp(cmd, "move_to") == 0) {
        if (has_speed) {
            r = stepper_control_set_move_speed((uint32_t)speed);
        }
        if (r == ESP_OK) {
            r = stepper_control_move_to((int32_t)arg);
        }
    } else if (strcmp(cmd, "move_rel") == 0) {
        if (has_speed) {
            r = stepper_control_set_move_speed((uint32_t)speed);
        }
        if (r == ESP_OK) {
            r = stepper_control_move_relative((int32_t)arg);
        }
    } else if (strcmp(cmd, "run_speed") == 0) {
        r = stepper_control_run_speed((int32_t)arg);
    } else if (strcmp(cmd, "set_speed") == 0) {
        r = stepper_control_set_move_speed((uint32_t)arg);
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
    } else if (strcmp(cmd, "plc_send_step") == 0) {
        int32_t pos = stepper_control_get_position();
        uint8_t exception_code = 0;
        uint16_t vw0 = (uint16_t)((uint32_t)pos & 0xFFFF);
        uint16_t vw2 = (uint16_t)((uint32_t)pos >> 16);
        r = plc_write_int32_vw0_vw2(pos, &exception_code);

        if (r == ESP_OK) {
            uint16_t readback[2] = {};
            uint8_t read_exception = 0;
            esp_err_t rr = plc_read_holding_registers(PLC_STEP_REGISTER, 2,
                                                      readback, &read_exception);
            if (rr == ESP_OK) {
                int32_t read_value = plc_words_to_int32(readback[0], readback[1]);
                bool match = (read_value == pos);
                snprintf(resp, resp_sz,
                         "{\"result\":\"%s\",\"cmd\":\"%s\",\"pos\":%ld,"
                         "\"plc_slave\":%u,\"plc_register\":%u,"
                         "\"plc_value_32\":%ld,\"readback_value_32\":%ld,"
                         "\"vw0_register\":%u,\"vw0\":%u,"
                         "\"vw2_register\":%u,\"vw2\":%u,"
                         "\"readback_vw0\":%u,\"readback_vw2\":%u,"
                         "\"verify\":\"%s\"}",
                         match ? "ok" : "error", cmd, (long)pos,
                         PLC_STEP_SLAVE_ID, PLC_STEP_REGISTER,
                         (long)pos, (long)read_value,
                         PLC_STEP_REGISTER, vw0,
                         PLC_VW2_REGISTER, vw2,
                         readback[0], readback[1],
                         match ? "match" : "mismatch");
            } else {
                snprintf(resp, resp_sz,
                         "{\"result\":\"error\",\"cmd\":\"%s\",\"pos\":%ld,"
                         "\"plc_slave\":%u,\"plc_register\":%u,"
                         "\"plc_value_32\":%ld,\"vw0_register\":%u,\"vw0\":%u,"
                         "\"vw2_register\":%u,\"vw2\":%u,"
                         "\"write_status\":\"ok\",\"read_status\":\"lectura_error\","
                         "\"exception\":%u,\"err\":\"%s\"}",
                         cmd, (long)pos, PLC_STEP_SLAVE_ID, PLC_STEP_REGISTER,
                         (long)pos, PLC_STEP_REGISTER, vw0,
                         PLC_VW2_REGISTER, vw2,
                         read_exception, esp_err_to_name(rr));
            }
        } else {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"pos\":%ld,"
                     "\"plc_slave\":%u,\"plc_register\":%u,\"plc_value_32\":%ld,"
                     "\"vw0_register\":%u,\"vw0\":%u,"
                     "\"vw2_register\":%u,\"vw2\":%u,"
                     "\"write_status\":\"escritura_error\","
                     "\"exception\":%u,\"err\":\"%s\"}",
                     cmd, (long)pos, PLC_STEP_SLAVE_ID, PLC_STEP_REGISTER, (long)pos,
                     PLC_STEP_REGISTER, vw0,
                     PLC_VW2_REGISTER, vw2,
                     exception_code, esp_err_to_name(r));
        }
        return;
    } else if (strcmp(cmd, "plc_read_vw") == 0) {
        uint16_t values[2] = {};
        uint8_t exception_code = 0;
        r = plc_read_holding_registers(PLC_STEP_REGISTER, 2, values, &exception_code);

        if (r == ESP_OK) {
            int32_t plc_value_32 = plc_words_to_int32(values[0], values[1]);
            snprintf(resp, resp_sz,
                     "{\"result\":\"ok\",\"cmd\":\"%s\",\"plc_slave\":%u,"
                     "\"vw0_register\":%u,\"vw0\":%u,"
                     "\"vw2_register\":%u,\"vw2\":%u,"
                     "\"plc_value_32\":%ld,"
                     "\"read_status\":\"lectura_ok\"}",
                     cmd, PLC_STEP_SLAVE_ID,
                     PLC_STEP_REGISTER, values[0],
                     PLC_VW2_REGISTER, values[1],
                     (long)plc_value_32);
        } else {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"plc_slave\":%u,"
                     "\"vw0_register\":%u,\"vw2_register\":%u,"
                     "\"exception\":%u,\"err\":\"%s\","
                     "\"read_status\":\"lectura_error\"}",
                     cmd, PLC_STEP_SLAVE_ID,
                     PLC_STEP_REGISTER, PLC_VW2_REGISTER,
                     exception_code, esp_err_to_name(r));
        }
        return;
    } else {
        known = false;
    }

    if (!known) {
        snprintf(resp, resp_sz, "{\"result\":\"error\",\"msg\":\"cmd desconocido\",\"cmd\":\"%s\"}", cmd);
        return;
    }

    snprintf(resp, resp_sz,
             "{\"result\":\"%s\",\"cmd\":\"%s\",\"pos\":%ld,\"speed\":%lu,\"state\":%d}",
             (r == ESP_OK) ? "ok" : "error", cmd,
             (long)stepper_control_get_position(),
             (unsigned long)stepper_control_get_move_speed(),
             (int)stepper_control_get_state());
}

static esp_err_t http_get_status_handler(httpd_req_t *req)
{
    char resp[220];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"device\":\"NEMA23_Gateway\",\"mode\":\"sta\","
             "\"pos\":%ld,\"speed\":%ld,\"move_speed\":%lu,\"state\":%d}",
             (long)stepper_control_get_position(),
             (long)stepper_control_get_current_speed(),
             (unsigned long)stepper_control_get_move_speed(),
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

    char resp[1024];
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
        ".plc{margin-top:8px;color:#9ff}"
        ".ver{font-size:12px;color:#9aa;margin-top:10px}"
        ".row{display:flex;gap:8px;flex-wrap:wrap}"
        "input{background:#0f3460;color:white;border:1px solid #e94560;padding:8px 12px;border-radius:6px;width:100px}"
        "</style></head><body>"
        "<h1>⚙️ NEMA23 Gateway</h1>"
        "<div class='status'><strong>Estado:</strong> <span id='status'>Online</span><br>"
        "<strong>Posición:</strong> <span id='pos'>0</span> steps</div>"
        "<div class='plc'><strong>PLC:</strong> <span id='plc_status'>Sin lectura</span></div>"
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
        "<div class='row' style='margin-top:8px'>"
        "<button onclick='send(\"plc_send_step\")'>PLC Steps -> VW0/VW2</button>"
        "<button onclick='send(\"plc_read_vw\")'>Leer VW0 / VW2</button>"
        "</div>"
        "<div class='ver'>UI: 1.1</div>"
        "<script>async function send(cmd,arg){let b={cmd:cmd};if(arg)b.arg=arg;"
        "let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
        "let j=await r.json();document.getElementById('status').textContent=JSON.stringify(j);"
        "if(cmd==='plc_read_vw'||cmd==='plc_send_step'){let el=document.getElementById('plc_status');"
        "if(j.result==='ok'){let v=j.readback_value_32!==undefined?j.readback_value_32:j.plc_value_32;"
        "let p=j.pos!==undefined?' POS='+j.pos:'';el.textContent='OK DINT='+v+' VW0='+j.vw0+' VW2='+j.vw2+p;}"
        "else{let v=j.readback_value_32!==undefined?' DINT='+j.readback_value_32:'';"
        "let w0=j.readback_vw0!==undefined?j.readback_vw0:(j.vw0!==undefined?j.vw0:'?');"
        "let w2=j.readback_vw2!==undefined?j.readback_vw2:(j.vw2!==undefined?j.vw2:'?');"
        "el.textContent='ERROR '+(j.verify||j.err||'sin respuesta')+v+' VW0='+w0+' VW2='+w2}}}"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
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
