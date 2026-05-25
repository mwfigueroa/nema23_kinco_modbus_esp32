# NEMA23 LILYGO T-CAN485 Gateway

Controlador de motor paso a paso **NEMA23** + **gateway industrial** basado en
**ESP32 LILYGO T-CAN485**, con puente **Modbus TCP <-> RS485**, **CAN bus** y
**WiFi**.

## Hardware

| Componente | Especificación |
|-----------|---------------|
| **MCU** | ESP32 (LILYGO T-CAN485) |
| **Motor** | NEMA23 con driver externo (DM542/TB6600) |
| **RS485** | MAX13487EESA+ (half-duplex) |
| **CAN** | SN65HVD231 (TWAI) |
| **WiFi** | 802.11 b/g/n (AP + STA) |
| **LED** | WS2812B RGB (GPIO 4) |

## Pinout

| Función | GPIO | Notas |
|---------|------|-------|
| **STEP** | 5 | RMT channel |
| **DIR** | 18 | Dirección motor |
| **EN** | 25 | Enable driver |
| **RS485 TX** | 22 | UART2 |
| **RS485 RX** | 21 | UART2 |
| **RS485 EN** | 17 | MAX13487 /RE — debe ir HIGH para activar el transceiver |
| **RS485 SE** | 19 | MAX13487 SHDN — debe ir HIGH para activar el transceiver |
| **CAN TX** | 27 | TWAI |
| **CAN RX** | 26 | TWAI |
| **CAN SE** | 23 | SN65HVD231 Rs — **LOW = high-speed**, HIGH = standby |
| **BOOST EN** | 16 | ME2107 / PIN_5V_EN — HIGH para habilitar 5V |
| **WS2812B** | 4 | LED RGB |
| **LIMIT MIN** | 32 | Final de carrera |
| **LIMIT MAX** | 33 | Final de carrera |

## Quick Start

### 0. Verificar entorno ESP-IDF (opcional pero recomendado)

```powershell
.\scripts\doctor.ps1
```

### 1. Compilar

```powershell
.\scripts\build.ps1
```

Por defecto el proyecto compila en `build_marti/`. Si querés usar otro
directorio:

```powershell
.\scripts\build.ps1 -BuildDir build
```

### 2. Flashear y monitorear

```powershell
.\scripts\flash_monitor.ps1 -Port COM3
```

### 3. Conectarse

El firmware arranca en modo **AP + STA simultáneo**:

| Vía | SSID / Red | IP de la placa | Notas |
|-----|-----------|----------------|-------|
| **AP propio (siempre activo)** | `NEMA23_Gateway` / `12345678` | `192.168.4.1` | Acceso directo, ideal como backup |
| **STA (cliente de red existente)** | configurado en `main/main.cpp` | DHCP del router | Para acceso desde la red de oficina/laboratorio |

- **Panel web**: `http://<IP>/`
- **Modbus TCP**: `<IP>:502` -> puente a RS485
- **API REST**: ver sección "API HTTP" abajo

> **Cambiar SSID/password de STA**: editar las líneas con
> `strcpy(wifi_cfg.sta_*)` en [main/main.cpp](main/main.cpp) y recompilar. La
> IP asignada por DHCP se loguea en el monitor serie como
> `wifi_mgr: WiFi STA IP: x.x.x.x`.

### Alimentación

La placa **no arranca correctamente alimentada solo por USB de PC**: el inrush
del boost converter ME2107 (GPIO 16) hace caer la tensión y dispara el brownout
detector. Solución: alimentar por el **terminal de tornillo de 2 pines (5-12V)**
que la placa trae cerca del USB-C, y dejar el USB para datos/monitor.

## API HTTP

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| `GET` | `/` | Panel de control HTML |
| `GET` | `/api/status` | Estado del sistema (JSON) |
| `POST` | `/api/command` | Enviar comando (JSON) |

### Comandos JSON

```json
{"cmd": "move_to",     "arg": 10000}
{"cmd": "move_rel",    "arg": -500}
{"cmd": "run_speed",   "arg": 5000}
{"cmd": "stop"}
{"cmd": "estop"}
{"cmd": "home"}
{"cmd": "enable",      "arg": 1}
{"cmd": "rs485_mode"}
{"cmd": "can_mode"}
```

## Modos de puente

- **Modbus TCP -> RS485**: las tramas Modbus TCP recibidas por WiFi se forwardean al bus RS485.
- **RAW TCP -> RS485**: puente transparente.
- **Local Only**: solo comandos al motor NEMA23.
- **CAN**: mensajes CAN bus independientes.

## Estructura del proyecto

```text
nema23_lilygo/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp                 <- Entry point
│   ├── pin_config.h             <- GPIO mapping
│   ├── wifi_manager.h/cpp       <- WiFi AP/STA + HTTP
│   ├── bridge_rs485.h/cpp       <- Modbus TCP <-> RS485
│   ├── can_bus.h/cpp            <- CAN bus (TWAI)
│   ├── stepper_control.h/cpp    <- NEMA23 via FastAccelStepper
│   └── stepper_motor_encoder.c/h <- RMT encoder
├── components/
│   └── FastAccelStepper/        <- Librería de aceleración
└── scripts/
    ├── build.ps1
    ├── flash_monitor.ps1
    ├── ensure_idf.ps1
    └── doctor.ps1               <- Verifica entorno ESP-IDF
```

## Requisitos

- **ESP-IDF** v5.5.1 (o superior)
- **FastAccelStepper** incluido en `components/`
- Driver NEMA23 externo (DM542, TB6600 o similar)
- Fuente de alimentación adecuada para el motor

## Estado de revisión (2026-05-25)

Revisión técnica de factibilidad. El proyecto compila (`build_marti/nema23_lilygo.bin`)
y la arquitectura es sólida. Se corrigieron los siguientes **bugs bloqueantes**
que impedían que el hardware funcionara; el pinout fue verificado contra el repo
oficial [Xinyuan-LilyGO/T-CAN485](https://github.com/Xinyuan-LilyGO/T-CAN485).

### Correcciones aplicadas

1. **CAN no transmitía — pin SE invertido.** `CAN_SE` (GPIO 23) se ponía en HIGH,
   lo que deja al SN65HVD231 en *standby*. Ahora se pone en **LOW** (high-speed),
   igual que el ejemplo oficial. → [`can_bus.cpp`](main/can_bus.cpp)
2. **RS485 no transmitía — transceiver en shutdown.** Los pines de habilitación
   del MAX13487 nunca se manejaban y estaban mal etiquetados. Pinout real:
   `RS485_EN`=GPIO17 (/RE), `RS485_SE`=GPIO19 (SHDN); **ambos deben ir HIGH**.
   Se corrigieron las etiquetas en [`pin_config.h`](main/pin_config.h) y se
   habilita el transceiver en [`bridge_rs485.cpp`](main/bridge_rs485.cpp). Además
   se cambió de `UART_MODE_RS485_HALF_DUPLEX` a `UART_MODE_UART` porque el
   MAX13487 conmuta la dirección por hardware (auto-direction).
3. **Puente Modbus inservible — faltaba CRC16.** Las tramas Modbus TCP no llevan
   CRC, pero RTU lo exige. Ahora se calcula y anexa el CRC16 en TCP→RTU, y se
   valida/descarta en RTU→TCP. → [`bridge_rs485.cpp`](main/bridge_rs485.cpp)
4. **El panel web no movía el motor.** `POST /api/command` solo logueaba. Ahora
   parsea el JSON y enruta a `stepper_control_*` / `bridge_rs485_set_mode`, y
   `GET /api/status` reporta posición/velocidad/estado reales.
   → [`wifi_manager.cpp`](main/wifi_manager.cpp)

### Pendiente (TODOs conocidos, no bloqueantes para arrancar)

- **Homing real**: [`stepper_control.cpp`](main/stepper_control.cpp) `stepper_control_home()`
  todavía no usa los finales de carrera (GPIO 32/33); resetea la posición a 0 sin moverse.
- **Dispatch de Modbus local y CAN**: `cmd_processor_task` en [`main.cpp`](main/main.cpp)
  recibe tramas pero aún no las interpreta (function codes / mensajes CAN).
- **`can_mode` desde la web**: se mapea a `BRIDGE_MODE_LOCAL_ONLY` como placeholder;
  no existe un modo puente CAN dedicado en el enum.
- **Servidor Modbus TCP de un solo cliente**: `tcp_server_task` atiende una conexión
  a la vez (loop bloqueante). Suficiente para un master único.
- **Parser JSON mínimo**: `wifi_manager.cpp` usa búsqueda de strings, no un parser
  completo. Para cargas arbitrarias conviene migrar a cJSON.
- **Credenciales WiFi hardcodeadas** en [`main.cpp`](main/main.cpp); mover a NVS/menuconfig.
- **Nivel lógico 3.3 V** hacia el driver DM542/TB6600: validar contra la hoja de datos
  del driver (común-ánodo); algunos requieren ~5 V para los optoacopladores.
