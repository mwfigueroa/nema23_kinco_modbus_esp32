/**
 * wifi_manager.cpp â€” ImplementaciÃ³n WiFi AP+STA + HTTP Server
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
#define KINCO_MAX_REGS          120
#define KINCO_TEST_STEPS        5000
#define KINCO_REL_TEST_STEPS    7000
#define KINCO_PROGRAM_NAME      "Kinco_esp_Modbus_test_2/MAIN_MAIN.ilp"
#define KINCO_CYCLE_BUSY_MASK   0x100F
#define KINCO_AXIS_COUNT        2

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
#define KINCO_DEFAULT_PREL_MAXF 2000
#define KINCO_DEFAULT_PREL_MINF 300
#define KINCO_DEFAULT_PREL_TIME 300
#define KINCO_DEFAULT_HOME_MODE 1
#define KINCO_DEFAULT_HOME_DIR  0
#define KINCO_DEFAULT_HOME_MAXF 1000
#define KINCO_DEFAULT_HOME_MINF 200
#define KINCO_DEFAULT_HOME_TIME 300
#define KINCO_DEFAULT_JOG_SPEED 1000
#define KINCO_MIN_FREQ          125
#define KINCO_MAX_FREQ          200000

typedef struct {
    uint16_t control;
    uint16_t pabs_pos;
    uint16_t pabs_maxf;
    uint16_t pabs_minf;
    uint16_t pabs_time;
    uint16_t home_cmd;
    uint16_t home_mode;
    uint16_t home_dir;
    uint16_t home_minf;
    uint16_t home_maxf;
    uint16_t home_time;
    uint16_t prel_dist;
    uint16_t prel_maxf;
    uint16_t prel_minf;
    uint16_t prel_time;
    uint16_t jog_cmd;
    uint16_t jog_dir;
    uint16_t jog_speed;
    uint16_t status;
    uint16_t status2;
    uint16_t position;
    uint16_t home_status;
    uint16_t home_error;
    uint16_t jog_status;
    uint16_t jog_error;
} kinco_axis_map_t;

typedef struct {
    bool ok;
    uint8_t exception;
    int32_t command_steps;
    uint32_t maxf;
    uint16_t minf;
    uint16_t time_ms;
    int32_t rel_steps;
    uint32_t prel_maxf;
    uint16_t prel_minf;
    uint16_t prel_time_ms;
    uint16_t home_cmd;
    uint16_t home_mode;
    uint16_t home_dir;
    uint16_t home_minf;
    uint32_t home_maxf;
    uint16_t home_time_ms;
    uint16_t jog_cmd;
    uint16_t jog_dir;
    uint32_t jog_speed;
    uint16_t control_word;
    uint16_t status_word;
    uint16_t status_word2;
    uint16_t status_word3;
    uint16_t home_status_word;
    uint16_t home_error_word;
    uint16_t jog_status_word;
    uint16_t jog_error_word;
    int32_t position;
} kinco_axis_status_t;

static const kinco_axis_map_t s_kinco_axis[KINCO_AXIS_COUNT] = {
    /*
     * Mapa del programa PLC Kinco_esp_Modbus_test_2 / MAIN_MAIN.ilp:
     *
     * Trigger: escribir un valor â‰  0 en 40151-40152 (%VD100 = Cmd_TargetSteps)
     * arranca automaticamente el ciclo completo (ida â†’ espera 3 s â†’ vuelta a 0).
     * No tiene palabra de control en 40070; PSTOP se usa internamente para JOG.
     *
     * Parametros PABS:   MAXF=40153-40154 (%VD104), MINF=40155 (%VW108),
     *                    TIME=40156 (%VW110).
     * Estado:            40252 (%VW302) bits de ciclo.
     * Error PABS:        40253 (%VB304) â€” byte bajo.
     * Posicion actual:   40201-40202 (%VD200).
     *
     * HOME usa 40157-40163 y estado adicional 40255-40256.
     * PREL relativo usa 40167-40172.
     * JOG usa 40175-40178 y estado adicional 40257-40258.
     * Campo control se pone a 0 porque este PLC no usa palabra de control.
     * Las direcciones aqui son base 0 para Modbus: 40001 -> 0.
     */
    {0, 150, 152, 154, 155, 156, 157, 158, 159, 160, 162,
     166, 168, 170, 171, 174, 175, 176, 251, 252, 200, 254, 255, 256, 257},
    /*
     * Motor 2 / AXIS=1:
     * PABS: 40301-40306 (%VD400..%VW410)
     * HOME: 40307-40313 (%VW412..%VW424), sensor PLC I0.3
     * PREL: 40317-40322 (%VD432..%VW442)
     * JOG:  40325-40328 (%VW448..%VD452), entradas PLC I0.4/I0.5
     * Posicion: 40351-40352 (%VD500, copia de %SMD242)
     * Estado: 40402-40408 (%VW602..%VW614)
     */
    {0, 300, 302, 304, 305, 306, 307, 308, 309, 310, 312,
     316, 318, 320, 321, 324, 325, 326, 401, 402, 350, 404, 405, 406, 407},
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

    uint8_t resp[260] = {};
    size_t resp_len = 0;
    status_led_modbus_activity(STATUS_LED_MODBUS_WRITE);
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, KINCO_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Modbus FC16 fallo: reg=%u qty=%u err=%s",
                 (unsigned)(40001 + start_reg), (unsigned)quantity,
                 esp_err_to_name(ret));
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        ESP_LOGW(TAG, "Modbus FC16 CRC invalido: reg=%u qty=%u resp_len=%u",
                 (unsigned)(40001 + start_reg), (unsigned)quantity,
                 (unsigned)resp_len);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == KINCO_SLAVE_ID && resp[1] == (0x10 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        ESP_LOGW(TAG, "Modbus FC16 exception: reg=%u qty=%u code=0x%02X",
                 (unsigned)(40001 + start_reg), (unsigned)quantity, resp[2]);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_FAIL;
    }

    if (resp_len != 8 || resp[0] != KINCO_SLAVE_ID || resp[1] != 0x10 ||
        resp[2] != (uint8_t)(start_reg >> 8) || resp[3] != (uint8_t)(start_reg & 0xFF) ||
        resp[4] != (uint8_t)(quantity >> 8) || resp[5] != (uint8_t)(quantity & 0xFF)) {
        ESP_LOGW(TAG, "Modbus FC16 respuesta invalida: reg=%u qty=%u resp_len=%u",
                 (unsigned)(40001 + start_reg), (unsigned)quantity,
                 (unsigned)resp_len);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGD(TAG, "Modbus FC16 OK: reg=%u qty=%u",
             (unsigned)(40001 + start_reg), (unsigned)quantity);
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

    uint8_t resp[260] = {};
    size_t resp_len = 0;
    status_led_modbus_activity(STATUS_LED_MODBUS_WRITE);
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, KINCO_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Modbus FC06 fallo: reg=%u value=%u err=%s",
                 (unsigned)(40001 + reg), (unsigned)value, esp_err_to_name(ret));
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        ESP_LOGW(TAG, "Modbus FC06 CRC invalido: reg=%u resp_len=%u",
                 (unsigned)(40001 + reg), (unsigned)resp_len);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == KINCO_SLAVE_ID && resp[1] == (0x06 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        ESP_LOGW(TAG, "Modbus FC06 exception: reg=%u code=0x%02X",
                 (unsigned)(40001 + reg), resp[2]);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_FAIL;
    }

    if (resp_len != req_len || memcmp(resp, req, req_len) != 0) {
        ESP_LOGW(TAG, "Modbus FC06 respuesta invalida: reg=%u resp_len=%u",
                 (unsigned)(40001 + reg), (unsigned)resp_len);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGD(TAG, "Modbus FC06 OK: reg=%u value=%u",
             (unsigned)(40001 + reg), (unsigned)value);
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

    uint8_t resp[260] = {};
    size_t resp_len = 0;
    status_led_modbus_activity(STATUS_LED_MODBUS_READ);
    esp_err_t ret = bridge_rs485_transact(req, req_len, resp, sizeof(resp),
                                          &resp_len, KINCO_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Modbus FC03 fallo: reg=%u qty=%u err=%s",
                 (unsigned)(40001 + start_reg), (unsigned)quantity,
                 esp_err_to_name(ret));
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ret;
    }

    if (!modbus_crc_ok(resp, resp_len)) {
        ESP_LOGW(TAG, "Modbus FC03 CRC invalido: reg=%u qty=%u resp_len=%u",
                 (unsigned)(40001 + start_reg), (unsigned)quantity,
                 (unsigned)resp_len);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_ERR_INVALID_CRC;
    }

    if (resp_len >= 5 && resp[0] == KINCO_SLAVE_ID && resp[1] == (0x03 | 0x80)) {
        if (exception_code) *exception_code = resp[2];
        ESP_LOGW(TAG, "Modbus FC03 exception: reg=%u qty=%u code=0x%02X",
                 (unsigned)(40001 + start_reg), (unsigned)quantity, resp[2]);
        status_led_modbus_activity(STATUS_LED_MODBUS_ERROR);
        return ESP_FAIL;
    }

    size_t expected_len = 5 + (size_t)quantity * 2;
    if (resp_len != expected_len || resp[0] != KINCO_SLAVE_ID ||
        resp[1] != 0x03 || resp[2] != quantity * 2) {
        ESP_LOGW(TAG, "Modbus FC03 respuesta invalida: reg=%u qty=%u resp_len=%u expected=%u",
                 (unsigned)(40001 + start_reg), (unsigned)quantity,
                 (unsigned)resp_len, (unsigned)expected_len);
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

static unsigned kinco_modbus_reg(uint16_t zero_based_reg)
{
    return 40001u + (unsigned)zero_based_reg;
}

static unsigned kinco_optional_modbus_reg(uint16_t zero_based_reg)
{
    return zero_based_reg ? kinco_modbus_reg(zero_based_reg) : 0;
}

static bool kinco_valid_axis(int axis)
{
    return axis >= 0 && axis < KINCO_AXIS_COUNT;
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
    if (s_kinco_axis[axis].control == 0) return ESP_ERR_NOT_SUPPORTED;
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

static esp_err_t kinco_axis_pabs(int axis, int32_t target, uint32_t maxf,
                                 uint16_t minf, uint16_t time_ms,
                                 uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;

    const kinco_axis_map_t *m = &s_kinco_axis[axis];

    ESP_LOGD(TAG, "PABS start: axis=%d target=%ld maxf=%lu minf=%u time=%u",
             axis, (long)target, (unsigned long)maxf,
             (unsigned)minf, (unsigned)time_ms);

    uint16_t cfg_words[4] = {};
    kinco_u32_to_words(maxf, &cfg_words[0]);
    cfg_words[2] = minf;
    cfg_words[3] = time_ms;

    esp_err_t ret = plc_write_holding_registers(m->pabs_maxf, cfg_words, 4,
                                                exception_code);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PABS fallo escribiendo parametros: err=%s exception=0x%02X",
                 esp_err_to_name(ret), exception_code ? *exception_code : 0);
        return ret;
    }

    uint16_t target_words[2] = {};
    kinco_u32_to_words((uint32_t)target, target_words);
    ret = plc_write_holding_registers(m->pabs_pos, target_words, 2,
                                      exception_code);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PABS fallo escribiendo target: err=%s exception=0x%02X",
                 esp_err_to_name(ret), exception_code ? *exception_code : 0);
        return ret;
    }

    /* Si el programa PLC tiene palabra de control (control != 0),
     * pulsar el bit de StartPABS. Si no (ej. Kinco_esp_Modbus_test_2),
     * el propio write a pabs_pos dispara el ciclo automaticamente. */
    if (m->control != 0) {
        ret = kinco_axis_pulse_control(axis, KINCO_CMD_START_PABS, exception_code);
    }
    ESP_LOGD(TAG, "PABS comando enviado: axis=%d target=%ld result=%s exception=0x%02X",
             axis, (long)target, esp_err_to_name(ret),
             exception_code ? *exception_code : 0);
    return ret;
}

static esp_err_t kinco_axis_prel(int axis, int32_t delta, uint32_t maxf,
                                 uint16_t minf, uint16_t time_ms,
                                 uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;

    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    if (m->prel_dist == 0) return ESP_ERR_NOT_SUPPORTED;

    ESP_LOGD(TAG, "PREL start: axis=%d delta=%ld maxf=%lu minf=%u time=%u",
             axis, (long)delta, (unsigned long)maxf,
             (unsigned)minf, (unsigned)time_ms);

    uint16_t cfg_words[4] = {};
    kinco_u32_to_words(maxf, &cfg_words[0]);
    cfg_words[2] = minf;
    cfg_words[3] = time_ms;

    esp_err_t ret = plc_write_holding_registers(m->prel_maxf, cfg_words, 4,
                                                exception_code);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PREL fallo escribiendo parametros: err=%s exception=0x%02X",
                 esp_err_to_name(ret), exception_code ? *exception_code : 0);
        return ret;
    }

    uint16_t delta_words[2] = {};
    kinco_u32_to_words((uint32_t)delta, delta_words);
    ret = plc_write_holding_registers(m->prel_dist, delta_words, 2,
                                      exception_code);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PREL fallo escribiendo delta: err=%s exception=0x%02X",
                 esp_err_to_name(ret), exception_code ? *exception_code : 0);
        return ret;
    }

    if (m->control != 0) {
        ret = kinco_axis_pulse_control(axis, KINCO_CMD_START_PREL, exception_code);
    }
    ESP_LOGD(TAG, "PREL comando enviado: axis=%d delta=%ld result=%s exception=0x%02X",
             axis, (long)delta, esp_err_to_name(ret),
             exception_code ? *exception_code : 0);
    return ret;
}

static esp_err_t kinco_axis_home(int axis, uint16_t dir, uint16_t mode,
                                 uint32_t maxf, uint16_t minf,
                                 uint16_t time_ms, uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;

    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    if (m->home_cmd == 0) {
        return kinco_axis_pulse_control(axis, KINCO_CMD_START_HOME, exception_code);
    }

    dir = dir ? 1 : 0;
    mode = mode ? 1 : 0;

    ESP_LOGD(TAG, "HOME start: axis=%d dir=%u mode=%u maxf=%lu minf=%u time=%u",
             axis, (unsigned)dir, (unsigned)mode, (unsigned long)maxf,
             (unsigned)minf, (unsigned)time_ms);

    uint16_t cfg_words[6] = {};
    cfg_words[0] = mode;
    cfg_words[1] = dir;
    cfg_words[2] = minf;
    kinco_u32_to_words(maxf, &cfg_words[3]);
    cfg_words[5] = time_ms;

    esp_err_t ret = plc_write_holding_registers(m->home_mode, cfg_words, 6,
                                                exception_code);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HOME fallo escribiendo parametros: err=%s exception=0x%02X",
                 esp_err_to_name(ret), exception_code ? *exception_code : 0);
        return ret;
    }

    ret = plc_write_single_register(m->home_cmd, 1, exception_code);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HOME fallo escribiendo comando: err=%s exception=0x%02X",
                 esp_err_to_name(ret), exception_code ? *exception_code : 0);
        return ret;
    }

    ESP_LOGD(TAG, "HOME comando enviado: axis=%d dir=%u result=%s exception=0x%02X",
             axis, (unsigned)dir, esp_err_to_name(ret),
             exception_code ? *exception_code : 0);
    return ret;
}

static esp_err_t kinco_axis_jog(int axis, int32_t speed_hz,
                                uint8_t *exception_code)
{
    if (!kinco_valid_axis(axis)) return ESP_ERR_INVALID_ARG;
    if (speed_hz == 0) {
        return plc_write_single_register(s_kinco_axis[axis].jog_cmd, 0,
                                         exception_code);
    }

    uint32_t abs_speed = (speed_hz < 0) ? (uint32_t)(-speed_hz) : (uint32_t)speed_hz;
    if (abs_speed < KINCO_MIN_FREQ) abs_speed = KINCO_MIN_FREQ;
    if (abs_speed > KINCO_MAX_FREQ) abs_speed = KINCO_MAX_FREQ;

    uint16_t cfg[3] = {};
    cfg[0] = speed_hz > 0 ? 0 : 1;
    kinco_u32_to_words(abs_speed, &cfg[1]);
    esp_err_t ret = plc_write_holding_registers(s_kinco_axis[axis].jog_dir,
                                                cfg, 3, exception_code);
    if (ret != ESP_OK) return ret;

    return plc_write_single_register(s_kinco_axis[axis].jog_cmd,
                                     speed_hz > 0 ? 1 : 2, exception_code);
}

static esp_err_t kinco_axis_read_status(int axis, kinco_axis_status_t *status)
{
    if (!kinco_valid_axis(axis) || !status) return ESP_ERR_INVALID_ARG;

    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    uint8_t exception_code = 0;
    esp_err_t ret;

    /* Leer palabra de control si el programa PLC la tiene mapeada (control != 0).
     * Para programas sin palabra de control (ej. MAIN_MAIN.ilp) se omite
     * porque la direccion 0 no es valida en el mapeo MODBUS de la Kinco. */
    if (m->control != 0) {
        uint16_t ctrl_buf[1] = {};
        ret = plc_read_holding_registers(m->control, 1, ctrl_buf,
                                         &exception_code);
        if (ret != ESP_OK) {
            status->ok = false;
            status->exception = exception_code;
            return ret;
        }
        status->control_word = ctrl_buf[0];
    } else {
        status->control_word = 0;
    }

    /* Leer posiciÃ³n objetivo PABS desde 40151-40152 (%VD100). */
    uint16_t cfg_regs[6] = {};
    ret = plc_read_holding_registers(m->pabs_pos, 6, cfg_regs, &exception_code);
    if (ret != ESP_OK) {
        status->ok = false;
        status->exception = exception_code;
        ESP_LOGW(TAG, "Status fallo leyendo parametros %u..%u: err=%s exception=0x%02X",
                 (unsigned)(40001 + m->pabs_pos), (unsigned)(40001 + m->pabs_pos + 5),
                 esp_err_to_name(ret), exception_code);
        return ret;
    }

    status->command_steps = kinco_words_to_int32(cfg_regs[0], cfg_regs[1]);
    status->maxf = ((uint32_t)cfg_regs[3] << 16) | cfg_regs[2];
    status->minf = cfg_regs[4];
    status->time_ms = cfg_regs[5];

    status->home_cmd = 0;
    status->home_mode = KINCO_DEFAULT_HOME_MODE;
    status->home_dir = KINCO_DEFAULT_HOME_DIR;
    status->home_minf = KINCO_DEFAULT_HOME_MINF;
    status->home_maxf = KINCO_DEFAULT_HOME_MAXF;
    status->home_time_ms = KINCO_DEFAULT_HOME_TIME;
    if (m->home_cmd != 0) {
        uint16_t home_regs[7] = {};
        ret = plc_read_holding_registers(m->home_cmd, 7, home_regs, &exception_code);
        if (ret != ESP_OK) {
            status->ok = false;
            status->exception = exception_code;
            ESP_LOGW(TAG, "Status fallo leyendo HOME %u..%u: err=%s exception=0x%02X",
                     (unsigned)(40001 + m->home_cmd),
                     (unsigned)(40001 + m->home_cmd + 6),
                     esp_err_to_name(ret), exception_code);
            return ret;
        }
        status->home_cmd = home_regs[0];
        status->home_mode = home_regs[1];
        status->home_dir = home_regs[2];
        status->home_minf = home_regs[3];
        status->home_maxf = ((uint32_t)home_regs[5] << 16) | home_regs[4];
        status->home_time_ms = home_regs[6];
    }

    status->rel_steps = 0;
    status->prel_maxf = 0;
    status->prel_minf = 0;
    status->prel_time_ms = 0;
    if (m->prel_dist != 0) {
        uint16_t rel_regs[6] = {};
        ret = plc_read_holding_registers(m->prel_dist, 6, rel_regs, &exception_code);
        if (ret != ESP_OK) {
            status->ok = false;
            status->exception = exception_code;
            ESP_LOGW(TAG, "Status fallo leyendo PREL %u..%u: err=%s exception=0x%02X",
                     (unsigned)(40001 + m->prel_dist), (unsigned)(40001 + m->prel_dist + 5),
                     esp_err_to_name(ret), exception_code);
            return ret;
        }
        status->rel_steps = kinco_words_to_int32(rel_regs[0], rel_regs[1]);
        status->prel_maxf = ((uint32_t)rel_regs[3] << 16) | rel_regs[2];
        status->prel_minf = rel_regs[4];
        status->prel_time_ms = rel_regs[5];
    }

    status->jog_cmd = 0;
    status->jog_dir = 0;
    status->jog_speed = KINCO_DEFAULT_JOG_SPEED;
    if (m->jog_cmd != 0) {
        uint16_t jog_regs[4] = {};
        ret = plc_read_holding_registers(m->jog_cmd, 4, jog_regs, &exception_code);
        if (ret != ESP_OK) {
            status->ok = false;
            status->exception = exception_code;
            ESP_LOGW(TAG, "Status fallo leyendo JOG %u..%u: err=%s exception=0x%02X",
                     (unsigned)(40001 + m->jog_cmd),
                     (unsigned)(40001 + m->jog_cmd + 3),
                     esp_err_to_name(ret), exception_code);
            return ret;
        }
        status->jog_cmd = jog_regs[0];
        status->jog_dir = jog_regs[1];
        status->jog_speed = ((uint32_t)jog_regs[3] << 16) | jog_regs[2];
    }

    uint16_t pos_regs[2] = {};
    ret = plc_read_holding_registers(m->position, 2, pos_regs, &exception_code);
    if (ret != ESP_OK) {
        status->ok = false;
        status->exception = exception_code;
        ESP_LOGW(TAG, "Status fallo leyendo posicion %u..%u: err=%s exception=0x%02X",
                 (unsigned)(40001 + m->position), (unsigned)(40001 + m->position + 1),
                 esp_err_to_name(ret), exception_code);
        return ret;
    }
    status->position = kinco_words_to_int32(pos_regs[0], pos_regs[1]);

    uint16_t status_regs[7] = {};
    ret = plc_read_holding_registers(m->status, 7, status_regs, &exception_code);
    status->ok = (ret == ESP_OK);
    status->exception = exception_code;
    if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Status fallo leyendo estado %u..%u: err=%s exception=0x%02X",
                 (unsigned)(40001 + m->status), (unsigned)(40001 + m->status + 6),
                 esp_err_to_name(ret), exception_code);
        return ret;
    }
    status->status_word = status_regs[0];
    status->status_word2 = status_regs[1];
    status->status_word3 = status_regs[2];
    status->home_status_word = status_regs[3];
    status->home_error_word = status_regs[4];
    status->jog_status_word = status_regs[5];
    status->jog_error_word = status_regs[6];
    ESP_LOGD(TAG,
             "PLC status: cmd=%ld pos=%ld maxf=%lu minf=%u time=%u "
             "home_cmd=%u home_dir=%u jog_cmd=%u jog_dir=%u jog_speed=%lu "
             "40252=0x%04X 40253=0x%04X(err=%u) "
             "40254=0x%04X 40255=0x%04X 40256=0x%04X(home_err=%u) "
             "40257=0x%04X 40258=0x%04X(jog_err=%u) "
             "active=%u out=%u wait=%u ret=%u done=%u cyc_err=%u out_err=%u ret_err=%u en_log=%u alive=%u",
             (long)status->command_steps, (long)status->position,
             (unsigned long)status->maxf, (unsigned)status->minf,
             (unsigned)status->time_ms,
             (unsigned)status->home_cmd, (unsigned)status->home_dir,
             (unsigned)status->jog_cmd, (unsigned)status->jog_dir,
             (unsigned long)status->jog_speed,
             (unsigned)status->status_word,
             (unsigned)status->status_word2,
             (unsigned)(status->status_word2 & 0x00FF),
             (unsigned)status->status_word3,
             (unsigned)status->home_status_word,
             (unsigned)status->home_error_word,
             (unsigned)(status->home_error_word & 0x00FF),
             (unsigned)status->jog_status_word,
             (unsigned)status->jog_error_word,
             (unsigned)(status->jog_error_word & 0x00FF),
             (unsigned)((status->status_word >> 0) & 1),
             (unsigned)((status->status_word >> 1) & 1),
             (unsigned)((status->status_word >> 2) & 1),
             (unsigned)((status->status_word >> 3) & 1),
             (unsigned)((status->status_word >> 4) & 1),
             (unsigned)((status->status_word >> 5) & 1),
             (unsigned)((status->status_word >> 7) & 1),
             (unsigned)((status->status_word >> 9) & 1),
             (unsigned)((status->status_word >> 10) & 1),
             (unsigned)((status->status_word >> 15) & 1));
    return ESP_OK;
}

static esp_err_t kinco_axis_read_fast_status(int axis, kinco_axis_status_t *status)
{
    if (!kinco_valid_axis(axis) || !status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));

    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    uint8_t exception_code = 0;

    uint16_t status_regs[7] = {};
    esp_err_t ret = plc_read_holding_registers(m->status, 7, status_regs,
                                               &exception_code);
    status->exception = exception_code;
    if (ret != ESP_OK) {
        status->ok = false;
        ESP_LOGW(TAG, "Fast status fallo leyendo estado %u..%u: err=%s exception=0x%02X",
                 (unsigned)(40001 + m->status), (unsigned)(40001 + m->status + 6),
                 esp_err_to_name(ret), exception_code);
        return ret;
    }

    uint16_t pos_regs[2] = {};
    ret = plc_read_holding_registers(m->position, 2, pos_regs, &exception_code);
    status->exception = exception_code;
    if (ret != ESP_OK) {
        status->ok = false;
        ESP_LOGW(TAG, "Fast status fallo leyendo posicion %u..%u: err=%s exception=0x%02X",
                 (unsigned)(40001 + m->position), (unsigned)(40001 + m->position + 1),
                 esp_err_to_name(ret), exception_code);
        return ret;
    }

    status->status_word = status_regs[0];
    status->status_word2 = status_regs[1];
    status->status_word3 = status_regs[2];
    status->home_status_word = status_regs[3];
    status->home_error_word = status_regs[4];
    status->jog_status_word = status_regs[5];
    status->jog_error_word = status_regs[6];
    status->position = kinco_words_to_int32(pos_regs[0], pos_regs[1]);
    status->ok = true;
    return ESP_OK;
}

/* ================================================================
 * Handlers HTTP
 * ================================================================ */

/* ----------------------------------------------------------------
 * Parser JSON mÃ­nimo (sin dependencias). Reconoce {"cmd":"...","arg":N}
 * con arg numÃ©rico o entre comillas. Suficiente para el panel de control;
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

static int axis_from_http_req(httpd_req_t *req)
{
    char query[64] = {};
    char value[8] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "axis", value, sizeof(value)) == ESP_OK) {
        int axis = atoi(value);
        if (kinco_valid_axis(axis)) return axis;
    }
    return 0;
}

static void update_led_from_status(const kinco_axis_status_t *st);

static void kinco_status_response(int axis, char *resp, size_t resp_sz, const char *cmd)
{
    if (!kinco_valid_axis(axis)) axis = 0;
    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    kinco_axis_status_t st = {};
    esp_err_t ret = kinco_axis_read_status(axis, &st);
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
    bool prel_active = (st.status_word >> 12) & 1;
    bool prel_done = (st.status_word >> 13) & 1;
    bool prel_err = (st.status_word >> 14) & 1;
    bool plc_alive = (st.status_word >> 15) & 1;
    bool home_active = (st.home_status_word >> 0) & 1;
    bool home_done = (st.home_status_word >> 1) & 1;
    bool home_err = (st.home_status_word >> 2) & 1;
    bool home_sensor = (st.home_status_word >> 3) & 1;
    bool home_dir_bit = (st.home_status_word >> 4) & 1;
    bool home_reset_pulse = (st.home_status_word >> 5) & 1;
    bool jog_active = (st.jog_status_word >> 0) & 1;
    bool jog_done = (st.jog_status_word >> 1) & 1;
    bool jog_err = (st.jog_status_word >> 2) & 1;
    bool jog_dir_bit = (st.jog_status_word >> 3) & 1;
    bool jog_fwd_input = (st.jog_status_word >> 4) & 1;
    bool jog_bwd_input = (st.jog_status_word >> 5) & 1;
    bool jog_cmd_active = (st.jog_status_word >> 6) & 1;
    bool jog_stop_err = (st.jog_status_word >> 7) & 1;
    uint16_t error_out = st.status_word2 & 0x00FF;
    uint16_t error_return = st.status_word3 & 0x00FF;
    uint16_t debug_return = (st.status_word3 >> 8) & 0x00FF;
    uint16_t error_home = st.home_error_word & 0x00FF;
    uint16_t error_jog = st.jog_error_word & 0x00FF;
    uint16_t error_jog_stop = (st.jog_error_word >> 8) & 0x00FF;

    snprintf(resp, resp_sz,
             "{\"result\":\"%s\",\"cmd\":\"%s\",\"target\":\"kinco\",\"slave\":%u,"
             "\"axis\":%d,\"program\":\"" KINCO_PROGRAM_NAME "\","
             "\"control_reg\":%u,\"command_reg\":%u,\"maxf_reg\":%u,\"minf_reg\":%u,"
             "\"time_reg\":%u,\"home_cmd_reg\":%u,\"home_mode_reg\":%u,"
             "\"home_dir_reg\":%u,\"home_minf_reg\":%u,\"home_maxf_reg\":%u,"
             "\"home_time_reg\":%u,\"rel_reg\":%u,\"prel_maxf_reg\":%u,"
             "\"prel_minf_reg\":%u,\"prel_time_reg\":%u,"
             "\"jog_cmd_reg\":%u,\"jog_dir_reg\":%u,\"jog_speed_reg\":%u,"
             "\"status_reg\":%u,\"home_status_reg\":%u,\"jog_status_reg\":%u,"
             "\"position_reg\":%u,"
             "\"error_out_reg\":%u,\"error_return_reg\":%u,"
             "\"error_home_reg\":%u,\"error_jog_reg\":%u,"
             "\"motor\":{\"ok\":%s,\"err\":\"%s\",\"exception\":%u,"
             "\"command_steps\":%ld,\"pos\":%ld,\"maxf\":%lu,\"minf\":%u,"
             "\"time\":%u,\"rel_steps\":%ld,\"prel_maxf\":%lu,"
             "\"prel_minf\":%u,\"prel_time\":%u,"
             "\"home_cmd\":%u,\"home_mode\":%u,\"home_dir\":%u,"
             "\"home_maxf\":%lu,\"home_minf\":%u,\"home_time\":%u,"
             "\"jog_cmd\":%u,\"jog_dir\":%u,\"jog_speed\":%lu,"
             "\"status\":%u,\"status2\":%u,\"status3\":%u,"
             "\"home_status\":%u,\"home_error_word\":%u,"
             "\"jog_status\":%u,\"jog_error_word\":%u,"
             "\"error_out\":%u,\"error_return\":%u,\"debug_return\":%u,"
             "\"error_home\":%u,\"error_jog\":%u,"
             "\"cycle_active\":%u,\"move_out_active\":%u,"
             "\"wait_return_active\":%u,\"return_active\":%u,"
             "\"cycle_done\":%u,\"cycle_err\":%u,"
             "\"pabs_out_done\":%u,\"pabs_out_err\":%u,"
             "\"pabs_return_done\":%u,\"pabs_return_err\":%u,"
             "\"enable_out\":%u,\"wait_done\":%u,"
             "\"prel_active\":%u,\"prel_done\":%u,\"prel_err\":%u,"
             "\"home_active\":%u,\"home_done\":%u,\"home_err\":%u,"
             "\"home_sensor\":%u,\"home_dir_bit\":%u,\"home_reset_pulse\":%u,"
             "\"jog_active\":%u,\"jog_done\":%u,\"jog_err\":%u,"
             "\"jog_dir_bit\":%u,\"jog_fwd_input\":%u,\"jog_bwd_input\":%u,"
             "\"jog_cmd_active\":%u,\"jog_stop_err\":%u,"
             "\"error_jog_stop\":%u,"
             "\"plc_alive\":%u}}",
             ok ? "ok" : "error", cmd, KINCO_SLAVE_ID, axis,
             kinco_optional_modbus_reg(m->control),
             kinco_modbus_reg(m->pabs_pos), kinco_modbus_reg(m->pabs_maxf),
             kinco_modbus_reg(m->pabs_minf), kinco_modbus_reg(m->pabs_time),
             kinco_modbus_reg(m->home_cmd), kinco_modbus_reg(m->home_mode),
             kinco_modbus_reg(m->home_dir), kinco_modbus_reg(m->home_minf),
             kinco_modbus_reg(m->home_maxf), kinco_modbus_reg(m->home_time),
             kinco_modbus_reg(m->prel_dist), kinco_modbus_reg(m->prel_maxf),
             kinco_modbus_reg(m->prel_minf), kinco_modbus_reg(m->prel_time),
             kinco_modbus_reg(m->jog_cmd), kinco_modbus_reg(m->jog_dir),
             kinco_modbus_reg(m->jog_speed), kinco_modbus_reg(m->status),
             kinco_modbus_reg(m->home_status), kinco_modbus_reg(m->jog_status),
             kinco_modbus_reg(m->position), kinco_modbus_reg(m->status2),
             kinco_modbus_reg(m->status2 + 1), kinco_modbus_reg(m->home_error),
             kinco_modbus_reg(m->jog_error),
             st.ok ? "true" : "false", esp_err_to_name(ret), st.exception,
             (long)st.command_steps, (long)st.position,
             (unsigned long)st.maxf, st.minf, st.time_ms,
             (long)st.rel_steps, (unsigned long)st.prel_maxf,
             st.prel_minf, st.prel_time_ms,
             st.home_cmd, st.home_mode, st.home_dir,
             (unsigned long)st.home_maxf, st.home_minf, st.home_time_ms,
             st.jog_cmd, st.jog_dir, (unsigned long)st.jog_speed,
             st.status_word, st.status_word2, st.status_word3,
             st.home_status_word, st.home_error_word,
             st.jog_status_word, st.jog_error_word,
             error_out, error_return, debug_return, error_home, error_jog,
             cycle_active, move_out_active, wait_return_active, return_active,
             cycle_done, cycle_err, pabs_out_done, pabs_out_err,
             pabs_return_done, pabs_return_err, enable_out, wait_done,
             prel_active, prel_done, prel_err,
             home_active, home_done, home_err,
             home_sensor, home_dir_bit, home_reset_pulse,
             jog_active, jog_done, jog_err, jog_dir_bit,
             jog_fwd_input, jog_bwd_input, jog_cmd_active,
             jog_stop_err, error_jog_stop,
             plc_alive);
}

/* â”€â”€ Helper: actualiza LED segÃºn estado del motor â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static void kinco_fast_status_response(int axis, char *resp, size_t resp_sz, const char *cmd)
{
    if (!kinco_valid_axis(axis)) axis = 0;
    const kinco_axis_map_t *m = &s_kinco_axis[axis];
    kinco_axis_status_t st = {};
    esp_err_t ret = kinco_axis_read_fast_status(axis, &st);
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
    bool prel_active = (st.status_word >> 12) & 1;
    bool prel_done = (st.status_word >> 13) & 1;
    bool prel_err = (st.status_word >> 14) & 1;
    bool plc_alive = (st.status_word >> 15) & 1;
    bool home_active = (st.home_status_word >> 0) & 1;
    bool home_done = (st.home_status_word >> 1) & 1;
    bool home_err = (st.home_status_word >> 2) & 1;
    bool home_sensor = (st.home_status_word >> 3) & 1;
    bool home_dir_bit = (st.home_status_word >> 4) & 1;
    bool home_reset_pulse = (st.home_status_word >> 5) & 1;
    bool jog_active = (st.jog_status_word >> 0) & 1;
    bool jog_done = (st.jog_status_word >> 1) & 1;
    bool jog_err = (st.jog_status_word >> 2) & 1;
    bool jog_dir_bit = (st.jog_status_word >> 3) & 1;
    bool jog_fwd_input = (st.jog_status_word >> 4) & 1;
    bool jog_bwd_input = (st.jog_status_word >> 5) & 1;
    bool jog_cmd_active = (st.jog_status_word >> 6) & 1;
    bool jog_stop_err = (st.jog_status_word >> 7) & 1;
    uint16_t error_out = st.status_word2 & 0x00FF;
    uint16_t error_return = st.status_word3 & 0x00FF;
    uint16_t debug_return = (st.status_word3 >> 8) & 0x00FF;
    uint16_t error_home = st.home_error_word & 0x00FF;
    uint16_t error_jog = st.jog_error_word & 0x00FF;
    uint16_t error_jog_stop = (st.jog_error_word >> 8) & 0x00FF;

    snprintf(resp, resp_sz,
             "{\"result\":\"%s\",\"cmd\":\"%s\",\"target\":\"kinco\",\"slave\":%u,"
             "\"axis\":%d,\"program\":\"" KINCO_PROGRAM_NAME "\",\"fast\":true,"
             "\"status_reg\":%u,\"home_status_reg\":%u,"
             "\"jog_status_reg\":%u,\"position_reg\":%u,"
             "\"motor\":{\"ok\":%s,\"err\":\"%s\",\"exception\":%u,"
             "\"pos\":%ld,\"status\":%u,\"status2\":%u,\"status3\":%u,"
             "\"home_status\":%u,\"home_error_word\":%u,"
             "\"jog_status\":%u,\"jog_error_word\":%u,"
             "\"error_out\":%u,\"error_return\":%u,\"debug_return\":%u,"
             "\"error_home\":%u,\"error_jog\":%u,\"error_jog_stop\":%u,"
             "\"cycle_active\":%u,\"move_out_active\":%u,"
             "\"wait_return_active\":%u,\"return_active\":%u,"
             "\"cycle_done\":%u,\"cycle_err\":%u,"
             "\"pabs_out_done\":%u,\"pabs_out_err\":%u,"
             "\"pabs_return_done\":%u,\"pabs_return_err\":%u,"
             "\"enable_out\":%u,\"wait_done\":%u,"
             "\"prel_active\":%u,\"prel_done\":%u,\"prel_err\":%u,"
             "\"home_active\":%u,\"home_done\":%u,\"home_err\":%u,"
             "\"home_sensor\":%u,\"home_dir_bit\":%u,\"home_reset_pulse\":%u,"
             "\"jog_active\":%u,\"jog_done\":%u,\"jog_err\":%u,"
             "\"jog_dir_bit\":%u,\"jog_fwd_input\":%u,\"jog_bwd_input\":%u,"
             "\"jog_cmd_active\":%u,\"jog_stop_err\":%u,"
             "\"plc_alive\":%u}}",
             ok ? "ok" : "error", cmd, KINCO_SLAVE_ID, axis,
             kinco_modbus_reg(m->status), kinco_modbus_reg(m->home_status),
             kinco_modbus_reg(m->jog_status), kinco_modbus_reg(m->position),
             st.ok ? "true" : "false", esp_err_to_name(ret), st.exception,
             (long)st.position, st.status_word, st.status_word2, st.status_word3,
             st.home_status_word, st.home_error_word,
             st.jog_status_word, st.jog_error_word,
             error_out, error_return, debug_return,
             error_home, error_jog, error_jog_stop,
             cycle_active, move_out_active, wait_return_active, return_active,
             cycle_done, cycle_err, pabs_out_done, pabs_out_err,
             pabs_return_done, pabs_return_err, enable_out, wait_done,
             prel_active, prel_done, prel_err,
             home_active, home_done, home_err,
             home_sensor, home_dir_bit, home_reset_pulse,
             jog_active, jog_done, jog_err, jog_dir_bit,
             jog_fwd_input, jog_bwd_input, jog_cmd_active, jog_stop_err,
             plc_alive);
}

static bool kinco_status_cycle_busy(const kinco_axis_status_t *st)
{
    return st && st->ok &&
           (((st->status_word & KINCO_CYCLE_BUSY_MASK) != 0) ||
            st->command_steps != 0 ||
            st->rel_steps != 0 ||
            st->home_cmd != 0 ||
            st->jog_cmd != 0 ||
            ((st->home_status_word & 0x0001) != 0) ||
            ((st->jog_status_word & 0x0001) != 0));
}

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
    bool prel_active = (st->status_word >> 12) & 1;
    bool prel_err = (st->status_word >> 14) & 1;
    bool home_active = (st->home_status_word >> 0) & 1;
    bool home_err = (st->home_status_word >> 2) & 1;
    bool jog_active = (st->jog_status_word >> 0) & 1;
    bool jog_err = (st->jog_status_word >> 2) & 1;
    bool jog_stop_err = (st->jog_status_word >> 7) & 1;

    if (cycle_err || pabs_out_err || pabs_return_err || prel_err || home_err ||
        jog_err || jog_stop_err || !st->ok) {
        status_led_set_state(STATUS_LED_STATE_ERROR);
    } else if (cycle_active || move_out_active || wait_return_active || return_active ||
               prel_active || home_active || jog_active) {
        status_led_set_state(STATUS_LED_STATE_MOTOR_MOVING);
    } else {
        status_led_set_state(STATUS_LED_STATE_SYSTEM_READY);
    }
}

/* ================================================================
 * Ciclo automÃ¡tico 0â†’Nâ†’0 â€” ejecutado en tarea dedicada
 *
 * Antes corrÃ­a dentro del handler HTTP y bloqueaba la Ãºnica tarea del
 * servidor hasta ~120 s, dejando sin respuesta al resto de peticiones
 * (incluido el polling de estado de la UI). Ahora el endpoint solo lanza
 * la tarea y devuelve de inmediato; la UI consulta el progreso con
 * "kinco_cycle_status". El acceso RS485 ya estÃ¡ serializado por el mutex
 * del puente, asÃ­ que las lecturas de estado concurrentes son seguras.
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
    bool      done;          /* terminÃ³ (Ã©xito o error) */
    int       phase;         /* CYCLE_PHASE_* */
    int       axis;
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

/* Espera CycleDone (bit4) o CycleErr (bit5) con timeout.
 * En MAIN_MAIN.ilp la PLC hace el ciclo completo (ida â†’ 3s â†’ vuelta a 0). */
static esp_err_t auto_cycle_wait_done(int axis, int timeout_ms,
                                      int32_t *final_pos, uint8_t *exception)
{
    while (timeout_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(250));
        timeout_ms -= 250;
        kinco_axis_status_t cur = {};
        if (kinco_axis_read_fast_status(axis, &cur) == ESP_OK) {
            /* Reflejar fase en el estado compartido para la UI. */
            bool move_out = (cur.status_word >> 1) & 1;
            bool wait_ret = (cur.status_word >> 2) & 1;
            bool returning = (cur.status_word >> 3) & 1;
            portENTER_CRITICAL(&s_auto_cycle_mux);
            if (returning || wait_ret) s_auto_cycle.phase = CYCLE_PHASE_CCW;
            else if (move_out)          s_auto_cycle.phase = CYCLE_PHASE_CW;
            portEXIT_CRITICAL(&s_auto_cycle_mux);

            if (cur.status_word & (1u << 4)) {  /* CycleDone */
                if (final_pos) *final_pos = cur.position;
                return ESP_OK;
            }
            if (cur.status_word & (1u << 5)) {  /* CycleErr */
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
    int32_t pos_final = 0;

    status_led_set_state(STATUS_LED_STATE_MOTOR_MOVING);

    /* En MAIN_MAIN.ilp, escribir un target â‰  0 en 40151-40152 dispara
     * el ciclo completo: ida al target â†’ espera 3 s â†’ vuelta a 0.
     * Solo necesitamos una llamada a kinco_axis_pabs. */
    esp_err_t r = kinco_axis_pabs(p.axis, p.cw_target, p.fwd_speed,
                                  KINCO_DEFAULT_PABS_MINF,
                                  KINCO_DEFAULT_PABS_TIME, &exception_code);
    bool ok = false;
    if (r == ESP_OK) {
        r = auto_cycle_wait_done(p.axis, 120000, &pos_final, &exception_code);
        ok = (r == ESP_OK);
    }

    /* Leer estado final y actualizar el LED */
    kinco_axis_status_t st_final = {};
    kinco_axis_read_fast_status(p.axis, &st_final);
    update_led_from_status(&st_final);

    portENTER_CRITICAL(&s_auto_cycle_mux);
    s_auto_cycle.active = false;
    s_auto_cycle.done = true;
    s_auto_cycle.phase = CYCLE_PHASE_DONE;
    s_auto_cycle.cw_ok = ok;
    s_auto_cycle.ccw_ok = ok;   /* la PLC valida ambas fases */
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
    long mode_value = 0;
    bool has_mode = json_find_int(json, "mode", &mode_value);
    long dir_value = 0;
    bool has_dir = json_find_int(json, "dir", &dir_value);
    uint32_t maxf = clamp_u32(has_speed ? speed_value : KINCO_DEFAULT_PABS_MAXF,
                              KINCO_DEFAULT_PABS_MAXF, KINCO_MIN_FREQ, KINCO_MAX_FREQ);
    uint16_t minf = clamp_u16(has_minf ? minf_value : KINCO_DEFAULT_PABS_MINF,
                              KINCO_DEFAULT_PABS_MINF, KINCO_MIN_FREQ, 65535);
    uint16_t time_ms = clamp_u16(has_time ? time_value : KINCO_DEFAULT_PABS_TIME,
                                 KINCO_DEFAULT_PABS_TIME, 1, 65535);

    if (strcmp(cmd, "kinco_status") == 0) {
        kinco_status_response(axis, resp, resp_sz, cmd);
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

    /* â”€â”€ Comandos de control para MAIN_MAIN.ilp (test_2) â”€â”€ */
    /* Este programa PLC no usa palabra de control 40070. PSTOP queda interno
     * para detener JOG al soltar entradas o al mandar kinco_jog_stop.
     * Se dispara escribiendo un valor â‰  0 en 40151-40152 (Cmd_TargetSteps).
     * Comandos soportados: kinco_pabs, kinco_move_delta, kinco_status,
     * kinco_cycle_status.  kinco_auto_cycle delega en kinco_pabs porque
     * el PLC ya hace el ciclo completo (ida â†’ 3s â†’ vuelta a 0). */
    esp_err_t r = ESP_OK;
    uint8_t exception_code = 0;
    int32_t target_pos = 0;
    bool has_target_pos = false;
    int32_t rel_delta = 0;
    bool has_rel_delta = false;
    bool has_home_request = false;
    int32_t jog_speed_cmd = 0;
    bool has_jog_request = false;

    if (strcmp(cmd, "kinco_clear_command") == 0) {
        uint16_t zero_target[2] = {0, 0};
        uint8_t exception_pabs = 0;
        uint8_t exception_prel = 0;
        uint8_t exception_home = 0;
        uint8_t exception_jog = 0;
        esp_err_t r_pabs = plc_write_holding_registers(s_kinco_axis[axis].pabs_pos,
                                                       zero_target, 2,
                                                       &exception_pabs);
        esp_err_t r_prel = ESP_OK;
        if (s_kinco_axis[axis].prel_dist != 0) {
            r_prel = plc_write_holding_registers(s_kinco_axis[axis].prel_dist,
                                                 zero_target, 2,
                                                 &exception_prel);
        }
        esp_err_t r_home = ESP_OK;
        if (s_kinco_axis[axis].home_cmd != 0) {
            r_home = plc_write_single_register(s_kinco_axis[axis].home_cmd, 0,
                                               &exception_home);
        }
        esp_err_t r_jog = ESP_OK;
        if (s_kinco_axis[axis].jog_cmd != 0) {
            r_jog = plc_write_single_register(s_kinco_axis[axis].jog_cmd, 0,
                                              &exception_jog);
        }
        r = (r_pabs == ESP_OK && r_prel == ESP_OK &&
             r_home == ESP_OK && r_jog == ESP_OK) ? ESP_OK : ESP_FAIL;
        kinco_axis_status_t st = {};
        esp_err_t sr = kinco_axis_read_fast_status(axis, &st);
        snprintf(resp, resp_sz,
                 "{\"result\":\"%s\",\"cmd\":\"%s\",\"write_pabs\":\"%s\","
                 "\"write_prel\":\"%s\",\"write_home\":\"%s\",\"write_jog\":\"%s\","
                 "\"exception_pabs\":%u,\"exception_prel\":%u,"
                 "\"exception_home\":%u,\"exception_jog\":%u,"
                 "\"status_read\":\"%s\",\"command_steps\":%ld,\"rel_steps\":%ld,"
                 "\"home_cmd\":%u,\"jog_cmd\":%u,"
                 "\"status\":%u,\"jog_status\":%u,\"plc_alive\":%u,\"pos\":%ld}",
                 r == ESP_OK ? "ok" : "error", cmd, esp_err_to_name(r_pabs),
                 esp_err_to_name(r_prel), esp_err_to_name(r_home),
                 esp_err_to_name(r_jog),
                 exception_pabs, exception_prel, exception_home, exception_jog,
                 esp_err_to_name(sr), (long)st.command_steps, (long)st.rel_steps,
                 (unsigned)st.home_cmd, (unsigned)st.jog_cmd,
                 st.status_word, st.jog_status_word,
                 (unsigned)((st.status_word >> 15) & 1),
                 (long)st.position);
        return;
    }

    /* Stop JOG: debe ejecutarse aunque la PLC este ocupada. */
    if (strcmp(cmd, "kinco_jog_stop") == 0 ||
        strcmp(cmd, "kinco_jog_off") == 0) {
        r = kinco_axis_jog(axis, 0, &exception_code);
        if (r != ESP_OK) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"target\":\"kinco\","
                     "\"axis\":%d,\"slave\":%u,\"exception\":%u,\"err\":\"%s\"}",
                     cmd, axis, KINCO_SLAVE_ID, exception_code, esp_err_to_name(r));
            return;
        }
        kinco_fast_status_response(axis, resp, resp_sz, cmd);
        return;
    }

    /* Comandos no soportados por este programa PLC. */
    if (strcmp(cmd, "kinco_enable") == 0 ||
        strcmp(cmd, "kinco_set_dir") == 0 ||
        strcmp(cmd, "kinco_stop") == 0 ||
        strcmp(cmd, "kinco_reset_pos") == 0 ||
        strcmp(cmd, "kinco_reset_status") == 0) {
        snprintf(resp, resp_sz,
                 "{\"result\":\"error\",\"cmd\":\"%s\","
                 "\"msg\":\"no soportado: el programa MAIN_MAIN no usa palabra de control 40070. Usa kinco_pabs, kinco_prel, kinco_home o kinco_jog_stop\"}",
                 cmd);
        return;
    }

    bool is_abs_motion_cmd = (strcmp(cmd, "kinco_pabs") == 0 ||
                              strcmp(cmd, "kinco_start_steps") == 0);
    bool is_rel_motion_cmd = (strcmp(cmd, "kinco_prel") == 0 ||
                              strcmp(cmd, "kinco_move_relative") == 0 ||
                              strcmp(cmd, "kinco_move_delta") == 0);
    bool is_home_cmd = (strcmp(cmd, "kinco_home") == 0 ||
                        strcmp(cmd, "kinco_home_fwd") == 0 ||
                        strcmp(cmd, "kinco_home_bwd") == 0);
    bool is_jog_cmd = (strcmp(cmd, "kinco_jog") == 0 ||
                       strcmp(cmd, "kinco_jog_fwd") == 0 ||
                       strcmp(cmd, "kinco_jog_bwd") == 0);
    bool is_motion_cmd = is_abs_motion_cmd || is_rel_motion_cmd ||
                         is_home_cmd || is_jog_cmd;
    kinco_axis_status_t cur = {};
    if (is_motion_cmd) {
        esp_err_t sr = kinco_axis_read_fast_status(axis, &cur);
        if (sr != ESP_OK) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"target\":\"kinco\","
                     "\"axis\":%d,\"slave\":%u,\"status_read\":\"error\","
                     "\"status_err\":\"%s\",\"exception\":%u}",
                     cmd, axis, KINCO_SLAVE_ID, esp_err_to_name(sr), cur.exception);
            return;
        }
        update_led_from_status(&cur);
        if (kinco_status_cycle_busy(&cur)) {
            bool plc_alive = (cur.status_word >> 15) & 1;
            const char *busy_msg = plc_alive
                ? "ciclo PLC en curso"
                : "MAIN_MAIN no confirma ejecucion: revisar RUN/download PLC";
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"%s\","
                     "\"command_steps\":%ld,\"rel_steps\":%ld,"
                     "\"home_cmd\":%u,\"jog_cmd\":%u,"
                     "\"status\":%u,\"jog_status\":%u,\"plc_alive\":%u,\"pos\":%ld}",
                     cmd, busy_msg, (long)cur.command_steps, (long)cur.rel_steps,
                     (unsigned)cur.home_cmd, (unsigned)cur.jog_cmd,
                     cur.status_word, cur.jog_status_word,
                     (unsigned)plc_alive, (long)cur.position);
            return;
        }
    }

    if (is_abs_motion_cmd) {
        target_pos = has_arg ? (int32_t)arg : KINCO_TEST_STEPS;
        has_target_pos = true;
    } else if (is_rel_motion_cmd) {
        rel_delta = has_arg ? (int32_t)arg : KINCO_REL_TEST_STEPS;
        has_rel_delta = true;
    } else if (is_home_cmd) {
        has_home_request = true;
    } else if (is_jog_cmd) {
        has_jog_request = true;
        long requested_jog = has_speed ? speed_value :
                             (has_arg ? arg : KINCO_DEFAULT_JOG_SPEED);
        if (requested_jog < 0) requested_jog = -requested_jog;
        uint32_t jog_abs = clamp_u32(requested_jog, KINCO_DEFAULT_JOG_SPEED,
                                     KINCO_MIN_FREQ, KINCO_MAX_FREQ);
        bool jog_backward = false;
        if (strcmp(cmd, "kinco_jog_bwd") == 0) {
            jog_backward = true;
        } else if (strcmp(cmd, "kinco_jog_fwd") == 0) {
            jog_backward = false;
        } else if (has_arg && arg < 0) {
            jog_backward = true;
        } else if (has_dir) {
            jog_backward = dir_value != 0;
        }
        jog_speed_cmd = jog_backward ? -(int32_t)jog_abs : (int32_t)jog_abs;
        maxf = jog_abs;
    }

    if (is_motion_cmd) {
        if ((is_abs_motion_cmd && target_pos == 0) ||
            (is_rel_motion_cmd && rel_delta == 0) ||
            (is_jog_cmd && jog_speed_cmd == 0)) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"los pasos no pueden ser 0\"}",
                     cmd);
            return;
        }
        if (is_abs_motion_cmd) {
            r = kinco_axis_pabs(axis, target_pos, maxf, minf, time_ms, &exception_code);
        } else if (is_rel_motion_cmd) {
            r = kinco_axis_prel(axis, rel_delta, maxf, minf, time_ms, &exception_code);
        } else if (is_jog_cmd) {
            r = kinco_axis_jog(axis, jog_speed_cmd, &exception_code);
        } else {
            uint16_t home_dir = KINCO_DEFAULT_HOME_DIR;
            if (strcmp(cmd, "kinco_home_bwd") == 0) {
                home_dir = 1;
            } else if (strcmp(cmd, "kinco_home_fwd") == 0) {
                home_dir = 0;
            } else if (has_dir) {
                home_dir = dir_value ? 1 : 0;
            } else if (has_arg) {
                home_dir = arg ? 1 : 0;
            }

            uint16_t home_mode = KINCO_DEFAULT_HOME_MODE;
            if (has_mode) {
                home_mode = (mode_value == 0) ? 0 : 1;
            }

            uint32_t home_maxf = clamp_u32(has_speed ? speed_value : KINCO_DEFAULT_HOME_MAXF,
                                           KINCO_DEFAULT_HOME_MAXF,
                                           KINCO_MIN_FREQ, KINCO_MAX_FREQ);
            uint16_t home_minf = clamp_u16(has_minf ? minf_value : KINCO_DEFAULT_HOME_MINF,
                                           KINCO_DEFAULT_HOME_MINF,
                                           KINCO_MIN_FREQ, 65535);
            uint16_t home_time_ms = clamp_u16(has_time ? time_value : KINCO_DEFAULT_HOME_TIME,
                                              KINCO_DEFAULT_HOME_TIME, 1, 65535);
            maxf = home_maxf;
            minf = home_minf;
            time_ms = home_time_ms;
            r = kinco_axis_home(axis, home_dir, home_mode, home_maxf, home_minf,
                                home_time_ms, &exception_code);
        }
    } else if (strcmp(cmd, "kinco_cycle_status") == 0) {
        /* Snapshot del ciclo automÃ¡tico + lectura de estado para el contador
         * en vivo. La UI hace polling de este comando mientras el ciclo corre. */
        auto_cycle_state_t snap;
        portENTER_CRITICAL(&s_auto_cycle_mux);
        snap = s_auto_cycle;
        portEXIT_CRITICAL(&s_auto_cycle_mux);

        kinco_axis_status_t st = {};
        esp_err_t sr = kinco_axis_read_fast_status(axis, &st);
        update_led_from_status(&st);

        snprintf(resp, resp_sz,
                 "{\"result\":\"%s\",\"cmd\":\"%s\",\"axis\":%d,\"pos\":%ld,"
                 "\"motor\":{\"ok\":%s,\"err\":\"%s\",\"pos\":%ld,\"control\":%u,"
                 "\"status\":%u,\"status2\":%u,\"status3\":%u,"
                 "\"cycle_active\":%u,\"move_out_active\":%u,\"wait_return_active\":%u,"
                 "\"return_active\":%u,\"cycle_done\":%u,\"cycle_err\":%u,"
                 "\"pabs_out_done\":%u,\"pabs_out_err\":%u,"
                 "\"pabs_return_done\":%u,\"pabs_return_err\":%u,"
                 "\"enable_out\":%u,\"wait_done\":%u,"
                 "\"prel_active\":%u,\"prel_done\":%u,\"prel_err\":%u,"
                 "\"plc_alive\":%u},"
                 "\"cycle\":{\"active\":%s,\"done\":%s,\"phase\":\"%s\","
                 "\"axis\":%d,\"cw_ok\":%s,\"ccw_ok\":%s,\"result\":\"%s\",\"exception\":%u,"
                 "\"cw_target\":%ld,\"fwd_speed\":%lu,\"ret_speed\":%lu,"
                 "\"pos_after_cw\":%ld,\"pos_final\":%ld}}",
                 sr == ESP_OK ? "ok" : "error", cmd, axis, (long)st.position,
                 st.ok ? "true" : "false", esp_err_to_name(sr), (long)st.position,
                 st.control_word,
                 st.status_word, st.status_word2, st.status_word3,
                 (st.status_word >> 0) & 1, (st.status_word >> 1) & 1,
                 (st.status_word >> 2) & 1, (st.status_word >> 3) & 1,
                 (st.status_word >> 4) & 1, (st.status_word >> 5) & 1,
                 (st.status_word >> 6) & 1, (st.status_word >> 7) & 1,
                 (st.status_word >> 8) & 1, (st.status_word >> 9) & 1,
                 (st.status_word >> 10) & 1, (st.status_word >> 11) & 1,
                 (st.status_word >> 12) & 1, (st.status_word >> 13) & 1,
                 (st.status_word >> 14) & 1,
                 (st.status_word >> 15) & 1,
                 snap.active ? "true" : "false", snap.done ? "true" : "false",
                 cycle_phase_str(snap.phase), snap.active ? snap.axis : axis,
                 snap.cw_ok ? "true" : "false", snap.ccw_ok ? "true" : "false",
                 esp_err_to_name(snap.result), snap.exception,
                 (long)snap.cw_target, (unsigned long)snap.fwd_speed,
                 (unsigned long)snap.ret_speed,
                 (long)snap.pos_after_cw, (long)snap.pos_final);
        return;
    } else if (strcmp(cmd, "kinco_auto_cycle") == 0) {
        /* MAIN_MAIN.ilp hace ida, espera 3 s y vuelta a 0 dentro de la PLC.
         * El ESP32 solo lanza un destino distinto de cero y monitorea estado. */
        int32_t cw_target = has_arg ? (int32_t)arg : KINCO_TEST_STEPS;
        uint32_t fwd_speed = clamp_u32(has_speed ? speed_value : KINCO_DEFAULT_PABS_MAXF,
                                       KINCO_DEFAULT_PABS_MAXF,
                                       KINCO_MIN_FREQ, KINCO_MAX_FREQ);
        uint32_t ret_speed = fwd_speed;
        if (cw_target == 0) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"los pasos no pueden ser 0\"}",
                     cmd);
            return;
        }

        kinco_axis_status_t cur = {};
        esp_err_t sr = kinco_axis_read_fast_status(axis, &cur);
        if (sr != ESP_OK) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"target\":\"kinco\","
                     "\"axis\":%d,\"slave\":%u,\"status_read\":\"error\","
                     "\"status_err\":\"%s\",\"exception\":%u}",
                     cmd, axis, KINCO_SLAVE_ID, esp_err_to_name(sr), cur.exception);
            return;
        }
        update_led_from_status(&cur);
        if (kinco_status_cycle_busy(&cur)) {
            snprintf(resp, resp_sz,
                     "{\"result\":\"error\",\"cmd\":\"%s\",\"msg\":\"ciclo PLC en curso\","
                     "\"status\":%u,\"pos\":%ld}",
                     cmd, cur.status_word, (long)cur.position);
            return;
        }

        /* Reservar el ciclo de forma atÃ³mica: si ya hay uno activo, rechazar. */
        bool busy;
        portENTER_CRITICAL(&s_auto_cycle_mux);
        busy = s_auto_cycle.active;
        if (!busy) {
            s_auto_cycle = auto_cycle_state_t{};
            s_auto_cycle.active = true;
            s_auto_cycle.phase = CYCLE_PHASE_CW;
            s_auto_cycle.axis = axis;
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
                 "\"running\":true,\"cw_target\":%ld,\"fwd_speed\":%lu,"
                 "\"ret_speed\":%lu,\"program\":\"" KINCO_PROGRAM_NAME "\"}",
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
    esp_err_t sr = kinco_axis_read_fast_status(axis, &st);
    update_led_from_status(&st);
    snprintf(resp, resp_sz,
             "{\"result\":\"%s\",\"cmd\":\"%s\",\"target\":\"kinco\","
             "\"axis\":%d,\"slave\":%u,\"exception\":%u,\"err\":\"%s\","
             "\"status_read\":\"%s\",\"status_err\":\"%s\","
             "\"status\":%u,\"status2\":%u,\"status3\":%u,"
             "\"home_status\":%u,\"home_error_word\":%u,"
             "\"jog_status\":%u,\"jog_error_word\":%u,"
             "\"pos\":%ld,\"target_pos\":%ld,\"rel_delta\":%ld,"
             "\"home_request\":%u,\"home_cmd\":%u,"
             "\"jog_request\":%u,\"jog_cmd\":%u,\"jog_speed_cmd\":%ld,"
             "\"cycle_active\":%u,\"move_out_active\":%u,\"wait_return_active\":%u,"
             "\"return_active\":%u,\"cycle_done\":%u,\"cycle_err\":%u,"
             "\"pabs_out_done\":%u,\"pabs_out_err\":%u,"
             "\"pabs_return_done\":%u,\"pabs_return_err\":%u,"
             "\"enable_out\":%u,\"wait_done\":%u,"
             "\"prel_active\":%u,\"prel_done\":%u,\"prel_err\":%u,"
             "\"home_active\":%u,\"home_done\":%u,\"home_err\":%u,"
             "\"home_sensor\":%u,\"home_dir\":%u,\"error_home\":%u,"
             "\"jog_active\":%u,\"jog_done\":%u,\"jog_err\":%u,"
             "\"jog_dir\":%u,\"jog_fwd_input\":%u,\"jog_bwd_input\":%u,"
             "\"jog_stop_err\":%u,\"error_jog\":%u,\"error_jog_stop\":%u,"
             "\"plc_alive\":%u,"
             "\"maxf\":%lu,\"minf\":%u,\"time\":%u}",
             r == ESP_OK ? "ok" : "error", cmd, axis, KINCO_SLAVE_ID,
             exception_code, esp_err_to_name(r),
             sr == ESP_OK ? "ok" : "error", esp_err_to_name(sr),
             st.status_word, st.status_word2, st.status_word3,
             st.home_status_word, st.home_error_word,
             st.jog_status_word, st.jog_error_word,
             (long)st.position, has_target_pos ? (long)target_pos : (long)st.position,
             has_rel_delta ? (long)rel_delta : 0L,
             has_home_request ? 1 : 0, (unsigned)st.home_cmd,
             has_jog_request ? 1 : 0, (unsigned)st.jog_cmd, (long)jog_speed_cmd,
             (st.status_word >> 0) & 1, (st.status_word >> 1) & 1,
             (st.status_word >> 2) & 1, (st.status_word >> 3) & 1,
             (st.status_word >> 4) & 1, (st.status_word >> 5) & 1,
             (st.status_word >> 6) & 1, (st.status_word >> 7) & 1,
             (st.status_word >> 8) & 1, (st.status_word >> 9) & 1,
             (st.status_word >> 10) & 1, (st.status_word >> 11) & 1,
             (st.status_word >> 12) & 1, (st.status_word >> 13) & 1,
             (st.status_word >> 14) & 1,
             (st.home_status_word >> 0) & 1, (st.home_status_word >> 1) & 1,
             (st.home_status_word >> 2) & 1, (st.home_status_word >> 3) & 1,
             st.home_dir, (unsigned)(st.home_error_word & 0x00FF),
             (st.jog_status_word >> 0) & 1, (st.jog_status_word >> 1) & 1,
             (st.jog_status_word >> 2) & 1, (st.jog_status_word >> 3) & 1,
             (st.jog_status_word >> 4) & 1, (st.jog_status_word >> 5) & 1,
             (st.jog_status_word >> 7) & 1,
             (unsigned)(st.jog_error_word & 0x00FF),
             (unsigned)((st.jog_error_word >> 8) & 0x00FF),
             (st.status_word >> 15) & 1,
             (unsigned long)maxf, minf, time_ms);
}

static esp_err_t http_get_status_handler(httpd_req_t *req)
{
    char resp[4096];
    kinco_status_response(axis_from_http_req(req), resp, sizeof(resp), "kinco_status");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_get_fast_status_handler(httpd_req_t *req)
{
    char resp[2048];
    kinco_fast_status_response(axis_from_http_req(req), resp, sizeof(resp), "kinco_fast_status");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
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
    ESP_LOGD(TAG, "Comando recibido: %s", buf);

    char resp[4096];
    dispatch_command(buf, resp, sizeof(resp));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
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
        "<h1>âš™ï¸ NEMA23 Gateway</h1>"
        "<div class='status'><strong>Estado:</strong> <span id='status'>Online</span><br>"
        "<strong>PosiciÃ³n:</strong> <span id='pos'>0</span> steps</div>"
        "<div class='plc'><strong>PLC:</strong> <span id='plc_status'>Sin lectura</span></div>"
        "<div class='row'>"
        "<button onclick='send(\"move_rel\",1000)'>â–¶ +1000</button>"
        "<button onclick='send(\"move_rel\",-1000)'>â—€ -1000</button>"
        "</div>"
        "<div class='row'>"
        "<button onclick='send(\"home\")'>ðŸ  Home</button>"
        "<button onclick='send(\"stop\")'>â¹ Stop</button>"
        "<button onclick='send(\"estop\")'>ðŸ›‘ E-Stop</button>"
        "</div>"
        "<div class='row' style='margin-top:8px'>"
        "<input id='speed' value='5000' placeholder='steps/s'>"
        "<button onclick='send(\"run_speed\",document.getElementById(\"speed\").value)'>âš¡ Run</button>"
        "</div>"
        "<div class='row' style='margin-top:8px'>"
        "<button onclick='send(\"rs485_mode\")'>ðŸ”€ RS485</button>"
        "<button onclick='send(\"can_mode\")'>ðŸ”€ CAN</button>"
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
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_get_kinco_root_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Kinco 2 motores PABS PREL HOME JOG</title>"
        "<style>"
        ":root{color-scheme:dark}*{box-sizing:border-box}"
        "body{font-family:Arial,sans-serif;margin:0;background:#171717;color:#e7e5e4}"
        "main{max-width:920px;margin:0 auto;padding:14px}"
        "h1{font-size:22px;margin:0 0 5px;color:#f9fafb}"
        ".sub{color:#a8a29e;font-size:13px;margin:0 0 12px}"
        "section{border:1px solid #3f3f46;border-radius:8px;padding:12px;margin:10px 0;background:#242424}"
        ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
        "button{background:#0f766e;color:#fff;border:0;border-radius:6px;padding:11px 14px;cursor:pointer;min-height:40px;font-weight:700}"
        "button:hover{background:#0d9488}button:disabled{background:#525252;cursor:not-allowed}"
        ".move{font-size:17px;min-width:165px}.secondary{background:#57534e}.danger{background:#dc2626}"
        ".ok{color:#2dd4bf}.bad{color:#fb7185}.warn{color:#fbbf24}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px}"
        ".kv{display:grid;grid-template-columns:1fr auto;gap:5px 12px;font-size:13px}"
        ".kv span:nth-child(odd){color:#a8a29e}.mono{font-family:Consolas,monospace}"
        "label{color:#a8a29e;font-size:13px;display:flex;align-items:center;gap:5px}"
        "input,select{background:#171717;color:#e7e5e4;border:1px solid #57534e;border-radius:6px;padding:8px 9px;width:92px;font-size:14px;text-align:center}select{width:116px;text-align:left}"
        ".counter{background:#171717;border:2px solid #3f3f46;border-radius:8px;padding:14px;text-align:center}"
        ".counter .big{font:700 52px/1 Consolas,monospace;color:#2dd4bf}"
        ".counter .label{font-size:11px;color:#78716c;text-transform:uppercase;letter-spacing:1px;margin-bottom:5px}"
        ".status{font-family:Consolas,monospace;white-space:pre-wrap;overflow:auto;max-height:210px;background:#171717;border-radius:6px;padding:8px}"
        ".muted{color:#a8a29e;font-size:12px}"
        "</style></head><body><main>"
        "<h1>Kinco 2 motores PABS/PREL/HOME/JOG</h1>"
        "<p class='sub'>Motor 1: STEP Q0.0 DIR Q0.2 EN Q0.4. Motor 2: STEP Q0.1 DIR Q0.3 EN Q0.5. Enable activo bajo.</p><section><div class='row'><label>Motor<select id='axis' onchange='onAxisChange()'><option value='0'>Motor 1</option><option value='1'>Motor 2</option></select></label><span class='muted' id='axisMap'>M1 PABS 40151, POS 40201, STATUS 40252</span></div></section>"
        "<div class='counter'><div class='label' id='posLabel'>Posicion actual 40201</div><div class='big' id='pos'>0</div>"
        "<div class='muted' id='phase'>Sin lectura</div></div>"
        "<section><div class='row'>"
        "<button class='move' onclick='startSteps(5000)'>+5000 y volver</button>"
        "<button class='move' onclick='startSteps(30000)'>+30000 y volver</button>"
        "<button class='move secondary' onclick='startSteps(-5000)'>-5000 y volver</button>"
        "<button class='secondary' onclick='readStates(true)'>Leer estados</button>"
        "<button class='secondary' onclick='api({cmd:\"kinco_clear_command\"})'>Limpiar comandos</button>"
        "</div></section>"
        "<section><div class='row'>"
        "<button class='move' onclick='startRel(7000)'>+7000 relativo</button>"
        "<button class='move secondary' onclick='startRel(-7000)'>-7000 relativo</button>"
        "</div><p class='muted'>PREL mueve la distancia indicada desde la posicion actual y no vuelve a cero.</p></section>"
        "<section><div class='row'>"
        "<button class='move' onclick='startHome(0)'>HOME forward</button>"
        "<button class='move secondary' onclick='startHome(1)'>HOME backward</button>"
        "</div><p class='muted'>PHOME usa I0.0 en motor 1 e I0.3 en motor 2. Los registros se actualizan segun el selector.</p></section>"
        "<section><div class='row'>"
        "<button class='move' onmousedown='jogStart(1)' onmouseup='jogStop()' onmouseleave='jogStop()' ontouchstart='jogTouch(event,1)' ontouchend='jogTouch(event,0)' ontouchcancel='jogTouch(event,0)'>JOG forward</button>"
        "<button class='move secondary' onmousedown='jogStart(-1)' onmouseup='jogStop()' onmouseleave='jogStop()' ontouchstart='jogTouch(event,-1)' ontouchend='jogTouch(event,0)' ontouchcancel='jogTouch(event,0)'>JOG backward</button>"
        "<button class='danger' onclick='jogStop(true)'>STOP JOG</button>"
        "</div><p class='muted'>JOG fisico: motor 1 I0.1/I0.2; motor 2 I0.4/I0.5. JOG web usa velocidad Max Hz.</p></section>"
        "<section><div class='row'>"
        "<label>Pasos<input id='steps' type='number' value='5000' min='1' max='999999' step='1000'></label>"
        "<label>Max Hz<input id='maxf' type='number' value='2000' min='125' max='200000' step='100'></label>"
        "<label>Min Hz<input id='minf' type='number' value='300' min='125' max='65535' step='25'></label>"
        "<label>Accel<input id='time' type='number' value='300' min='1' max='65535' step='50'></label>"
        "<button onclick='startCustom(1)'>Enviar +pasos</button>"
        "<button onclick='startCustom(-1)'>Enviar -pasos</button>"
        "<button onclick='startRelCustom(1)'>PREL +pasos</button>"
        "<button onclick='startRelCustom(-1)'>PREL -pasos</button>"
        "</div><p class='muted'>Motor 1 usa 40151/40157/40167/40175. Motor 2 usa 40301/40307/40317/40325.</p></section>"
        "<section><div class='grid'>"
        "<div><strong>Registros</strong><div class='kv' id='regs'>Sin lectura</div></div>"
        "<div><strong id='bitsTitle'>Bits 40252</strong><div class='kv' id='bits'>Sin lectura</div></div>"
        "</div></section>"
        "<section><strong>Respuesta</strong><pre class='status' id='log'>Listo</pre><div class='muted'>UI MAIN_MAIN test_2 v3 dos motores</div></section>"
        "</main><script>"
        "let running=false,pollId=null,pollTicks=0,targetAbs=0,statusBusy=false,fastBusy=false,cmdBusy=false,jogHeld=false,axis=0,meta={};"
        "function q(id){return document.getElementById(id)}function selectedAxis(){let e=q('axis');let v=e?parseInt(e.value):axis;return Number.isFinite(v)?v:0}function reg(k,d){return meta&&meta[k]?meta[k]:d}function axisDefaults(a){return a?{command_reg:40301,maxf_reg:40303,minf_reg:40305,time_reg:40306,home_cmd_reg:40307,home_dir_reg:40309,home_minf_reg:40310,home_maxf_reg:40311,home_time_reg:40313,rel_reg:40317,prel_maxf_reg:40319,prel_minf_reg:40321,prel_time_reg:40322,jog_cmd_reg:40325,jog_dir_reg:40326,jog_speed_reg:40327,position_reg:40351,status_reg:40402,error_out_reg:40403,error_return_reg:40404,home_status_reg:40405,error_home_reg:40406,jog_status_reg:40407,error_jog_reg:40408}:{command_reg:40151,maxf_reg:40153,minf_reg:40155,time_reg:40156,home_cmd_reg:40157,home_dir_reg:40159,home_minf_reg:40160,home_maxf_reg:40161,home_time_reg:40163,rel_reg:40167,prel_maxf_reg:40169,prel_minf_reg:40171,prel_time_reg:40172,jog_cmd_reg:40175,jog_dir_reg:40176,jog_speed_reg:40177,position_reg:40201,status_reg:40252,error_out_reg:40253,error_return_reg:40254,home_status_reg:40255,error_home_reg:40256,jog_status_reg:40257,error_jog_reg:40258}}function applyMeta(j){if(!j)return;axis=(j.axis!==undefined)?j.axis:selectedAxis();meta=Object.assign({},axisDefaults(axis),meta,j);let ax=q('axis');if(ax)ax.value=axis;let pos=meta.position_reg;q('posLabel').textContent='Motor '+(axis+1)+' posicion '+pos;let st=meta.status_reg;q('bitsTitle').textContent='Bits '+st;let m=q('axisMap');if(m)m.textContent=axis?'M2 PABS 40301, HOME 40307, PREL 40317, JOG 40325, POS 40351, STATUS 40402':'M1 PABS 40151, HOME 40157, PREL 40167, JOG 40175, POS 40201, STATUS 40252'}function onAxisChange(){axis=selectedAxis();running=false;jogHeld=false;if(pollId){clearInterval(pollId);pollId=null}applyMeta({axis:axis,position_reg:axis?40351:40201,status_reg:axis?40402:40252});getStatus(false)}"
        "function hx(v){return '0x'+(v||0).toString(16).padStart(4,'0')}"
        "function hxb(v){return '0x'+(v||0).toString(16).padStart(2,'0')}"
        "function cls(v){return v?'ok':'bad'}"
        "function num(id,def){let v=parseInt(q(id).value);return Number.isFinite(v)?v:def}"
        "function cfg(){return {speed:num('maxf',2000),minf:num('minf',300),time:num('time',300)}}"
        "async function getStatus(show=true){if(statusBusy)return null;statusBusy=true;let ac=new AbortController();let t=setTimeout(()=>ac.abort(),5000);try{let r=await fetch('/api/status?axis='+selectedAxis(),{cache:'no-store',signal:ac.signal});let j=await r.json();applyMeta(j);if(show)q('log').textContent=JSON.stringify(j,null,2);if(j.motor)paint(j.motor);return j}catch(e){let j={result:'error',cmd:'kinco_status',msg:e.name};q('log').textContent=JSON.stringify(j,null,2);return j}finally{clearTimeout(t);statusBusy=false}}"
        "async function getFastStatus(show=false){if(fastBusy)return null;fastBusy=true;let ac=new AbortController();let t=setTimeout(()=>ac.abort(),1800);try{let r=await fetch('/api/fast_status?axis='+selectedAxis(),{cache:'no-store',signal:ac.signal});let j=await r.json();applyMeta(j);if(show)q('log').textContent=JSON.stringify(j,null,2);if(j.motor)paintFast(j.motor);return j}catch(e){if(show)q('log').textContent=JSON.stringify({result:'error',cmd:'kinco_fast_status',msg:e.name},null,2);return null}finally{clearTimeout(t);fastBusy=false}}"
        "async function api(b,force=false){if(cmdBusy&&!force)return {result:'error',msg:'comando en curso'};if(!force)cmdBusy=true;let ac=new AbortController();let t=setTimeout(()=>ac.abort(),7000);try{b.axis=selectedAxis();let r=await fetch('/api/command',{method:'POST',cache:'no-store',headers:{'Content-Type':'application/json','Connection':'close'},body:JSON.stringify(b),signal:ac.signal});let j=await r.json();applyMeta(j);q('log').textContent=JSON.stringify(j,null,2);if(j.motor){if(j.fast)paintFast(j.motor);else paint(j.motor)}return j}catch(e){let j={result:'error',cmd:b.cmd,msg:e.name};q('log').textContent=JSON.stringify(j,null,2);return j}finally{clearTimeout(t);if(!force)cmdBusy=false}}"
        "function active(m){return !!(m&&(m.cycle_active||m.move_out_active||m.wait_return_active||m.return_active||m.prel_active||m.home_active||m.jog_active))}"
        "function failed(m){return !!(m&&(m.cycle_err||m.pabs_out_err||m.pabs_return_err||m.prel_err||m.home_err||m.jog_err||m.jog_stop_err))}"
        "function phase(m){if(!m)return 'Sin lectura';if(failed(m))return 'Error';if(m.jog_active)return 'JOG '+(m.jog_dir_bit?'backward':'forward');if(m.home_active)return 'Buscando HOME';if(m.prel_active)return 'Moviendo relativo';if(m.move_out_active)return 'Moviendo a destino';if(m.wait_return_active)return 'Esperando 3 s';if(m.return_active)return 'Volviendo a cero';if(m.cycle_active)return 'Ciclo activo';if(m.jog_done)return 'JOG terminado';if(m.home_done)return 'HOME terminado';if(m.prel_done)return 'Relativo terminado';if(m.cycle_done)return 'Terminado';return 'Idle'}"
        "function paintFast(m){q('pos').textContent=(m.pos||0).toLocaleString();q('phase').textContent=phase(m)}"
        "function paint(m){let dbg=(m.debug_return!==undefined)?m.debug_return:((m.status3||0)>>8);let en=(dbg>>7)&1;let pto=(dbg>>6)&1;let ax=selectedAxis();let drv=dbg?en?0:1:m.enable_out;"
        "q('pos').textContent=(m.pos||0).toLocaleString();q('phase').textContent=phase(m);"
        "q('regs').innerHTML='<span>Link</span><span class='+cls(m.ok)+'>'+(m.ok?'OK':'ERR '+m.err)+'</span>'"
        "+'<span>Comando '+reg('command_reg',40151)+'</span><span class=mono>'+m.command_steps+'</span>'"
        "+'<span>Max '+reg('maxf_reg',40153)+'</span><span class=mono>'+m.maxf+'</span>'"
        "+'<span>Min '+reg('minf_reg',40155)+'</span><span class=mono>'+m.minf+'</span>'"
        "+'<span>Accel '+reg('time_reg',40156)+'</span><span class=mono>'+m.time+'</span>'"
        "+'<span>HOME cmd '+reg('home_cmd_reg',40157)+'</span><span class=mono>'+m.home_cmd+'</span>'"
        "+'<span>HOME dir '+reg('home_dir_reg',40159)+'</span><span class=mono>'+m.home_dir+'</span>'"
        "+'<span>HOME max '+reg('home_maxf_reg',40161)+'</span><span class=mono>'+m.home_maxf+'</span>'"
        "+'<span>HOME min '+reg('home_minf_reg',40160)+'</span><span class=mono>'+m.home_minf+'</span>'"
        "+'<span>HOME accel '+reg('home_time_reg',40163)+'</span><span class=mono>'+m.home_time+'</span>'"
        "+'<span>Rel '+reg('rel_reg',40167)+'</span><span class=mono>'+m.rel_steps+'</span>'"
        "+'<span>PREL max '+reg('prel_maxf_reg',40169)+'</span><span class=mono>'+m.prel_maxf+'</span>'"
        "+'<span>PREL min '+reg('prel_minf_reg',40171)+'</span><span class=mono>'+m.prel_minf+'</span>'"
        "+'<span>PREL accel '+reg('prel_time_reg',40172)+'</span><span class=mono>'+m.prel_time+'</span>'"
        "+'<span>JOG cmd '+reg('jog_cmd_reg',40175)+'</span><span class=mono>'+m.jog_cmd+'</span>'"
        "+'<span>JOG dir '+reg('jog_dir_reg',40176)+'</span><span class=mono>'+m.jog_dir+'</span>'"
        "+'<span>JOG speed '+reg('jog_speed_reg',40177)+'</span><span class=mono>'+m.jog_speed+'</span>'"
        "+'<span>Estado '+reg('status_reg',40252)+'</span><span class=mono>'+hx(m.status)+'</span>'"
        "+'<span>Error ida '+reg('error_out_reg',40253)+'</span><span class=mono>'+hxb(m.error_out)+'</span>'"
        "+'<span>Error PREL '+reg('error_return_reg',40254)+'L</span><span class=mono>'+hxb(m.error_return)+'</span>'"
        "+'<span>HOME estado '+reg('home_status_reg',40255)+'</span><span class=mono>'+hx(m.home_status)+'</span>'"
        "+'<span>Error HOME '+reg('error_home_reg',40256)+'L</span><span class=mono>'+hxb(m.error_home)+'</span>'"
        "+'<span>JOG estado '+reg('jog_status_reg',40257)+'</span><span class=mono>'+hx(m.jog_status)+'</span>'"
        "+'<span>Error JOG '+reg('error_jog_reg',40258)+'L</span><span class=mono>'+hxb(m.error_jog)+'</span>'"
        "+'<span>Error PSTOP JOG '+reg('error_jog_reg',40258)+'H</span><span class=mono>'+hxb(m.error_jog_stop)+'</span>'"
        "+'<span>Debug '+reg('error_return_reg',40254)+'H</span><span class=mono>'+hxb(dbg)+'</span>'"
        "+'<span>Enable logico</span><span class='+cls(m.enable_out)+'>'+m.enable_out+'</span>'"
        "+'<span>'+((ax)?'Q0.5':'Q0.4')+' fisico</span><span class=mono>'+en+' '+(en?'OFF':'ON')+'</span>'"
        "+'<span>Driver energizado</span><span class='+cls(drv)+'>'+drv+'</span>'"
        "+'<span>'+((ax)?'PTO1 SM76.7':'PTO0 SM66.7')+'</span><span class='+cls(pto)+'>'+pto+'</span>'"
        "+'<span>PLC Alive</span><span class='+cls(m.plc_alive)+'>'+m.plc_alive+'</span>';"
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
        "+'<span>b10 Enable logico</span><span>'+m.enable_out+'</span>'"
        "+'<span>b11 WaitDone</span><span>'+m.wait_done+'</span>'"
        "+'<span>b12 PrelActive</span><span>'+m.prel_active+'</span>'"
        "+'<span>b13 PrelDone</span><span>'+m.prel_done+'</span>'"
        "+'<span>b14 PrelErr</span><span class='+cls(!m.prel_err)+'>'+m.prel_err+'</span>'"
        "+'<span>b15 PLC Alive</span><span class='+cls(m.plc_alive)+'>'+m.plc_alive+'</span>'"
        "+'<span>'+reg('home_status_reg',40255)+'.0 HomeActive</span><span>'+m.home_active+'</span>'"
        "+'<span>'+reg('home_status_reg',40255)+'.1 HomeDone</span><span>'+m.home_done+'</span>'"
        "+'<span>'+reg('home_status_reg',40255)+'.2 HomeErr</span><span class='+cls(!m.home_err)+'>'+m.home_err+'</span>'"
        "+'<span>'+reg('home_status_reg',40255)+'.3 HomeSensor</span><span class='+cls(m.home_sensor)+'>'+m.home_sensor+'</span>'"
        "+'<span>'+reg('home_status_reg',40255)+'.4 HomeDir</span><span>'+m.home_dir_bit+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.0 JogActive</span><span>'+m.jog_active+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.1 JogDone</span><span>'+m.jog_done+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.2 JogErr</span><span class='+cls(!m.jog_err)+'>'+m.jog_err+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.3 JogDir</span><span>'+m.jog_dir_bit+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.4 '+(selectedAxis()?'I0.4':'I0.1')+' Fwd</span><span class='+cls(m.jog_fwd_input)+'>'+m.jog_fwd_input+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.5 '+(selectedAxis()?'I0.5':'I0.2')+' Bwd</span><span class='+cls(m.jog_bwd_input)+'>'+m.jog_bwd_input+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.6 CmdWeb</span><span>'+m.jog_cmd_active+'</span>'"
        "+'<span>'+reg('jog_status_reg',40257)+'.7 StopErr</span><span class='+cls(!m.jog_stop_err)+'>'+m.jog_stop_err+'</span>';}"
        "async function readStates(show){return show?await getStatus(true):await getFastStatus(false)}"
        "function startPoll(){if(pollId)clearInterval(pollId);pollTicks=0;pollId=setInterval(async()=>{pollTicks++;try{let j=await getFastStatus(false);if(!j)return;let m=j.motor;if(m&&(failed(m)||(!active(m)&&m.cycle_done&&pollTicks>2))){clearInterval(pollId);pollId=null;running=false;getStatus(false)}}catch(e){}},300)}"
        "async function startSteps(steps){if(running)return;let c=cfg();targetAbs=Math.abs(steps);running=true;q('phase').textContent='Enviando '+steps+' pasos';let j=await api({cmd:'kinco_pabs',axis:0,arg:steps,speed:c.speed,minf:c.minf,time:c.time});if(j.result!=='ok'){running=false;return}startPoll()}"
        "async function startRel(steps){if(running)return;let c=cfg();targetAbs=Math.abs(steps);running=true;q('phase').textContent='PREL '+steps+' pasos';let j=await api({cmd:'kinco_prel',axis:0,arg:steps,speed:c.speed,minf:c.minf,time:c.time});if(j.result!=='ok'){running=false;return}startPoll()}"
        "async function startHome(dir){if(running)return;let c=cfg();running=true;q('phase').textContent='HOME '+(dir?'backward':'forward');let j=await api({cmd:'kinco_home',axis:0,arg:dir,dir:dir,mode:1,speed:c.speed,minf:c.minf,time:c.time});if(j.result!=='ok'){running=false;return}startPoll()}"
        "async function jogStart(dir){if(jogHeld||running)return;let c=cfg();jogHeld=true;running=true;q('phase').textContent='JOG '+(dir>0?'forward':'backward');let j=await api({cmd:dir>0?'kinco_jog_fwd':'kinco_jog_bwd',axis:0,speed:c.speed});if(j.result!=='ok'){jogHeld=false;running=false;return}if(!jogHeld){await api({cmd:'kinco_jog_stop',axis:0},true);running=false;return}startPoll()}"
        "async function jogStop(force=false){if(!jogHeld&&!force)return;jogHeld=false;let j=await api({cmd:'kinco_jog_stop',axis:0},true);running=false;if(pollId){clearInterval(pollId);pollId=null}await readStates(false);return j}"
        "function jogTouch(e,dir){e.preventDefault();if(dir)jogStart(dir);else jogStop()}"
        "function startCustom(sign){let s=Math.abs(num('steps',5000));startSteps(sign*s)}"
        "function startRelCustom(sign){let s=Math.abs(num('steps',7000));startRel(sign*s)}"
        "setInterval(()=>{if(!running&&!fastBusy&&!statusBusy)getFastStatus(false)},1000);getStatus(false);"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "close");
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
        "<div class='label'>Contador de pasos â€” Posicion actual</div>"
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
        "ðŸ”„ Auto Cycle 0â†’Nâ†’0</button>"
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
        "btn.disabled=true;btn.textContent='â³ Ciclo en curso...';btn.style.background='#16a34a';"
        "log.textContent='ðŸš€ Iniciando: 0 â†’ '+steps+' CW @'+fwdHz+'Hz / CCW @'+ccwHz+'Hz ...';"
        /* Lanzar el ciclo (responde de inmediato). Si falla, abortar. */
        "try{let b={cmd:'kinco_auto_cycle',axis:0,arg:steps,speed:fwdHz,minf:ccwHz,time:300,dir:dir};"
        "let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
        "let j=await r.json();if(j.result!=='ok'){throw new Error(j.msg||j.err||'no se pudo iniciar')}}"
        "catch(e){btn.style.background='#dc2626';log.textContent='âŒ '+e.message;"
        "btn.disabled=false;btn.textContent='ðŸ”„ Auto Cycle 0â†’Nâ†’0';cycleRunning=false;cycleTarget=0;return}"
        /* Sondear progreso sin bloquear; finalizar cuando cycle.active=false. */
        "fastPollId=setInterval(async()=>{"
        "try{let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({cmd:'kinco_cycle_status'})});"
        "let j=await r.json();if(j.motor){paint(j.motor);updateCounter(j.motor.pos,cycleTarget)}"
        "if(j.cycle&&!j.cycle.active){"
        "clearInterval(fastPollId);fastPollId=null;cycleRunning=false;cycleTarget=0;"
        "btn.disabled=false;btn.textContent='ðŸ”„ Auto Cycle 0â†’Nâ†’0';"
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
    {.uri = "/api/fast_status", .method = HTTP_GET, .handler = http_get_fast_status_handler, .user_ctx = nullptr},
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
 * API PÃºblica
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

    /* Configurar AP si estÃ¡ habilitado */
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

    /* Configurar STA si estÃ¡ habilitado y tiene SSID */
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
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.keep_alive_enable = false;
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
