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
| **WS2812B** | 4 | LED RGB de estado (ver "Indicador de estado") |
| **LIMIT MIN** | 32 | Final de carrera |
| **LIMIT MAX** | 33 | Final de carrera |
| **RELÉ 1** | 13 | Salida activo-alto — coil Modbus 0 ⚠️ compartido con microSD |
| **RELÉ 2** | 14 | Salida activo-alto — coil Modbus 1 ⚠️ compartido con microSD |

> ⚠️ **GPIO 13 y 14 están compartidos con el zócalo microSD** de la T-CAN485.
> Están ruteados y disponibles **solo si NO vas a usar la SD**. Tené cuidado con
> el zócalo SD, sus resistencias/pull-ups y cualquier tarjeta insertada (pueden
> cargar la línea o entrar en conflicto con la salida). Para tomar la señal
> físicamente, lo más seguro es soldar desde los **pads del propio zócalo
> microSD**, o confirmar continuidad con multímetro antes de cablear el relé.

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

Para evitar flashear un firmware viejo, tambien se puede compilar en una carpeta
explicita y usar esa misma carpeta al flashear:

```powershell
. .\scripts\ensure_idf.ps1
Import-EspIdfEnvironment
idf.py -B build_marti build
idf.py -B build_marti -p COM4 flash
```

El ultimo flash validado se hizo en `COM4`. Lo importante es compilar y flashear
siempre desde el mismo `BuildDir`; por defecto los scripts usan `build_marti/`.
En el panel web, la UI nueva se identifica como `UI: 1.1`.

### 2. Flashear y monitorear

```powershell
.\scripts\flash_monitor.ps1 -Port COM3
```

### 3. Conectarse

El firmware arranca en modo **STA / cliente WiFi**:

| Vía | SSID / Red | IP de la placa | Notas |
|-----|-----------|----------------|-------|
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
{"cmd": "move_rel",    "arg": 1000, "speed": 5000}
{"cmd": "set_speed",   "arg": 5000}
{"cmd": "run_speed",   "arg": 5000}
{"cmd": "stop"}
{"cmd": "estop"}
{"cmd": "home"}
{"cmd": "enable",      "arg": 1}
{"cmd": "rs485_mode"}
{"cmd": "can_mode"}
{"cmd": "plc_send_step"}
{"cmd": "plc_read_vw"}
```

### Comandos PLC Kinco desde HTTP

El panel web incluye dos botones para probar la comunicacion con el PLC Kinco
`MK043E-20DT` por RS485/Modbus RTU:

| Boton / comando | Accion |
|-----------------|--------|
| `PLC Steps -> VW0/VW2` / `{"cmd":"plc_send_step"}` | Escribe la posicion actual del NEMA23 como entero de 32 bits en `%VD0` |
| `Leer VW0 / VW2` / `{"cmd":"plc_read_vw"}` | Lee `%VW0` y `%VW2`, y reconstruye el valor DINT |

Respuesta de lectura esperada:

```json
{
  "result": "ok",
  "cmd": "plc_read_vw",
  "plc_slave": 1,
  "vw0_register": 100,
  "vw0": 0,
  "vw2_register": 101,
  "vw2": 1234,
  "plc_value_32": 1234,
  "read_status": "lectura_ok"
}
```

### Ejemplo TCP/IP: avanzar 1000 pasos con velocidad

Conectarse a la misma red WiFi donde se conecta la placa (`NS-LAB`) y usar la IP DHCP informada por el monitor serie:

| Dato | Valor |
|------|-------|
| **SSID STA** | `NS-LAB` |
| **IP placa** | DHCP, ver log `WiFi STA IP: x.x.x.x` |
| **Endpoint** | `POST http://<IP_STA>/api/command` |

Enviar comando desde PowerShell:

```powershell
Invoke-RestMethod `
  -Uri "http://<IP_STA>/api/command" `
  -Method POST `
  -ContentType "application/json" `
  -Body '{"cmd":"move_rel","arg":1000,"speed":5000}'
```

El cuerpo JSON enviado es:

```json
{"cmd":"move_rel","arg":1000,"speed":5000}
```

Tambien se puede configurar la velocidad una vez y luego enviar movimientos:

```powershell
Invoke-RestMethod `
  -Uri "http://<IP_STA>/api/command" `
  -Method POST `
  -ContentType "application/json" `
  -Body '{"cmd":"set_speed","arg":5000}'

Invoke-RestMethod `
  -Uri "http://<IP_STA>/api/command" `
  -Method POST `
  -ContentType "application/json" `
  -Body '{"cmd":"move_rel","arg":1000}'
```

Reemplazar `<IP_STA>` por la IP DHCP que aparece en el monitor serie como
`WiFi STA IP: x.x.x.x`.

## PLC Kinco MK043E-20DT por RS485

El gateway puede hablar directamente con el PLC Kinco por Modbus RTU usando el
bus RS485. La comunicacion directa usa `bridge_rs485_transact()`, protegida con
mutex para no pisarse con el puente Modbus TCP -> RS485.

Configuracion usada actualmente:

| Parametro | Valor |
|-----------|-------|
| Slave ID PLC | `1` |
| Baudrate | `9600` |
| Formato serie | `8N1` |
| Lectura | FC03 Read Holding Registers |
| Escritura 32 bits | FC16 Write Multiple Registers |

Mapa Kinco probado:

| Variable Kinco | Registro Modbus | Uso |
|----------------|-----------------|-----|
| `%VW0` | `100` | Word bajo de `%VD0` |
| `%VW2` | `101` | Word alto de `%VD0` |
| `%VD0` | `100` + `101` | DINT de 32 bits |

La ESP escribe la posicion del NEMA23 como `int32_t` en `%VD0`:

- `%VW0` recibe `(pos & 0xFFFF)`.
- `%VW2` recibe `(pos >> 16)`.

El programa del PLC no debe escribir ni incrementar `%VD0` si se quiere que la
lectura posterior coincida exactamente con la posicion enviada por la ESP.

Al leer, la ESP reconstruye:

```c
plc_value_32 = ((uint32_t)VW2 << 16) | VW0;
```

Programa basico para cargar en KincoBuilder:

- [`Info/KincoBuilder_MK043E-20DT_programa_basico.md`](Info/KincoBuilder_MK043E-20DT_programa_basico.md)

## Modos de puente

- **Modbus TCP -> RS485**: las tramas Modbus TCP recibidas por WiFi se forwardean al bus RS485.
- **RAW TCP -> RS485**: puente transparente.
- **Local Only**: solo comandos al motor NEMA23.
- **CAN**: mensajes CAN bus independientes.

## Salidas de relé ("topes") vía Modbus

Dos relés en **GPIO 13/14** (activo-alto, ver advertencia de microSD en el Pinout)
se controlan como **coils Modbus** dirigidos al **slave ID local 247 (0xF7)**.
Las tramas a ese ID se procesan en el gateway y **no** se reenvían al bus RS485
(filtro local activado). El gateway responde de forma síncrona, así que un master
estándar no da timeout.

- **Conexión**: Modbus **TCP**, `<IP>:502`, **Unit ID = 247 (0xF7)**
- **Mapa de coils**: coil `0` = Relé 1 (GPIO 13) · coil `1` = Relé 2 (GPIO 14)

| Acción | Función | Detalle |
|--------|---------|---------|
| Encender Relé 1 | **FC 05** (Write Single Coil) | coil `0`, valor `0xFF00` |
| Apagar Relé 1 | **FC 05** | coil `0`, valor `0x0000` |
| Relé 2 | **FC 05** | coil `1` |
| Ambos a la vez | **FC 0F** (Write Multiple Coils) | start `0`, qty `2` |
| Leer estado | **FC 01** (Read Coils) | start `0`, qty `2` |

Direcciones o valores inválidos devuelven una excepción Modbus estándar
(`0x01` función ilegal, `0x02` dirección ilegal, `0x03` valor ilegal).
Los relés arrancan **desactivados** tras el reset.

## Indicador de estado (WS2812B)

El LED RGB de **GPIO 4** muestra el estado del gateway con prioridad (los estados
críticos pisan a los informativos). Siempre está encendido con alguna señal:

| Prioridad | Estado | Color | Patrón |
|-----------|--------|-------|--------|
| 1 (crítico) | Motor en **E-STOP** | 🔴 Rojo | Parpadeo rápido |
| 2 (actividad) | **Motor moviéndose** | 🩵 Cian | Fijo |
| 3 (reposo) | **STA conectado** a la red | 🟢 Verde | Respiración |
| 3 (reposo) | **Solo AP** (sin STA) | 🔵 Azul | Respiración |
| 3 (reposo) | Arrancando / sin red | ⚪ Blanco tenue | Respiración |

## Estructura del proyecto

```text
nema23_lilygo/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml        <- Dependencias (espressif/led_strip)
│   ├── main.cpp                 <- Entry point
│   ├── pin_config.h             <- GPIO mapping
│   ├── wifi_manager.h/cpp       <- WiFi AP/STA + HTTP
│   ├── bridge_rs485.h/cpp       <- Modbus TCP <-> RS485 + relés (coils)
│   ├── relay_control.h/cpp      <- Salidas de relé (GPIO 13/14)
│   ├── status_led.h/cpp         <- Indicador WS2812B por estado
│   ├── can_bus.h/cpp            <- CAN bus (TWAI)
│   ├── stepper_control.h/cpp    <- NEMA23 via FastAccelStepper
│   └── stepper_motor_encoder.c/h <- RMT encoder
├── components/
│   └── FastAccelStepper/        <- Librería de aceleración
├── Info/
│   ├── KincoBuilder_MK043E-20DT_programa_basico.md
│   ├── Kinco_K5_Software_Manual_20210510.pdf
│   └── Kinco_MK043E-20DT_Spec_Sheet.pdf
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

## Estado de revision (2026-05-27)

Revision tecnica de factibilidad. El proyecto compila (`build_marti/nema23_lilygo.bin`)
y la arquitectura es solida. Se corrigieron los siguientes **bugs bloqueantes**
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
5. **Acceso local al PLC Kinco por RS485.** Se agrego `bridge_rs485_transact()`
   con mutex de UART para que el HTTP server pueda hacer transacciones Modbus
   RTU directas sin chocar con el puente TCP -> RS485.
   -> [`bridge_rs485.cpp`](main/bridge_rs485.cpp)
6. **Lectura/escritura de posicion NEMA23 en PLC Kinco.** El comando
   `plc_send_step` escribe `%VD0` como DINT de 32 bits usando `%VW0/%VW2`
   con FC16; `plc_read_vw` lee ambos words con FC03 y reconstruye
   `plc_value_32`. -> [`wifi_manager.cpp`](main/wifi_manager.cpp)

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
