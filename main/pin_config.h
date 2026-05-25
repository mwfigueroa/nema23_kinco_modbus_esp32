/**
 * pin_config.h — GPIO mapping for LILYGO T-CAN485 + NEMA23
 *
 * Define todos los pines usados por el hardware:
 * - RS485 (MAX13487)
 * - CAN bus (SN65HVD231 via TWAI)
 * - WS2812B RGB LED
 * - NEMA23 stepper (STEP/DIR/EN)
 * - UART de debug
 * - Boost converter enable
 */

#pragma once

#include "hal/uart_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * RS485 (MAX13487EESA+) — Conectado a UART2
 * ================================================================ */
/* Pines fijos del MAX13487EESA+ en la placa LILYGO T-CAN485.
 * Verificado contra el repo oficial Xinyuan-LilyGO/T-CAN485 (config.h):
 *   RS485_EN_PIN=17 (/RE), RS485_TX=22, RS485_RX=21, RS485_SE/SHDN=19.
 * Ambos pines de control deben quedar en HIGH para sacar al transceiver del
 * shutdown; el MAX13487 maneja la dirección TX/RX automáticamente. */
#define RS485_UART_PORT         UART_NUM_2
#define RS485_TX_GPIO           22
#define RS485_RX_GPIO           21
#define RS485_EN_GPIO           17      // /RE receiver-enable (HIGH = activo)
#define RS485_SE_GPIO           19      // SHDN del MAX13487 (HIGH = activo)
#define RS485_BAUD              115200
#define RS485_UART_BUF_SIZE     1024

/* ================================================================
 * CAN Bus (SN65HVD231) — TWAI
 * ================================================================ */
#define CAN_TX_GPIO             27
#define CAN_RX_GPIO             26
#define CAN_SPEED_MODE_GPIO     23      // Rs/SE del SN65HVD231: LOW = high-speed, HIGH = standby
#define CAN_BAUD_RATE           500000  // 500 kbps típico

/* ================================================================
 * Me2107 Boost Converter Enable
 * ================================================================ */
#define BOOST_EN_GPIO           16      // HIGH para habilitar 5V boost

/* ================================================================
 * WS2812B RGB LED
 * ================================================================ */
#define WS2812B_DATA_GPIO       4
#define WS2812B_LED_COUNT       1

/* ================================================================
 * NEMA23 Stepper Motor (via FastAccelStepper + RMT)
 * Conectar a driver externo (e.g., DM542 o TB6600)
 * ================================================================ */
#define STEPPER_STEP_GPIO       5       // STEP — RMT channel
#define STEPPER_DIR_GPIO        18      // DIR
#define STEPPER_EN_GPIO         25      // ENABLE (activo bajo típicamente)
#define STEPPER_RMT_RESOLUTION  10000000 // 10 MHz RMT tick

/* Pines opcionales para finales de carrera */
#define LIMIT_SW_MIN_GPIO       32
#define LIMIT_SW_MAX_GPIO       33

/* ================================================================
 * UART de Debug (conector USB-UART externo)
 * ================================================================ */
#define DEBUG_UART_PORT         UART_NUM_0
#define DEBUG_TX_GPIO           1
#define DEBUG_RX_GPIO           3
#define DEBUG_BAUD              115200

/* ================================================================
 * Indicador de estado (opcional)
 * ================================================================ */
#define STATUS_LED_GPIO         2       // Compartido con SD_MISO si no usás SD

#ifdef __cplusplus
}
#endif
