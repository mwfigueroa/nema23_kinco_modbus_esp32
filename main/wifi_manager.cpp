/**
 * wifi_manager.cpp — Implementación WiFi AP+STA + HTTP Server
 */

#include "wifi_manager.h"
#include "pin_config.h"
#include "bridge_rs485.h"
#include "status_led.h"
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
 * Kinco MK043E-20DT por Modbus RTU
 * ================================================================ */

#define KINCO_SLAVE_ID          1
#define KINCO_TIMEOUT_MS        700
#define KINCO_EDGE_PULSE_MS     100
#define KINCO_MAX_REGS          16
#define KINCO_TEST_STEPS        5000

#define KINCO_CMD_ENABLE        0x0001
#define KINCO_CMD_RESET_POS     0x0003
#define KINCO_CMD_START_PABS    0x0005
#define KINCO_CMD_START_HOME    0x0009
#define KINCO_CMD_RESET_STATUS  0x0011
#define KINCO_CMD_STOP          0x0021
#define KINCO_CMD_START_PREL    0x0041
#define KINCO_CMD_JOG_FWD       0x0081
#define KINCO_CMD_JOG_BWD       0x0101

#define KINCO_DEFAULT_PABS_MAXF 2000
#define KINCO_DEFAULT_PABS_MINF 300
#define KINCO_DEFAULT_PABS_TIME 300
#define KINCO_DEFAULT_HOME_MAXF 1000
#define KINCO_DEFAULT_HOME_MINF 200
#define KINCO_DEFAULT_HOME_TIME 300
#define KINCO_MIN_FREQ          125
#define KINCO_MAX_FREQ          200000

typedef struct {
    uint16_t control;
    uint16_t pabs_pos;
    uint16_t pabs_maxf;
    uint16_t pabs_minf;
    uint16_t pabs_time;
    uint16_t home_mode;
    uint16_t home_dir;
    uint16_t home_minf;
    uint16_t home_maxf;
    uint16_t home_time;
    uint16_t prel_dist;
    uint16_t prel_maxf;
    uint16_t prel_minf;
    uint16_t prel_time;
    uint16_t pjog_speed;
    uint16_t pjog_dir;
    uint16_t status;
    uint16_t status2;
    uint16_t position;
} kinco_axis_map_t;

typedef struct {
    bool ok;
    uint8_t exception;
    int32_t command_steps;
    uint32_t maxf;
    uint16_t minf;
    uint16_t time_ms;
    uint16_t control_word;
    uint16_t home_dir;
    uint16_t status_word;
    uint16_t status_word2;
    uint16_t status_word3;
    int32_t position;
} kinco_axis_status_t;

static const kinco_axis_map_t s_kinco_axis[1] = {
    /*
     * Mapa del programa PLC pabs_basico_5000:
     * 40051/%VD100 = comando pasos, 40053/%VD104 = maxf,
     * 40055/%VW108 = minf, 40056/%VW110 = accel,
     * 40152/%VW302 = estado, 40101/%VD200 = posicion.
     * Las direcciones aqui son base 0 para Modbus: 40001 -> 0.
     */
    {69, 50, 52, 54, 55, 56, 57, 58, 59, 61, 0, 0, 0, 0, 0, 0, 151, 152, 100},
};

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
    if (!values || quantity == 0 || quantity > KINCO_MAX_REGS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (exception_code) *exception_code = 0;

    uint8_t req[7 + KINCO_MAX_REGS * 2 + 2] = {};
    size_t payload_len = 7 + (size_t)quantity * 2;
    if (payload_len + 2 > sizeof(req)) {
        return ESP_ERR_INVALID_ARG;
    }

    req[0] = KINCO_SLAVE_ID;
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
    status_led_modbus_activity(STATUS_LED_MODBUS_WRITE);
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, KINCO_TIMEOUT_MS);
    if (ret != ESP_OK) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == KINCO_SLAVE_ID && resp[1] == (0x10 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_FAIL;
    }

    if (resp_len != 8 || resp[0] != KINCO_SLAVE_ID || resp[1] != 0x10 ||
        resp[2] != (uint8_t)(start_reg >> 8) || resp[3] != (uint8_t)(start_reg & 0xFF) ||
        resp[4] != (uint8_t)(quantity >> 8) || resp[5] != (uint8_t)(quantity & 0xFF)) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static esp_err_t plc_write_single_register(uint16_t reg, uint16_t value,
                                           uint8_t *exception_code)
{
    if (exception_code) *exception_code = 0;

    uint8_t req[8] = {
        KINCO_SLAVE_ID,
        0x06,
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
        0,
        0,
    };
    size_t req_len = modbus_append_crc(req, 6);

    uint8_t resp[64] = {};
    size_t resp_len = 0;
    status_led_modbus_activity(STATUS_LED_MODBUS_WRITE);
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, KINCO_TIMEOUT_MS);
    if (ret != ESP_OK) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == KINCO_SLAVE_ID && resp[1] == (0x06 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_FAIL;
    }

    if (resp_len != req_len || memcmp(resp, req, req_len) != 0) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static esp_err_t plc_read_holding_registers(uint16_t start_reg, uint16_t quantity,
                                            uint16_t *values, uint8_t *exception_code)
{
    if (!values || quantity == 0 || quantity > KINCO_MAX_REGS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (exception_code) *exception_code = 0;

    uint8_t req[8] = {
        KINCO_SLAVE_ID,
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
    status_led_modbus_activity(STATUS_LED_MODBUS_READ);
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, KINCO_TIMEOUT_MS);
    if (ret != ESP_OK) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == KINCO_SLAVE_ID && resp[1] == (0x03 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_FAIL;
    }

    size_t expected_len = 5 + (size_t)quantity * 2;
    if (resp_len != expected_len || resp[0] != KINCO_SLAVE_ID ||
        resp[1] != 0x03 || resp[2] != quantity * 2) {
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_RESPONSE;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        values[i] = ((uint16_t)resp[3 + i * 2] << 8) | resp[4 + i * 2];
    }

    return ESP_OK;
}

static void kinco_u32_to_words(uint32_t value, uint16_t *lo_hi)
{
    /* Kinco %VD uses the lower %VW first in this project context. */
    lo_hi[0] = (uint16_t)(value & 0xFFFF);
    lo_hi[1] = (uint16_t)(value >> 16);
}

static int32_t kinco_words_to_int32(uint16_t lo, uint16_t hi)
{
    uint32_t raw = ((uint32_t)hi << 16) | lo;
    return (int32_t)raw;
}

static bool kinco_valid_axis(int axis)
{
    return axis == 0;
}

static uint16_t clamp_u16(long value, uint16_t def, uint16_t min_v, uint16_t max_v)
{
    if (value <= 0) return def;
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return (uint16_t)value;
}

static uint32_t clamp_u32(long value, uint32_t def, uint32_t min_v, uint32_t max_v)
{
    if (value <= 0) return def;
    if ((uint32_t)value < min_v) return min_v;
    if ((uint32_t)value > max_v) return max_v;
    return (uint32_t)value;
}

static esp_err_t kinco_axis_write_control(int axis, uint16_t value,
                                          uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;
    return plc_write_single_register(s_kinco_axis[axis].control, value, exception_code);
}

static esp_err_t kinco_axis_pulse_control(int axis, uint16_t value,
                                          uint8_t *exception_code)
{
    esp_err_t ret = kinco_axis_write_control(axis, value, exception_code);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(KINCO_EDGE_PULSE_MS));
    return kinco_axis_write_control(axis, KINCO_CMD_ENABLE, exception_code);
}

static esp_err_t kinco_axis_enable(int axis, bool enable, uint8_t *exception_code)
{
    return kinco_axis_write_control(axis, enable ? KINCO_CMD_ENABLE : 0x0000,
                                    exception_code);
}

static esp_err_t kinco_axis_home(int axis, int direction, uint32_t maxf,
                                 uint16_t minf, uint16_t time_ms,
                                 uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;

    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    uint16_t maxf_words[2] = {};
    kinco_u32_to_words(maxf, maxf_words);

    esp_err_t ret = plc_write_single_register(m->home_mode, 1, exception_code);
    if (ret != ESP_OK) return ret;
    ret = plc_write_single_register(m->home_dir, direction ? 1 : 0, exception_code);
    if (ret != ESP_OK) return ret;
    ret = plc_write_single_register(m->home_minf, minf, exception_code);
    if (ret != ESP_OK) return ret;
    ret = plc_write_holding_registers(m->home_maxf, maxf_words, 2, exception_code);
    if (ret != ESP_OK) return ret;
    ret = plc_write_single_register(m->home_time, time_ms, exception_code);
    if (ret != ESP_OK) return ret;

    return kinco_axis_pulse_control(axis, KINCO_CMD_START_HOME, exception_code);
}

static esp_err_t kinco_axis_pabs(int axis, int32_t target, uint32_t maxf,
                                 uint16_t minf, uint16_t time_ms,
                                 uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;

    const kinco_axis_map_t *m = &s_kinco_axis[axis];

    uint16_t cfg_words[4] = {};
    kinco_u32_to_words(maxf, &cfg_words[0]);
    cfg_words[2] = minf;
    cfg_words[3] = time_ms;

    esp_err_t ret = plc_write_holding_registers(m->pabs_maxf, cfg_words, 4,
                                                exception_code);
    if (ret != ESP_OK) return ret;

    uint16_t target_words[2] = {};
    kinco_u32_to_words((uint32_t)target, target_words);
    return plc_write_holding_registers(m->pabs_pos, target_words, 2,
                                       exception_code);
}

static esp_err_t __attribute__((unused)) kinco_axis_prel(int axis, int32_t delta,
                                                         uint32_t maxf,
                                                         uint16_t minf,
                                                         uint16_t time_ms,
                                                         uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;

    uint16_t words[6] = {};
    kinco_u32_to_words((uint32_t)delta, &words[0]);
    kinco_u32_to_words(maxf, &words[2]);
    words[4] = minf;
    words[5] = time_ms;

    esp_err_t ret = plc_write_holding_registers(s_kinco_axis[axis].prel_dist,
                                                words, 6, exception_code);
    if (ret != ESP_OK) return ret;

    return kinco_axis_pulse_control(axis, KINCO_CMD_START_PREL, exception_code);
}

static esp_err_t __attribute__((unused)) kinco_axis_jog(int axis, int32_t speed_hz,
                                                        uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;
    if (speed_hz == 0) {
        return kinco_axis_pulse_control(axis, KINCO_CMD_STOP, exception_code);
    }

    uint32_t abs_speed = (speed_hz < 0) ? (uint32_t)(-speed_hz) : (uint32_t)speed_hz;
    if (abs_speed < KINCO_MIN_FREQ) abs_speed = KINCO_MIN_FREQ;
    if (abs_speed > KINCO_MAX_FREQ) abs_speed = KINCO_MAX_FREQ;

    uint16_t words[2] = {};
    kinco_u32_to_words(abs_speed, words);
    esp_err_t ret = plc_write_holding_registers(s_kinco_axis[axis].pjog_speed,
                                                words, 2, exception_code);
    if (ret != ESP_OK) return ret;

    return kinco_axis_write_control(axis,
                                    speed_hz > 0 ? KINCO_CMD_JOG_FWD : KINCO_CMD_JOG_BWD,
                                    exception_code);
}

static esp_err_t kinco_axis_read_status(int axis, kinco_axis_status_t *status)
{
    if (!kinco_valid_axis(axis) || !status) return ESP_ERR_INVALID_ARG;

    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    uint8_t exception_code = 0;

    uint16_t command_regs[2] = {};
    esp_err_t ret = plc_read_holding_registers(m->pabs_pos, 2, command_regs,
                                               &exception_code);
    if (ret != ESP_OK) {
        status->ok = false;
        status->exception = exception_code;
        return ret;
    }
    status->command_steps = kinco_words_to_int32(command_regs[0], command_regs[1]);
    status->control_word = command_regs[0];

    uint16_t cfg_regs[4] = {};
    ret = plc_read_holding_registers(m->pabs_maxf, 4, cfg_regs, &exception_code);
    if (ret != ESP_OK) {
        status->ok = false;
        status->exception = exception_code;
        return ret;
    }
    status->maxf = ((uint32_t)cfg_regs[1] << 16) | cfg_regs[0];
    status->minf = cfg_regs[2];
    status->time_ms = cfg_regs[3];
    status->home_dir = 0;

    uint16_t state_regs[3] = {};
    ret = plc_read_holding_registers(m->status, 3, state_regs, &exception_code);
    if (ret != ESP_OK) {
        status->ok = false;
        status->exception = exception_code;
        return ret;
    }
    status->status_word = state_regs[0];
    status->status_word2 = state_regs[1];
    status->status_word3 = state_regs[2];

    uint16_t pos_regs[2] = {};
    ret = plc_read_holding_registers(m->position, 2, pos_regs, &exception_code);
    status->ok = (ret == ESP_OK);
    status->exception = exception_code;
    if (ret != ESP_OK) return ret;

    status->position = kinco_words_to_int32(pos_regs[0], pos_regs[1]);
    return ESP_OK;
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

static void update_led_from_status(const kinco_axis_status_t *st);

static void kinco_status_response(char *resp, size_t resp_sz, const char *cmd)
{
    kinco_axis_status_t st = {};
    esp_err_t ret = kinco_axis_read_status(0, &st);
    update_led_from_status(&st);
    bool ok = (ret == ESP_OK);
    bool cycle_active = (st.status_word >> 0) & 1;
    bool move_out_active = (st.status_word >> 1) & 1;
    bool wait_return_active = (st.status_word >> 2) & 1;
    bool return_active = (st.status_word >> 3) & 1;
    bool cycle_done = (st.status_word >> 4) & 1;
    bool cycle_err = (st.status_word >> 5) & 1;
    bool pabs_out_done = (st.status_word >> 6) & 1;
    bool pabs_out_err = (st.status_word >> 7) & 1;
    bool pabs_return_done = (st.status_word >> 8) & 1;
    bool pabs_return_err = (st.status_word >> 9) & 1;
    bool enable_out = (st.status_word >> 10) & 1;
    bool wait_done = (st.status_word >> 11) & 1;

    snprintf(resp, resp_sz,
             "{\"result\":\"%s\",\"cmd\":\"%s\",\"target\":\"kinco\",\"slave\":%u,"
             "\"program\":\"pabs_basico_5000\","
             "\"command_reg\":40051,\"maxf_reg\":40053,\"minf_reg\":40055,"
             "\"time_reg\":40056,\"status_reg\":40152,\"position_reg\":40101,"
             "\"error_out_reg\":40153,\"error_return_reg\":40154,"
             "\"motor\":{\"ok\":%s,\"err\":\"%s\",\"exception\":%u,"
             "\"command_steps\":%ld,\"pos\":%ld,\"maxf\":%lu,\"minf\":%u,"
             "\"time\":%u,\"status\":%u,\"status2\":%u,\"status3\":%u,"
             "\"error_out\":%u,\"error_return\":%u,"
             "\"cycle_active\":%u,\"move_out_active\":%u,"
             "\"wait_return_active\":%u,\"return_active\":%u,"
             "\"cycle_done\":%u,\"cycle_err\":%u,"
             "\"pabs_out_done\":%u,\"pabs_out_err\":%u,"
             "\"pabs_return_done\":%u,\"pabs_return_err\":%u,"
             "\"enable_out\":%u,\"wait_done\":%u}}",
             ok ? "ok" : "error", cmd, KINCO_SLAVE_ID,
             st.ok ? "true" : "false", esp_err_to_name(ret), st.exception,
             (long)st.command_steps, (long)st.position,
             (unsigned long)st.maxf, st.minf, st.time_ms,
             st.status_word, st.status_word2, st.status_word3,
             st.status_word2, st.status_word3,
             cycle_active, move_out_active, wait_return_active, return_active,
             cycle_done, cycle_err, pabs_out_done, pabs_out_err,
             pabs_return_done, pabs_return_err, enable_out, wait_done);
}

/* ── Helper: actualiza LED según estado del motor ──────────────── */
static void update_led_from_status(const kinco_axis_status_t *st)
{
    if (!st) return;
    if (!st->ok) {
        status_led_set_state(STATUS_LED_STATE_ERROR);
        return;
    }
    bool cycle_active = (st->status_word >> 0) & 1;
    bool move_out_active = (st->status_word >> 1) & 1;
    bool wait_return_active = (st->status_word >> 2) & 1;
    bool return_active = (st->status_word >> 3) & 1;
    bool cycle_err = (st->status_word >> 5) & 1;
    bool pabs_out_err = (st->status_word >> 7) & 1;
    bool pabs_return_err = (st->status_word >> 9) & 1;

    if (cycle_err || pabs_out_err || pabs_return_err || !st->ok) {
        status_led_set_state(STATUS_LED_STATE_ERROR);
    } else if (cycle_active || move_out_active || wait_return_active || return_active) {
        status_led_set_state(STATUS_LED_STATE_MOTOR_MOVING);
    } else {
        status_led_set_state(STATUS_LED_STATE_SYSTEM_READY);
    }
}

/* ================================================================
 * Ciclo automático 0→N→0 — ejecutado en tarea dedicada
 *
 * Antes corría dentro del handler HTTP y bloqueaba la única tarea del
 * servidor hasta ~120 s, dejando sin respuesta al resto de peticiones
 * (incluido el polling de estado de la UI). Ahora el endpoint solo lanza
 * la tarea y devuelve de inmediato; la UI consulta el progreso con
 * "kinco_cycle_status". El acceso RS485 ya está serializado por el mutex
 * del puente, así que las lecturas de estado concurrentes son seguras.
 * ================================================================ */

enum {
    CYCLE_PHASE_IDLE = 0,
    CYCLE_PHASE_CW,
    CYCLE_PHASE_CCW,
    CYCLE_PHASE_DONE,
};

static const char *cycle_phase_str(int phase)
{
    switch (phase) {
    case CYCLE_PHASE_CW:   return "cw";
    case CYCLE_PHASE_CCW:  return "ccw";
    case CYCLE_PHASE_DONE: return "done";
    default:               return "idle";
    }
}

typedef struct {
    bool      active;        /* tarea en curso */
    bool      done;          /* terminó (éxito o error) */
    int       phase;         /* CYCLE_PHASE_* */
    bool      cw_ok;
    bool      ccw_ok;
    int32_t   cw_target;
    uint32_t  fwd_speed;
    uint32_t  ret_speed;
    int32_t   pos_after_cw;
    int32_t   pos_final;
    esp_err_t result;
    uint8_t   exception;
} auto_cycle_state_t;

typedef struct {
    int      axis;
    int32_t  cw_target;
    uint32_t fwd_speed;
    uint32_t ret_speed;
} auto_cycle_params_t;

static auto_cycle_state_t s_auto_cycle = {};
static portMUX_TYPE s_auto_cycle_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_auto_cycle_task = nullptr;

/* Espera PabsDone (bit3) o PabsErr (bit4) con timeout. Devuelve ESP_OK al
 * completar, ESP_FAIL ante PabsErr (rellena exception), ESP_ERR_TIMEOUT si
 * vence el plazo. `final_pos` recibe la posición al completar. */
static esp_err_t auto_cycle_wait_done(int axis, int timeout_ms,
                                      int32_t *final_pos, uint8_t *exception)
{
    while (timeout_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(250));
        timeout_ms -= 250;
        kinco_axis_status_t cur = {};
        if (kinco_axis_read_status(axis, &cur) == ESP_OK) {
            if (cur.status_word & (1u << 3)) {  /* PabsDone */
                if (final_pos) *final_pos = cur.position;
                return ESP_OK;
            }
            if (cur.status_word & (1u << 4)) {  /* PabsErr */
                if (exception) *exception = cur.exception;
                return ESP_FAIL;
            }
        }
    }
    return ESP_ERR_TIMEOUT;
}

static void auto_cycle_task(void *arg)
{
    auto_cycle_params_t p = *(auto_cycle_params_t *)arg;
    free(arg);

    uint8_t exception_code = 0;
    esp_err_t r = ESP_OK;
    bool cw_ok = false, ccw_ok = false;
    int32_t pos_after_cw = 0, pos_final = 0;

    status_led_set_state(STATUS_LED_STATE_MOTOR_MOVING);

    /* Fase 1: mover a CW target */
    r = kinco_axis_pabs(p.axis, p.cw_target, p.fwd_speed, KINCO_DEFAULT_PABS_MINF,
                        KINCO_DEFAULT_PABS_TIME, &exception_code);
    if (r == ESP_OK) {
        r = auto_cycle_wait_done(p.axis, 60000, &pos_after_cw, &exception_code);
        cw_ok = (r == ESP_OK);
    }

    /* Fase 2: regresar a 0 */
    if (r == ESP_OK && cw_ok) {
        portENTER_CRITICAL(&s_auto_cycle_mux);
        s_auto_cycle.phase = CYCLE_PHASE_CCW;
        s_auto_cycle.cw_ok = true;
        s_auto_cycle.pos_after_cw = pos_after_cw;
        portEXIT_CRITICAL(&s_auto_cycle_mux);

        r = kinco_axis_pabs(p.axis, 0, p.ret_speed, KINCO_DEFAULT_PABS_MINF,
                            KINCO_DEFAULT_PABS_TIME, &exception_code);
        if (r == ESP_OK) {
            r = auto_cycle_wait_done(p.axis, 60000, &pos_final, &exception_code);
            ccw_ok = (r == ESP_OK);
        }
    }

    /* Leer estado final y actualizar el LED */
    kinco_axis_status_t st_final = {};
    kinco_axis_read_status(p.axis, &st_final);
    update_led_from_status(&st_final);

    portENTER_CRITICAL(&s_auto_cycle_mux);
    s_auto_cycle.active = false;
    s_auto_cycle.done = true;
    s_auto_cycle.phase = CYCLE_PHASE_DONE;
    s_auto_cycle.cw_ok = cw_ok;
    s_auto_cycle.ccw_ok = ccw_ok;
    s_auto_cycle.pos_after_cw = pos_after_cw;
    s_auto_cycle.pos_final = pos_final;
    s_auto_cycle.result = r;
    s_auto_cycle.exception = exception_code;
    portEXIT_CRITICAL(&s_auto_cycle_mux);

    s_auto_cycle_task = nullptr;
    vTaskDelete(nullptr);
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
    long axis_value = 0;
    json_find_int(json, "axis", &axis_value);
    int axis = kinco_valid_axis((int)axis_value) ? (int)axis_value : 0;

    long speed_value = 0;
    bool has_speed = json_find_int(json, "speed", &speed_value);
    long minf_value = 0;
    bool has_minf = json_find_int(json, "minf", &minf_value);
    long time_value = 0;
    bool has_time = json_find_int(json, "time", &time_value);
    long dir_value = 0;
    bool has_dir = json_find_int(json, "dir", &dir_value);

    uint32_t maxf = clamp_u32(has_speed ? speed_value : KINCO_DEFAULT_PABS_MAXF,
                              KINCO_DEFAULT_PABS_MAXF, KINCO_MIN_FREQ, KINCO_MAX_FREQ);
    uint16_t minf = clamp_u16(has_minf ? minf_value : KINCO_DEFAULT_PABS_MINF,
                              KINCO_DEFAULT_PABS_MINF, KINCO_MIN_FREQ, 65535);
    uint16_t time_ms = clamp_u16(has_time ? time_value : KINCO_DEFAULT_PABS_TIME,
                                 KINCO_DEFAULT_PABS_TIME, 1, 65535);

    if (strcmp(cmd, "kinco_status") == 0) {
        kinco_status_response(resp, resp_sz, cmd);
        return;
    }

    if (strcmp(cmd, "rs485_mode") == 0) {
        esp_err_t r = bridge_rs485_set_mode(BRIDGE_MODE_MODBUS_TCP_RS485);
        snprintf(resp, resp_sz, "{\"result\":\"%s\",\"cmd\":\"%s\",\"bridge\":\"modbus_tcp_rs485\"}",
                 r == ESP_OK ? "ok" : "error", cmd);
        return;
    }

    if (strcmp(cmd, "local_mode") == 0) {
        esp_err_t r = bridge_rs485_set_mode(BRIDGE_MODE_LOCAL_ONLY);
        snprintf(resp, resp_sz, "{\"result\":\"%s\",\"cmd\":\"%s\",\"bridge\":\"local_only\"}",
                 r == ESP_OK ? "ok" : "error", cmd);
        return;
    }

    if (strcmp(cmd, "kinco_cycle_status") == 0) {
        kinco_status_response(resp, resp_sz, cmd);
        return;
    }

    if (strcmp(cmd, "kinco_pabs") == 0 ||
        strcmp(cmd, "kinco_start_steps") == 0 ||
        strcmp(cmd, "kinco_move_delta") == 0 ||
        strcmp(cmd, "kinco_auto_cycle") == 0) {
        int32_t target_steps = has_arg ? (int32_t)arg : KINCO_TEST_STEPS;
        if (target_steps == 0) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"los pasos no pueden ser 0\"}",
                     cmd);
            return;
        }

        uint8_t exception_code = 0;
        esp_err_t r = kinco_axis_pabs(axis, target_steps, maxf, minf, time_ms,
                                      &exception_code);
        if (r != ESP_OK) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"target\":\"kinco\","
                     "\"axis\":%d,\"slave\":%u,\"exception\":%u,\"err\":\"%s\","
                     "\"requested_steps\":%ld}",
                     cmd, axis, KINCO_SLAVE_ID, exception_code, esp_err_to_name(r),
                     (long)target_steps);
            return;
        }

        kinco_status_response(resp, resp_sz, cmd);
        return;
    }

    if (strcmp(cmd, "kinco_enable") == 0 ||
        strcmp(cmd, "kinco_set_dir") == 0 ||
        strcmp(cmd, "kinco_home") == 0 ||
        strcmp(cmd, "kinco_stop") == 0 ||
        strcmp(cmd, "kinco_reset_pos") == 0 ||
        strcmp(cmd, "kinco_reset_status") == 0) {
        snprintf(resp, resp_sz,
                 "{\"result\":\"error\",\"cmd\":\"%s\","
                 "\"msg\":\"comando no usado por pabs_basico_5000\"}",
                 cmd);
        return;
    }

    esp_err_t r = ESP_OK;
    uint8_t exception_code = 0;
    int32_t target_pos = 0;
    bool has_target_pos = false;

    if (strcmp(cmd, "kinco_enable") == 0) {
        r = kinco_axis_enable(axis, has_arg ? (arg != 0) : true, &exception_code);
    } else if (strcmp(cmd, "kinco_set_dir") == 0) {
        long dir = has_arg ? arg : dir_value;
        r = plc_write_single_register(s_kinco_axis[axis].home_dir,
                                      dir ? 1 : 0, &exception_code);
    } else if (strcmp(cmd, "kinco_home") == 0) {
        uint32_t home_maxf = clamp_u32(has_speed ? speed_value : KINCO_DEFAULT_HOME_MAXF,
                                       KINCO_DEFAULT_HOME_MAXF,
                                       KINCO_MIN_FREQ, KINCO_MAX_FREQ);
        uint16_t home_minf = clamp_u16(has_minf ? minf_value : KINCO_DEFAULT_HOME_MINF,
                                       KINCO_DEFAULT_HOME_MINF,
                                       KINCO_MIN_FREQ, 65535);
        uint16_t home_time = clamp_u16(has_time ? time_value : KINCO_DEFAULT_HOME_TIME,
                                       KINCO_DEFAULT_HOME_TIME, 1, 65535);
        r = kinco_axis_home(axis, has_dir ? (int)dir_value : 0,
                            home_maxf, home_minf, home_time, &exception_code);
        maxf = home_maxf;
        minf = home_minf;
        time_ms = home_time;
    } else if (strcmp(cmd, "kinco_pabs") == 0) {
        target_pos = (int32_t)arg;
        has_target_pos = true;
        r = kinco_axis_pabs(axis, target_pos, maxf, minf, time_ms, &exception_code);
    } else if (strcmp(cmd, "kinco_move_delta") == 0) {
        kinco_axis_status_t cur = {};
        esp_err_t sr = kinco_axis_read_status(axis, &cur);
        if (sr != ESP_OK) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"target\":\"kinco\","
                     "\"axis\":%d,\"slave\":%u,\"status_read\":\"error\","
                     "\"status_err\":\"%s\",\"exception\":%u}",
                     cmd, axis, KINCO_SLAVE_ID, esp_err_to_name(sr), cur.exception);
            return;
        }
        int64_t next = (int64_t)cur.position + (has_arg ? arg : KINCO_TEST_STEPS);
        if (next < (-2147483647LL - 1) || next > 2147483647LL) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"target fuera de rango\","
                     "\"pos\":%ld,\"delta\":%ld}",
                     cmd, (long)cur.position, has_arg ? arg : KINCO_TEST_STEPS);
            return;
        }
        target_pos = (int32_t)next;
        has_target_pos = true;
        r = kinco_axis_pabs(axis, target_pos, maxf, minf, time_ms, &exception_code);
    } else if (strcmp(cmd, "kinco_stop") == 0) {
        r = kinco_axis_pulse_control(axis, KINCO_CMD_STOP, &exception_code);
    } else if (strcmp(cmd, "kinco_reset_pos") == 0) {
        r = kinco_axis_pulse_control(axis, KINCO_CMD_RESET_POS, &exception_code);
    } else if (strcmp(cmd, "kinco_reset_status") == 0) {
        r = kinco_axis_pulse_control(axis, KINCO_CMD_RESET_STATUS, &exception_code);
    } else if (strcmp(cmd, "kinco_cycle_status") == 0) {
        /* Snapshot del ciclo automático + lectura de estado para el contador
         * en vivo. La UI hace polling de este comando mientras el ciclo corre. */
        auto_cycle_state_t snap;
        portENTER_CRITICAL(&s_auto_cycle_mux);
        snap = s_auto_cycle;
        portEXIT_CRITICAL(&s_auto_cycle_mux);

        kinco_axis_status_t st = {};
        esp_err_t sr = kinco_axis_read_status(0, &st);
        update_led_from_status(&st);

        snprintf(resp, resp_sz,
                 "{\"result\":\"%s\",\"cmd\":\"%s\",\"pos\":%ld,"
                 "\"motor\":{\"ok\":%s,\"err\":\"%s\",\"pos\":%ld,\"control\":%u,"
                 "\"home_dir\":%u,\"status\":%u,\"status2\":%u,\"status3\":%u,"
                 "\"enable\":%u,\"home_ok\":%u,\"home_done\":%u,\"home_err\":%u,"
                 "\"pabs_done\":%u,\"pabs_err\":%u,\"pto0\":%u,"
                 "\"homing_active\":%u,\"pabs_active\":%u,"
                 "\"home_sensor\":%u,\"system_ready\":%u},"
                 "\"cycle\":{\"active\":%s,\"done\":%s,\"phase\":\"%s\","
                 "\"cw_ok\":%s,\"ccw_ok\":%s,\"result\":\"%s\",\"exception\":%u,"
                 "\"cw_target\":%ld,\"fwd_speed\":%lu,\"ret_speed\":%lu,"
                 "\"pos_after_cw\":%ld,\"pos_final\":%ld}}",
                 sr == ESP_OK ? "ok" : "error", cmd, (long)st.position,
                 st.ok ? "true" : "false", esp_err_to_name(sr), (long)st.position,
                 st.control_word, st.home_dir,
                 st.status_word, st.status_word2, st.status_word3,
                 (st.control_word >> 0) & 1,
                 (st.status_word >> 0) & 1, (st.status_word >> 1) & 1,
                 (st.status_word >> 2) & 1, (st.status_word >> 3) & 1,
                 (st.status_word >> 4) & 1, (st.status_word >> 5) & 1,
                 (st.status_word >> 6) & 1, (st.status_word >> 7) & 1,
                 (st.status_word >> 8) & 1, (st.status_word >> 9) & 1,
                 snap.active ? "true" : "false", snap.done ? "true" : "false",
                 cycle_phase_str(snap.phase),
                 snap.cw_ok ? "true" : "false", snap.ccw_ok ? "true" : "false",
                 esp_err_to_name(snap.result), snap.exception,
                 (long)snap.cw_target, (unsigned long)snap.fwd_speed,
                 (unsigned long)snap.ret_speed,
                 (long)snap.pos_after_cw, (long)snap.pos_final);
        return;
    } else if (strcmp(cmd, "kinco_auto_cycle") == 0) {
        /* Ciclo automatico: N steps CW a velocidad forward, regreso a 0 CCW a
         * velocidad return. Se ejecuta en una tarea dedicada (auto_cycle_task)
         * para no bloquear el servidor HTTP; este endpoint solo la lanza.
         * Parametros JSON opcionales:
         *   "arg"   -> target CW steps (default 15000)
         *   "speed" -> forward speed Hz (default 5000)
         *   "minf"  -> return speed Hz (default 2500)
         */
        int32_t cw_target = has_arg ? (int32_t)arg : 15000;
        uint32_t fwd_speed = clamp_u32(has_speed ? speed_value : 5000,
                                       5000, KINCO_MIN_FREQ, KINCO_MAX_FREQ);
        long ret_speed_val = 0;
        bool has_ret_speed = json_find_int(json, "minf", &ret_speed_val);
        uint32_t ret_speed = clamp_u32(has_ret_speed ? ret_speed_val : 2500,
                                       2500, KINCO_MIN_FREQ, KINCO_MAX_FREQ);

        /* Reservar el ciclo de forma atómica: si ya hay uno activo, rechazar. */
        bool busy;
        portENTER_CRITICAL(&s_auto_cycle_mux);
        busy = s_auto_cycle.active;
        if (!busy) {
            s_auto_cycle = auto_cycle_state_t{};
            s_auto_cycle.active = true;
            s_auto_cycle.phase = CYCLE_PHASE_CW;
            s_auto_cycle.cw_target = cw_target;
            s_auto_cycle.fwd_speed = fwd_speed;
            s_auto_cycle.ret_speed = ret_speed;
        }
        portEXIT_CRITICAL(&s_auto_cycle_mux);

        if (busy) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"ciclo en curso\","
                     "\"running\":true}", cmd);
            return;
        }

        auto_cycle_params_t *params =
            (auto_cycle_params_t *)malloc(sizeof(auto_cycle_params_t));
        BaseType_t created = pdFAIL;
        if (params) {
            params->axis = axis;
            params->cw_target = cw_target;
            params->fwd_speed = fwd_speed;
            params->ret_speed = ret_speed;
            created = xTaskCreate(auto_cycle_task, "auto_cycle", 4096, params, 5,
                                  &s_auto_cycle_task);
        }

        if (created != pdPASS) {
            free(params);
            portENTER_CRITICAL(&s_auto_cycle_mux);
            s_auto_cycle.active = false;
            portEXIT_CRITICAL(&s_auto_cycle_mux);
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"no se pudo lanzar el ciclo\"}",
                     cmd);
            return;
        }

        snprintf(resp, resp_sz,
                 "{\"result\":\"ok\",\"cmd\":\"%s\",\"target\":\"kinco\",\"axis\":%d,"
                 "\"running\":true,\"cw_target\":%ld,\"fwd_speed\":%lu,\"ret_speed\":%lu}",
                 cmd, axis, (long)cw_target, (unsigned long)fwd_speed,
                 (unsigned long)ret_speed);
        return;
    } else {
        snprintf(resp, resp_sz,
                 "{\"result\":\"error\",\"msg\":\"cmd desconocido\",\"cmd\":\"%s\"}",
                 cmd);
        return;
    }

    kinco_axis_status_t st = {};
    esp_err_t sr = kinco_axis_read_status(axis, &st);
    update_led_from_status(&st);
    snprintf(resp, resp_sz,
             "{\"result\":\"%s\",\"cmd\":\"%s\",\"target\":\"kinco\","
             "\"axis\":%d,\"slave\":%u,\"exception\":%u,\"err\":\"%s\","
             "\"status_read\":\"%s\",\"status_err\":\"%s\","
             "\"control\":%u,\"home_dir\":%u,"
             "\"status\":%u,\"status2\":%u,\"status3\":%u,"
             "\"pos\":%ld,\"target_pos\":%ld,"
             "\"enable\":%u,\"reset_pos\":%u,\"start_pabs\":%u,\"start_home\":%u,"
             "\"reset_status\":%u,\"stop\":%u,"
             "\"home_ok\":%u,\"home_done\":%u,\"home_err\":%u,"
             "\"pabs_done\":%u,\"pabs_err\":%u,\"pto0\":%u,"
             "\"homing_active\":%u,\"pabs_active\":%u,"
             "\"home_sensor\":%u,\"system_ready\":%u,"
             "\"maxf\":%lu,\"minf\":%u,\"time\":%u}",
             r == ESP_OK ? "ok" : "error", cmd, axis, KINCO_SLAVE_ID,
             exception_code, esp_err_to_name(r),
             sr == ESP_OK ? "ok" : "error", esp_err_to_name(sr),
             st.control_word, st.home_dir,
             st.status_word, st.status_word2, st.status_word3,
             (long)st.position, has_target_pos ? (long)target_pos : (long)st.position,
             (st.control_word >> 0) & 1, (st.control_word >> 1) & 1,
             (st.control_word >> 2) & 1, (st.control_word >> 3) & 1,
             (st.control_word >> 4) & 1, (st.control_word >> 5) & 1,
             (st.status_word >> 0) & 1, (st.status_word >> 1) & 1,
             (st.status_word >> 2) & 1, (st.status_word >> 3) & 1,
             (st.status_word >> 4) & 1, (st.status_word >> 5) & 1,
             (st.status_word >> 6) & 1, (st.status_word >> 7) & 1,
             (st.status_word >> 8) & 1, (st.status_word >> 9) & 1,
             (unsigned long)maxf, minf, time_ms);
}

static esp_err_t http_get_status_handler(httpd_req_t *req)
{
    char resp[1536];
    kinco_status_response(resp, sizeof(resp), "kinco_status");
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

    char resp[2048];
    dispatch_command(buf, resp, sizeof(resp));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t __attribute__((unused)) http_get_root_handler(httpd_req_t *req)
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

static esp_err_t http_get_kinco_root_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Kinco PABS 5000</title>"
        "<style>"
        ":root{color-scheme:dark}*{box-sizing:border-box}"
        "body{font-family:Arial,sans-serif;margin:0;background:#111827;color:#e5e7eb}"
        "main{max-width:820px;margin:0 auto;padding:14px}"
        "h1{font-size:22px;margin:0 0 5px;color:#f9fafb}"
        ".sub{color:#9ca3af;font-size:13px;margin:0 0 12px}"
        "section{border:1px solid #374151;border-radius:8px;padding:12px;margin:10px 0;background:#1f2937}"
        ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
        "button{background:#2563eb;color:#fff;border:0;border-radius:6px;padding:11px 14px;cursor:pointer;min-height:40px;font-weight:700}"
        "button:hover{background:#1d4ed8}button:disabled{background:#4b5563;cursor:not-allowed}"
        ".move{font-size:17px;min-width:165px}.danger{background:#dc2626}"
        ".ok{color:#34d399}.bad{color:#f87171}.warn{color:#fbbf24}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px}"
        ".kv{display:grid;grid-template-columns:1fr auto;gap:5px 12px;font-size:13px}"
        ".kv span:nth-child(odd){color:#9ca3af}.mono{font-family:Consolas,monospace}"
        "label{color:#9ca3af;font-size:13px;display:flex;align-items:center;gap:5px}"
        "input{background:#111827;color:#e5e7eb;border:1px solid #4b5563;border-radius:6px;padding:8px 9px;width:92px;font-size:14px;text-align:center}"
        ".counter{background:#111827;border:2px solid #374151;border-radius:8px;padding:14px;text-align:center}"
        ".counter .big{font:700 52px/1 Consolas,monospace;color:#34d399}"
        ".counter .label{font-size:11px;color:#6b7280;text-transform:uppercase;letter-spacing:1px;margin-bottom:5px}"
        ".status{font-family:Consolas,monospace;white-space:pre-wrap;overflow:auto;max-height:210px;background:#111827;border-radius:6px;padding:8px}"
        ".muted{color:#9ca3af;font-size:12px}"
        "</style></head><body><main>"
        "<h1>Kinco PABS 5000</h1>"
        "<p class='sub'>PLC pabs_basico_5000: escribir pasos en 40051, la PLC habilita, mueve, espera 3 s, vuelve a cero y deshabilita.</p>"
        "<div class='counter'><div class='label'>Posicion actual 40101</div><div class='big' id='pos'>0</div>"
        "<div class='muted' id='phase'>Sin lectura</div></div>"
        "<section><div class='row'>"
        "<button class='move' onclick='startSteps(5000)'>+5000 y volver a 0</button>"
        "<button class='move' onclick='startSteps(-5000)'>-5000 y volver a 0</button>"
        "<button onclick='readStates(true)'>Leer estados</button>"
        "</div></section>"
        "<section><div class='row'>"
        "<label>Pasos<input id='steps' type='number' value='5000' min='1' max='999999' step='1000'></label>"
        "<label>Max Hz<input id='maxf' type='number' value='2000' min='125' max='200000' step='100'></label>"
        "<label>Min Hz<input id='minf' type='number' value='300' min='125' max='65535' step='25'></label>"
        "<label>Accel<input id='time' type='number' value='300' min='1' max='65535' step='50'></label>"
        "<button onclick='startCustom(1)'>Enviar +pasos</button>"
        "<button onclick='startCustom(-1)'>Enviar -pasos</button>"
        "</div><p class='muted'>Registros: pasos 40051-40052, max 40053-40054, min 40055, accel 40056.</p></section>"
        "<section><div class='grid'>"
        "<div><strong>Registros</strong><div class='kv' id='regs'>Sin lectura</div></div>"
        "<div><strong>Bits 40152</strong><div class='kv' id='bits'>Sin lectura</div></div>"
        "</div></section>"
        "<section><strong>Respuesta</strong><pre class='status' id='log'>Listo</pre><div class='muted'>UI pabs_basico_5000 v1</div></section>"
        "</main><script>"
        "let running=false,pollId=null,pollTicks=0,targetAbs=0;"
        "function q(id){return document.getElementById(id)}"
        "function hx(v){return '0x'+(v||0).toString(16).padStart(4,'0')}"
        "function cls(v){return v?'ok':'bad'}"
        "function num(id,def){let v=parseInt(q(id).value);return Number.isFinite(v)?v:def}"
        "function cfg(){return {speed:num('maxf',2000),minf:num('minf',300),time:num('time',300)}}"
        "async function api(b){let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});let j=await r.json();q('log').textContent=JSON.stringify(j,null,2);if(j.motor)paint(j.motor);return j}"
        "function active(m){return !!(m&&(m.cycle_active||m.move_out_active||m.wait_return_active||m.return_active))}"
        "function failed(m){return !!(m&&(m.cycle_err||m.pabs_out_err||m.pabs_return_err))}"
        "function phase(m){if(!m)return 'Sin lectura';if(failed(m))return 'Error';if(m.move_out_active)return 'Moviendo a destino';if(m.wait_return_active)return 'Esperando 3 s';if(m.return_active)return 'Volviendo a cero';if(m.cycle_active)return 'Ciclo activo';if(m.cycle_done)return 'Terminado';return 'Idle'}"
        "function paint(m){q('pos').textContent=(m.pos||0).toLocaleString();q('phase').textContent=phase(m);"
        "q('regs').innerHTML='<span>Link</span><span class='+cls(m.ok)+'>'+(m.ok?'OK':'ERR '+m.err)+'</span>'"
        "+'<span>Comando 40051</span><span class=mono>'+m.command_steps+'</span>'"
        "+'<span>Max 40053</span><span class=mono>'+m.maxf+'</span>'"
        "+'<span>Min 40055</span><span class=mono>'+m.minf+'</span>'"
        "+'<span>Accel 40056</span><span class=mono>'+m.time+'</span>'"
        "+'<span>Estado 40152</span><span class=mono>'+hx(m.status)+'</span>'"
        "+'<span>Error ida 40153</span><span class=mono>'+hx(m.error_out)+'</span>'"
        "+'<span>Error vuelta 40154</span><span class=mono>'+hx(m.error_return)+'</span>'"
        "+'<span>Enable Q0.3</span><span class='+cls(m.enable_out)+'>'+m.enable_out+'</span>';"
        "q('bits').innerHTML='<span>b0 CycleActive</span><span>'+m.cycle_active+'</span>'"
        "+'<span>b1 MoveOut</span><span>'+m.move_out_active+'</span>'"
        "+'<span>b2 WaitReturn</span><span>'+m.wait_return_active+'</span>'"
        "+'<span>b3 Return</span><span>'+m.return_active+'</span>'"
        "+'<span>b4 Done</span><span class='+cls(m.cycle_done)+'>'+m.cycle_done+'</span>'"
        "+'<span>b5 CycleErr</span><span class='+cls(!m.cycle_err)+'>'+m.cycle_err+'</span>'"
        "+'<span>b6 OutDone</span><span>'+m.pabs_out_done+'</span>'"
        "+'<span>b7 OutErr</span><span class='+cls(!m.pabs_out_err)+'>'+m.pabs_out_err+'</span>'"
        "+'<span>b8 RetDone</span><span>'+m.pabs_return_done+'</span>'"
        "+'<span>b9 RetErr</span><span class='+cls(!m.pabs_return_err)+'>'+m.pabs_return_err+'</span>'"
        "+'<span>b10 EnableOut</span><span>'+m.enable_out+'</span>'"
        "+'<span>b11 WaitDone</span><span>'+m.wait_done+'</span>';}"
        "async function readStates(show){let j=await api({cmd:'kinco_status'});return j}"
        "function startPoll(){if(pollId)clearInterval(pollId);pollTicks=0;pollId=setInterval(async()=>{pollTicks++;try{let j=await api({cmd:'kinco_cycle_status'});let m=j.motor;if(m&&(failed(m)||(!active(m)&&m.cycle_done&&pollTicks>2))){clearInterval(pollId);pollId=null;running=false;}}catch(e){}},500)}"
        "async function startSteps(steps){if(running)return;let c=cfg();targetAbs=Math.abs(steps);running=true;q('phase').textContent='Enviando '+steps+' pasos';let j=await api({cmd:'kinco_pabs',axis:0,arg:steps,speed:c.speed,minf:c.minf,time:c.time});if(j.result!=='ok'){running=false;return}startPoll()}"
        "function startCustom(sign){let s=Math.abs(num('steps',5000));startSteps(sign*s)}"
        "setInterval(()=>{if(!running)readStates(false)},2000);readStates(false);"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t __attribute__((unused)) http_get_kinco_root_handler_old(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Kinco Motor 0</title>"
        "<style>"
        ":root{color-scheme:dark}*{box-sizing:border-box}"
        "body{font-family:Arial,sans-serif;margin:0;background:#0f172a;color:#e5e7eb}"
        "main{max-width:760px;margin:0 auto;padding:16px}"
        "h1{font-size:22px;margin:0 0 6px;color:#f8fafc}"
        ".sub{color:#94a3b8;font-size:13px;margin:0 0 14px}"
        "section{border:1px solid #334155;border-radius:8px;padding:12px;margin:10px 0;background:#1e293b}"
        ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
        "button{background:#2563eb;color:#fff;border:0;border-radius:6px;padding:11px 14px;cursor:pointer;min-height:40px;font-weight:600}"
        "button:hover{background:#1d4ed8}.danger{background:#dc2626}.danger:hover{background:#b91c1c}"
        ".toggle{background:#475569}.toggle.on{background:#16a34a}.toggle.off{background:#64748b}"
        ".move{font-size:17px;min-width:160px}.ok{color:#34d399}.bad{color:#f87171}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:8px}"
        ".kv{display:grid;grid-template-columns:1fr auto;gap:5px 12px;font-size:13px}"
        ".kv span:nth-child(odd){color:#94a3b8}.mono{font-family:Consolas,monospace}"
        ".status{font-family:Consolas,monospace;white-space:pre-wrap;overflow:auto;max-height:190px;background:#0f172a;border-radius:6px;padding:8px}"
        ".muted{color:#94a3b8;font-size:12px}.card{background:#0f172a;border:1px solid #334155;border-radius:8px;padding:10px}"
        ".counter-box{background:linear-gradient(135deg,#1e293b,#0f172a);border:2px solid #334155;border-radius:12px;padding:16px 20px;text-align:center;margin:8px 0}"
        ".counter-box .big{font-size:52px;font-weight:700;font-family:Consolas,monospace;color:#34d399;letter-spacing:2px;line-height:1.1}"
        ".counter-box .label{font-size:11px;color:#64748b;text-transform:uppercase;letter-spacing:2px;margin-bottom:4px}"
        ".counter-box .target-line{font-size:13px;color:#94a3b8;margin-top:4px}"
        ".counter-box .progress-bar{height:6px;background:#1e293b;border-radius:3px;margin-top:8px;overflow:hidden}"
        ".counter-box .progress-fill{height:100%;background:linear-gradient(90deg,#16a34a,#34d399);border-radius:3px;width:0%;transition:width .3s}"
        ".counter-box.pulse{animation:ctr-pulse .6s ease-in-out}"
        "@keyframes ctr-pulse{0%,100%{border-color:#334155}50%{border-color:#34d399}}"
        "@media(max-width:640px){main{padding:10px}.row button{flex:1 1 140px}.move{min-width:0}}"
        "</style></head><body><main><h1>Kinco Motor 0</h1>"
        "<p class='sub'>Prueba MODBUS directa a PLC: control 40070, estado 40152, posicion 40101.</p>"
        "<p class='sub'>LED ESP32: violeta=lectura MODBUS, ambar=escritura MODBUS, rojo=error MODBUS.</p>"
        "<div id='counterBox' class='counter-box'>"
        "<div class='label'>Contador de pasos — Posicion actual</div>"
        "<div class='big' id='counterValue'>0</div>"
        "<div id='counterTarget' class='target-line' style='display:none'>Objetivo: <strong id='counterTargetVal'>0</strong></div>"
        "<div class='progress-bar'><div id='counterProgress' class='progress-fill' style='width:0%'></div></div>"
        "</div>"
        "<section><div class='row'>"
        "<button id='enableBtn' class='toggle off' onclick='toggleEnable()'>Enable: OFF</button>"
        "<button id='dirBtn' class='toggle off' onclick='toggleDir()'>Direccion HOME: Forward</button>"
        "<button onclick='home()'>HOME</button>"
        "<button class='danger' onclick='cmd(\"kinco_stop\",0)'>STOP</button>"
        "</div></section>"
        "<section><div class='row'>"
        "<button class='move' onclick='move(5000)'>Adelantar 5000</button>"
        "<button class='move' onclick='move(-5000)'>Retroceder 5000</button>"
        "</div><p class='muted'>Cada movimiento lee la posicion actual de la PLC y ordena un PABS relativo de 5000 pasos.</p></section>"
        "<section><div class='row' style='align-items:center'>"
        "<button id='autoCycleBtn' class='move' onclick='autoCycle()' style='background:#16a34a;font-size:18px'>"
        "🔄 Auto Cycle 0→N→0</button>"
        "<label style='color:#94a3b8;font-size:13px;display:flex;align-items:center;gap:4px'>"
        "Steps<input id='cycleSteps' type='number' value='15000' min='1' max='999999' step='1000'"
        "style='background:#0f172a;color:#e5e7eb;border:1px solid #334155;border-radius:6px;padding:8px 10px;width:90px;font-size:14px;text-align:center'></label>"
        "<label style='color:#94a3b8;font-size:13px;display:flex;align-items:center;gap:4px'>"
        "CW Hz<input id='cycleFwdHz' type='number' value='5000' min='125' max='200000' step='500'"
        "style='background:#0f172a;color:#e5e7eb;border:1px solid #334155;border-radius:6px;padding:8px 10px;width:82px;font-size:14px;text-align:center'></label>"
        "<label style='color:#94a3b8;font-size:13px;display:flex;align-items:center;gap:4px'>"
        "CCW Hz<input id='cycleCcwHz' type='number' value='2500' min='125' max='200000' step='500'"
        "style='background:#0f172a;color:#e5e7eb;border:1px solid #334155;border-radius:6px;padding:8px 10px;width:82px;font-size:14px;text-align:center'></label>"
        "</div><p class='muted'>Ciclo automatico: N steps CW, regresa a 0 CCW. "
        "Configurable: pasos, velocidad ida (CW) y velocidad vuelta (CCW). Boton se deshabilita durante la ejecucion (~10-30 s).</p></section>"
        "<section><div class='row'>"
        "<button onclick='cmd(\"kinco_reset_pos\",0)'>Reset Pos</button>"
        "<button onclick='cmd(\"kinco_reset_status\",0)'>Reset Estados</button>"
        "<button onclick='readStates()'>Leer estados PLC</button>"
        "</div></section>"
        "<section><div class='grid'>"
        "<div class='card'><strong>Motor</strong><div class='kv' id='motor'>Sin lectura</div></div>"
        "<div class='card'><strong>Bits de estado %VW302</strong><div class='kv' id='bits'>Sin lectura</div></div>"
        "</div></section>"
        "<section><strong>Respuesta</strong><pre class='status' id='log'>Listo</pre><div class='muted'>UI prueba un motor 1.1</div></section>"
        "</main><script>"
        "let enable=false,dir=0,cycleTarget=0,cycleRunning=false,fastPollId=null;"
        "let counterEl=document.getElementById('counterValue');"
        "let counterBox=document.getElementById('counterBox');"
        "let counterTargetEl=document.getElementById('counterTarget');"
        "let counterTargetVal=document.getElementById('counterTargetVal');"
        "let counterProgress=document.getElementById('counterProgress');"
        "function updateCounter(pos,target){"
        "if(!counterEl)return;"
        "let prev=counterEl.textContent;"
        "counterEl.textContent=pos.toLocaleString();"
        "if(prev!==counterEl.textContent){counterBox.classList.remove('pulse');void counterBox.offsetWidth;counterBox.classList.add('pulse')}"
        "if(target>0){"
        "counterTargetEl.style.display='block';counterTargetVal.textContent=target.toLocaleString();"
        "let pct=target?Math.min(100,Math.round(Math.abs(pos)/target*100)):0;"
        "counterProgress.style.width=pct+'%'}"
        "else{counterTargetEl.style.display='none';counterProgress.style.width='0%'}"
        "}"
        "function hx(v){return '0x'+(v||0).toString(16).padStart(4,'0')}"
        "function cls(v){return v?'ok':'bad'}"
        "async function api(b){let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});let j=await r.json();document.getElementById('log').textContent=JSON.stringify(j,null,2);if(j.motor)paint(j.motor);return j}"
        "async function cmd(c,a){let b={cmd:c,axis:0,speed:2000,minf:300,time:300,dir:dir};if(a!==undefined)b.arg=a;let j=await api(b);readStates(false);return j}"
        "async function toggleEnable(){enable=!enable;await cmd('kinco_enable',enable?1:0)}"
        "async function toggleDir(){dir=dir?0:1;updateDir();await cmd('kinco_set_dir',dir)}"
        "function updateDir(){let b=document.getElementById('dirBtn');b.textContent='Direccion HOME: '+(dir?'Backward':'Forward');b.className='toggle '+(dir?'on':'off')}"
        "function home(){cmd('kinco_home',0)}"
        "function move(delta){cmd('kinco_move_delta',delta)}"
        "async function autoCycle(){"
        "let btn=document.getElementById('autoCycleBtn');"
        "let log=document.getElementById('log');"
        "let steps=parseInt(document.getElementById('cycleSteps').value)||15000;"
        "let fwdHz=parseInt(document.getElementById('cycleFwdHz').value)||5000;"
        "let ccwHz=parseInt(document.getElementById('cycleCcwHz').value)||2500;"
        "cycleTarget=steps;cycleRunning=true;updateCounter(0,steps);"
        "btn.disabled=true;btn.textContent='⏳ Ciclo en curso...';btn.style.background='#16a34a';"
        "log.textContent='🚀 Iniciando: 0 → '+steps+' CW @'+fwdHz+'Hz / CCW @'+ccwHz+'Hz ...';"
        /* Lanzar el ciclo (responde de inmediato). Si falla, abortar. */
        "try{let b={cmd:'kinco_auto_cycle',axis:0,arg:steps,speed:fwdHz,minf:ccwHz,time:300,dir:dir};"
        "let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
        "let j=await r.json();if(j.result!=='ok'){throw new Error(j.msg||j.err||'no se pudo iniciar')}}"
        "catch(e){btn.style.background='#dc2626';log.textContent='❌ '+e.message;"
        "btn.disabled=false;btn.textContent='🔄 Auto Cycle 0→N→0';cycleRunning=false;cycleTarget=0;return}"
        /* Sondear progreso sin bloquear; finalizar cuando cycle.active=false. */
        "fastPollId=setInterval(async()=>{"
        "try{let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({cmd:'kinco_cycle_status'})});"
        "let j=await r.json();if(j.motor){paint(j.motor);updateCounter(j.motor.pos,cycleTarget)}"
        "if(j.cycle&&!j.cycle.active){"
        "clearInterval(fastPollId);fastPollId=null;cycleRunning=false;cycleTarget=0;"
        "btn.disabled=false;btn.textContent='🔄 Auto Cycle 0→N→0';"
        "btn.style.background=(j.cycle.result==='ESP_OK')?'#16a34a':'#dc2626';"
        "log.textContent=JSON.stringify(j,null,2);"
        "let s=await api({cmd:'kinco_status'});if(s.motor){paint(s.motor);updateCounter(s.motor.pos,0)}}}"
        "catch(e){}"
        "},500);"
        "}"
        "async function readStates(show=true){let j=await api({cmd:'kinco_status'});if(show&&j.motor)paint(j.motor);return j}"
        "function paint(m){if(!m)return;enable=!!m.enable;dir=m.home_dir?1:0;let eb=document.getElementById('enableBtn');eb.textContent='Enable: '+(enable?'ON':'OFF');eb.className='toggle '+(enable?'on':'off');updateDir();"
        "updateCounter(m.pos,cycleTarget||0);"
        "document.getElementById('motor').innerHTML='<span>Link</span><span class='+cls(m.ok)+'>'+(m.ok?'OK':'ERR '+m.err)+'</span><span>Posicion</span><span class=mono>'+m.pos+'</span><span>Control 40070</span><span class=mono>'+hx(m.control)+'</span><span>Estado 40152</span><span class=mono>'+hx(m.status)+'</span><span>Estado2 40153</span><span class=mono>'+hx(m.status2)+'</span><span>Estado3 40154</span><span class=mono>'+hx(m.status3)+'</span>';"
        "document.getElementById('bits').innerHTML='<span>HomeOK b0</span><span class='+cls(m.home_ok)+'>'+m.home_ok+'</span><span>HomeDone b1</span><span>'+m.home_done+'</span><span>HomeErr b2</span><span class='+cls(!m.home_err)+'>'+m.home_err+'</span><span>PabsDone b3</span><span>'+m.pabs_done+'</span><span>PabsErr b4</span><span class='+cls(!m.pabs_err)+'>'+m.pabs_err+'</span><span>PTO0 b5</span><span>'+m.pto0+'</span><span>HomingAct b6</span><span>'+m.homing_active+'</span><span>PabsAct b7</span><span>'+m.pabs_active+'</span><span>HomeSensor b8</span><span>'+m.home_sensor+'</span><span>SystemReady b9</span><span class='+cls(m.system_ready)+'>'+m.system_ready+'</span>'}"
        "updateDir();setInterval(()=>{if(!cycleRunning)readStates(false)},3000);readStates(false);"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t s_uri_handlers[] = {
    {.uri = "/",              .method = HTTP_GET,  .handler = http_get_kinco_root_handler,  .user_ctx = nullptr},
    {.uri = "/api/status",    .method = HTTP_GET,  .handler = http_get_status_handler,      .user_ctx = nullptr},
    {.uri = "/api/command",   .method = HTTP_POST, .handler = http_post_command_handler,    .user_ctx = nullptr},
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
    /* El handler de comandos usa buffers grandes en stack (buf[512]+resp[2048])
     * y arma respuestas JSON extensas con snprintf; el default de 4096 queda
     * al borde del overflow. Lo subimos para dar margen. */
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&s_http_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    for (size_t i = 0; i < sizeof(s_uri_handlers) / sizeof(s_uri_handlers[0]); i++) {
        httpd_register_uri_handler(s_http_server, &s_uri_handlers[i]);
    }

    ESP_LOGI(TAG, "HTTP server iniciado en puerto %u", port);
    status_led_set_state(STATUS_LED_STATE_WIFI_ONLY);
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
