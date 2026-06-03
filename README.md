# NEMA23 Kinco MODBUS ESP32 — Testing Tool

Herramienta de prueba y control para motor **NEMA23** con driver **Kinco
MK043E-20DT** vía **MODBUS RTU (RS485)** desde un **ESP32 LILYGO T-CAN485**.
Interfaz web embebida para ejecutar movimientos PABS/HOME, ciclo automático
configurable, y monitoreo en tiempo real de la posición.

## Hardware

| Componente | Especificación |
|---|---|
| **MCU** | ESP32 — LILYGO T-CAN485 |
| **PLC** | Kinco MK043E-20DT (esclavo MODBUS RTU) |
| **Motor** | NEMA23 con driver externo |
| **RS485** | MAX13487EESA+ (half-duplex, UART2) |
| **WiFi** | 802.11 b/g/n — modo AP (`NEMA23_Gateway`) |
| **LED** | WS2812B RGB (GPIO 4) — indicador de estado del sistema |

## Pinout relevante

| Función | GPIO |
|---|---|
| RS485 TX | 22 |
| RS485 RX | 21 |
| RS485 EN (/RE) | 17 |
| RS485 SE (SHDN) | 19 |
| WS2812B DATA | 4 |

## Quick Start

### 1. Compilar

```powershell
.\scripts\build.ps1
```

El proyecto compila en `build_marti/`.

### 2. Flashear

```powershell
.\scripts\flash_monitor.ps1 -Port COM4
```

### 3. Conectarse a la interfaz web

La ESP32 levanta un **Access Point WiFi**:

| Parámetro | Valor |
|---|---|
| **SSID** | `NEMA23_Gateway` |
| **Password** | `12345678` |
| **IP** | `192.168.4.1` |
| **Puerto HTTP** | `80` |

Abrir `http://192.168.4.1/` en el navegador.

## Interfaz web

La página principal (`/`) es el panel de control del motor Kinco:

![UI Preview](Info/preview_kinco_ui.html)

### Secciones

| Sección | Función |
|---|---|
| **Contador de pasos** | Posición actual en tiempo real con barra de progreso |
| **Enable / HOME / STOP** | Control básico del driver y HOME |
| **Movimientos** | PABS relativos de ±5000 pasos |
| **🔄 Auto Cycle** | Ciclo automático configurable: N pasos CW → regreso a 0 CCW |
| **Reset / Lectura** | Reset de posición, reset de estados, lectura del PLC |
| **Tarjetas de estado** | Datos del motor + bits de `%VW302` en tiempo real |
| **Log JSON** | Respuesta completa del último comando |

### Auto Cycle

El botón verde ejecuta una secuencia automática:

1. **Fase CW**: PABS a N pasos con velocidad configurable
2. **Fase CCW**: regreso a 0 con velocidad de retorno

Campos configurables en la UI:

| Campo | Default | Rango |
|---|---|---|
| **Steps** | 15000 | 1 – 999999 |
| **CW Hz** | 5000 | 125 – 200000 |
| **CCW Hz** | 2500 | 125 – 200000 |

El contador se actualiza cada **500 ms** durante la ejecución. El botón se
deshabilita mientras corre (~10-30 s).

## API HTTP

| Método | Endpoint | Descripción |
|---|---|---|
| `GET` | `/` | Panel de control HTML |
| `GET` | `/api/status` | Estado del sistema (JSON) |
| `POST` | `/api/command` | Enviar comando (JSON) |

### Comandos disponibles

```json
{"cmd": "kinco_status"}           // Leer estado completo del motor
{"cmd": "kinco_enable", "arg": 1}  // Enable driver (1=ON, 0=OFF)
{"cmd": "kinco_home", "arg": 0}    // Ejecutar HOME
{"cmd": "kinco_pabs", "arg": 10000, "speed": 5000}  // PABS a posición
{"cmd": "kinco_move_delta", "arg": 5000}  // PABS relativo ±N pasos
{"cmd": "kinco_stop"}             // PSTOP
{"cmd": "kinco_reset_pos"}        // Reset posición PTO0
{"cmd": "kinco_reset_status"}     // Reset estados internos
{"cmd": "kinco_auto_cycle", "arg": 15000, "speed": 5000, "minf": 2500}
```

Parámetros opcionales: `axis` (default 0), `speed`, `minf`, `time`, `dir`.

### Ejemplo con PowerShell

```powershell
Invoke-RestMethod `
  -Uri "http://192.168.4.1/api/command" `
  -Method POST `
  -ContentType "application/json" `
  -Body '{"cmd":"kinco_auto_cycle","arg":20000,"speed":6000,"minf":3000}'
```

## Mapa MODBUS Kinco ↔ ESP32

El programa del PLC (`kinco_1motor_modbus_40070.ilp`) expone:

### Escritura (ESP32 → PLC)

| MODBUS | Variable Kinco | Tipo | Función |
|---|---:|---|---|
| **40070** | `%VW138` | WORD | Palabra de control (enable, start, stop, reset) |
| 40051-40052 | `%VD100` | DINT | Destino PABS |
| 40053-40054 | `%VD104` | DWORD | Frecuencia máxima PABS |
| 40055 | `%VW108` | WORD | Frecuencia mínima PABS |
| 40056 | `%VW110` | WORD | Tiempo aceleración PABS |
| 40057 | `%VW112` | INT | Modo HOME |
| 40058 | `%VW114` | INT | Dirección HOME |
| 40059 | `%VW116` | WORD | Frecuencia mínima HOME |
| 40060-40061 | `%VD118` | DWORD | Frecuencia máxima HOME |
| 40062 | `%VW122` | WORD | Tiempo aceleración HOME |

### Lectura (PLC → ESP32)

| MODBUS | Variable Kinco | Tipo | Función |
|---|---:|---|---|
| 40101-40102 | `%VD200` | DINT | Posición actual PTO0 |
| 40152 | `%VW302` | WORD | Bits de estado |
| 40153 | `%VW304` | WORD | ErrID PABS / STOP |
| 40154 | `%VW306` | WORD | ErrID HOME |

### Bits de control (`%VW138` / 40070)

| Bit | Valor | Función |
|---|---:|---|
| 0 | `0x0001` | Enable driver |
| 1 | `0x0003` | Reset posición PTO0 |
| 2 | `0x0005` | Start PABS |
| 3 | `0x0009` | Start HOME |
| 4 | `0x0011` | Reset estados |
| 5 | `0x0021` | PSTOP |

### Bits de estado (`%VW302` / 40152)

| Bit | Nombre | Significado |
|---|---:|---|
| 0 | HomeOK | HOME válido |
| 1 | HomeDone | PHOME completado |
| 2 | HomeErr | Error HOME |
| 3 | PabsDone | PABS completado |
| 4 | PabsErr | Error PABS |
| 5 | PTO0 | Estado PTO0 |
| 6 | HomingActive | HOME en curso |
| 7 | PabsActive | PABS en curso |
| 8 | HomeSensor | Sensor de HOME (I0.0) |
| 9 | SystemReady | Sistema listo para PABS |

## Configuración MODBUS RTU

| Parámetro | Valor |
|---|---|
| Slave ID PLC | `1` |
| Baudrate | `9600` |
| Formato | `8N1` |
| Timeout | 700 ms |
| Edge pulse | 100 ms |

## Indicador LED (WS2812B)

El LED RGB refleja el estado del sistema por prioridad:

| Prio | LED | Estado |
|---|---|---|
| 🔴 1 | Rojo fijo (5s) | Error — PabsErr, HomeErr, fallo MODBUS |
| 🟡 2 | Amarillo respiración rápida | Motor en movimiento |
| 🟢 3 | Verde respiración lenta | Sistema listo |
| 🟠 4 | Naranja blink | No listo — falta HomeOK o enable |
| 🔵 5 | Azul tenue | Solo WiFi — sin contacto PLC |
| ⚪ 6 | Blanco tenue | Boot — arrancando |

La actividad MODBUS aparece como micro-flash overlay que no reemplaza el estado base.

## Estructura del proyecto

```text
nema23_kinco_modbus_esp32/
├── CMakeLists.txt
├── sdkconfig / sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── main.cpp
│   ├── pin_config.h
│   ├── wifi_manager.cpp / .h       ← WiFi AP + HTTP server + lógica Kinco
│   ├── bridge_rs485.cpp / .h       ← MODBUS RTU sobre RS485
│   ├── status_led.cpp / .h         ← Indicador WS2812B
│   ├── can_bus.cpp / .h            ← CAN bus (monitoreo pasivo)
│   └── relay_control.cpp / .h      ← Salidas de relé
├── Info/
│   ├── kinco_1motor_modbus_40070.ilp    ← Programa PLC (IL)
│   ├── kinco_1motor_modbus_40070.kgv    ← Variables globales PLC
│   ├── kinco_1motor_modbus_40070_var_global.csv ← CSV para KincoBuilder
│   ├── preview_kinco_ui.html            ← Preview offline de la UI
│   └── programas_prueba/                ← Programas de prueba y docs
├── scripts/
│   ├── build.ps1
│   ├── flash_monitor.ps1
│   └── ensure_idf.ps1
└── managed_components/
    └── espressif__led_strip/
```

## Requisitos

- **ESP-IDF** v5.5.1
- Python 3.12 (con venv de ESP-IDF configurado)
- Kinco MK043E-20DT con programa `kinco_1motor_modbus_40070.ilp` cargado
- Driver NEMA23 externo + fuente de alimentación

## Archivos relacionados

| Archivo | Descripción |
|---|---|
| `Info/kinco_1motor_modbus_40070.ilp` | Programa del PLC en Instruction List |
| `Info/kinco_1motor_modbus_40070.kgv` | Variables globales del PLC |
| `Info/kinco_1motor_modbus_40070_var_global.csv` | CSV para importar en KincoBuilder |
| `Info/kinco_1motor_modbus_40070_ladder.md` | Documentación del ladder |
| `Info/KincoBuilder_MK043E-20DT_programa_basico.md` | Programa básico de prueba |
| `Info/kinco_mk043e_nema23_context.md` | Contexto técnico del proyecto |
