/**
 * pin_config.h — GPIO mapping for LILYGO T-CAN485 + NEMA23
 *
 * Define todos los pines usados por el hardware:
 * - RS485 (MAX13487)
 * - CAN bus (SN65HVD231 via TWAI)
 * - WS2812B RGB LED
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
 * Salidas de relé ("topes") — controladas por Modbus coils
 * GPIO 13 y 14: libres en el ESP32 y sin restricciones de strapping.
 * Manejan la señal de control de un módulo de relé (3.3 V, ~20 mA máx);
 * para cargas inductivas usar el driver/optoacoplador del módulo de relé.
 * ================================================================ */
#define RELAY1_GPIO             13      // Relé 1 — coil Modbus 0
#define RELAY2_GPIO             14      // Relé 2 — coil Modbus 1
#define RELAY_COUNT             2
#define RELAY_ACTIVE_LEVEL      1       // 1 = activo-alto (GPIO HIGH activa el relé)

/* ================================================================
 * UART de Debug (conector USB-UART externo)
 * ================================================================ */
#define DEBUG_UART_PORT         UART_NUM_0
#define DEBUG_TX_GPIO           1
#define DEBUG_RX_GPIO           3
#define DEBUG_BAUD              115200

#ifdef __cplusplus
}
#endif
