# NEMA23 Kinco MODBUS ESP32 - Testing Tool

Herramienta de prueba para mover un motor NEMA23 con un PLC Kinco
MK043E-20DT, usando un ESP32 LILYGO T-CAN485 como interfaz WiFi/HTTP y
puente MODBUS TCP -> MODBUS RTU por RS485.

El estado actual del proyecto esta enfocado en el programa PLC
`Kinco_esp_Modbus_test_2`: el ESP32 puede escribir un destino PABS en
`40151-40152` para hacer el ciclo completo, lanzar HOME con `40157`, o
escribir una distancia PREL en `40167-40168` para mover relativo desde la
posicion actual:

```text
idle -> habilita driver -> PABS a destino -> espera 3 s -> PABS a 0 -> deshabilita
idle -> habilita driver -> PHOME hasta sensor I0.0 -> reset contador -> deshabilita
idle -> habilita driver -> PREL +/-distancia -> deshabilita
```

No se usa STOP ni palabra de control `40070` en esta version de prueba.

## Hardware

| Componente | Especificacion |
|---|---|
| MCU | ESP32 - LILYGO T-CAN485 |
| PLC | Kinco MK043E-20DT como esclavo MODBUS RTU |
| Motor | NEMA23 con driver externo |
| RS485 | MAX13487EESA+ sobre UART2 |
| WiFi | STA, cliente de la red `NS-LAB` |
| LED | WS2812B RGB en GPIO 4 |

## Pinout relevante

| Funcion | GPIO |
|---|---:|
| RS485 TX | 22 |
| RS485 RX | 21 |
| RS485 EN (/RE) | 17 |
| RS485 SE (SHDN) | 19 |
| WS2812B DATA | 4 |

## Quick Start

### 1. Cargar el programa PLC

En KincoBuilder, cargar/importar:

| Archivo | Uso |
|---|---|
| `Info/programas_prueba/Kinco_esp_Modbus_test_2.kpr` | Proyecto KincoBuilder actual |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/MAIN_MAIN.ilp` | Programa IL del PLC |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/Kinco_esp_Modbus_test_2.kgv` | Variables globales |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/Kinco_esp_Modbus_test_2_var_global.csv` | CSV alternativo para importar variables |

Dejar la PLC en RUN con MODBUS RTU Slave ID `1`, `115200 8N1` (configurar en KincoBuilder → hardware COM1; el firmware ESP usa 115200 por defecto).

### 2. Compilar firmware ESP32

```powershell
.\scripts\build.ps1 -BuildDir build_marti
```

### 3. Flashear y monitorear

```powershell
.\scripts\flash_monitor.ps1 -Port COM4
```

### 4. Abrir la interfaz web

La ESP32 se conecta como cliente WiFi a `NS-LAB`. La IP aparece en el log serie:

```text
wifi_mgr: WiFi STA IP: x.x.x.x
```

Abrir:

```text
http://<IP_STA>/
```

## Interfaz web

La pagina principal (`/`) muestra el panel `Kinco PABS 5000`.

Funciones disponibles:

| Seccion | Funcion |
|---|---|
| Posicion actual | Lee `40201-40202` y muestra la posicion copiada desde `%SMD212` |
| `+5000 y volver a 0` | Escribe `5000` en `40151-40152`; la PLC hace el ciclo completo |
| `-5000 y volver a 0` | Escribe `-5000` en `40151-40152`; la PLC hace el ciclo completo |
| `HOME forward/backward` | Escribe parametros en `40158-40163` y dispara `40157` |
| Parametros | Ajusta max Hz, min Hz y aceleracion antes de enviar el destino |
| Bits `40252` | Muestra estado del ciclo PLC |
| Log JSON | Ultima respuesta de `/api/command` |

## API HTTP

| Metodo | Endpoint | Descripcion |
|---|---|---|
| `GET` | `/` | Panel de control HTML |
| `GET` | `/api/status` | Estado completo de PLC/ESP32 para diagnostico |
| `GET` | `/api/fast_status` | Estado liviano para contador y refresco rapido |
| `POST` | `/api/command` | Comandos JSON hacia la PLC |

### Comandos utiles para `Kinco_esp_Modbus_test_2`

Leer estado:

```json
{"cmd":"kinco_status"}
```

Enviar un ciclo a `+5000` pasos y vuelta automatica a `0`:

```json
{"cmd":"kinco_pabs","axis":0,"arg":5000,"speed":2000,"minf":300,"time":300}
```

Enviar un ciclo a `-5000` pasos:

```json
{"cmd":"kinco_pabs","axis":0,"arg":-5000,"speed":2000,"minf":300,"time":300}
```

Enviar un movimiento relativo de prueba a `+7000` pasos, sin vuelta a cero:

```json
{"cmd":"kinco_prel","axis":0,"arg":7000,"speed":2000,"minf":300,"time":300}
```

Enviar un movimiento relativo a `-7000` pasos:

```json
{"cmd":"kinco_prel","axis":0,"arg":-7000,"speed":2000,"minf":300,"time":300}
```

Lanzar HOME forward con sensor en `%I0.0`:

```json
{"cmd":"kinco_home","axis":0,"arg":0,"mode":1,"speed":1000,"minf":200,"time":300}
```

Lanzar HOME backward:

```json
{"cmd":"kinco_home","axis":0,"arg":1,"mode":1,"speed":1000,"minf":200,"time":300}
```

En `kinco_pabs` y `kinco_prel`, `arg` no puede ser `0`, porque los registros
de comando en `0` se usan como idle. En `kinco_home`, `arg=0` significa
forward y `arg=1` backward.

### Ejemplo PowerShell

```powershell
Invoke-RestMethod `
  -Uri "http://<IP_STA>/api/command" `
  -Method POST `
  -ContentType "application/json" `
  -Body '{"cmd":"kinco_pabs","axis":0,"arg":5000,"speed":2000,"minf":300,"time":300}'
```

### Comandos no usados en esta prueba

Estos comandos pertenecian al programa anterior con palabra de control `40070`
y el firmware los rechaza para `Kinco_esp_Modbus_test_2`:

```json
{"cmd":"kinco_enable","arg":1}
{"cmd":"kinco_stop"}
{"cmd":"kinco_reset_pos"}
{"cmd":"kinco_reset_status"}
```

## Mapa MODBUS Kinco <-> ESP32

El programa PLC `Kinco_esp_Modbus_test_2/MAIN_MAIN.ilp` expone:

### Escritura ESP32 -> PLC

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40151-40152 | `%VD100` | DINT | Comando de pasos destino; distinto de `0` arranca ciclo |
| 40153-40154 | `%VD104` | DWORD | Frecuencia maxima PABS |
| 40155 | `%VW108` | WORD | Frecuencia minima PABS |
| 40156 | `%VW110` | WORD | Tiempo de aceleracion/desaceleracion |
| 40157 | `%VW112` | WORD | Comando HOME; distinto de `0` arranca PHOME |
| 40158 | `%VW114` | WORD | Modo HOME; `1` usa solo sensor HOME |
| 40159 | `%VW116` | WORD | Direccion HOME; `0` forward, `1` backward |
| 40160 | `%VW118` | WORD | Frecuencia minima HOME |
| 40161-40162 | `%VD120` | DWORD | Frecuencia maxima HOME |
| 40163 | `%VW124` | WORD | Tiempo de aceleracion/desaceleracion HOME |
| 40167-40168 | `%VD132` | DINT | Distancia relativa PREL; distinto de `0` arranca movimiento |
| 40169-40170 | `%VD136` | DWORD | Frecuencia maxima PREL |
| 40171 | `%VW140` | WORD | Frecuencia minima PREL |
| 40172 | `%VW142` | WORD | Tiempo de aceleracion/desaceleracion PREL |

### Interno PLC

| Kinco | Tipo | Funcion |
|---|---|---|
| `%VD180` | DINT | Copia interna del destino recibido |
| `%VD184` | DINT | Destino de vuelta a cero |
| `%VD188` | DINT | Destino activo usado por la unica instruccion `PABS` |
| `%VD192` | DINT | Distancia activa usada por la instruccion `PREL` |
| `%M0.7` | BOOL | Pulso comun de arranque PABS |
| `%M4.5` | BOOL | Pulso de arranque PREL |
| `%M5.1` | BOOL | Pulso de arranque PHOME |

### Lectura PLC -> ESP32

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40201-40202 | `%VD200` | DINT | Posicion actual copiada desde `%SMD212` |
| 40252 | `%VW302` | WORD | Bits de estado del ciclo |
| 40253 | `%VW304` | WORD | `Err_Pabs`, low byte `%VB304` |
| 40254 | `%VW306` | WORD | `Err_Prel`, low byte `%VB306`; debug en high byte |
| 40255 | `%VW308` | WORD | Bits de estado HOME |
| 40256 | `%VW310` | WORD | `Err_Home`, low byte `%VB310` |

Si el master usa direcciones base 0:

```text
40151-40152 -> address 150, quantity 2
40153-40154 -> address 152, quantity 2
40155       -> address 154
40156       -> address 155
40157       -> address 156
40158       -> address 157
40159       -> address 158
40160       -> address 159
40161-40162 -> address 160, quantity 2
40163       -> address 162
40167-40168 -> address 166, quantity 2
40169-40170 -> address 168, quantity 2
40171       -> address 170
40172       -> address 171
40201-40202 -> address 200, quantity 2
40252       -> address 251
40255       -> address 254
40256       -> address 255
```

## Bits de estado `40252` / `%VW302`

| Bit | Direccion | Nombre | Significado |
|---:|---|---|---|
| 0 | `%V302.0` | CycleActive | Ciclo activo, driver habilitado |
| 1 | `%V302.1` | MoveOutActive | Movimiento de ida activo |
| 2 | `%V302.2` | WaitReturnActive | Esperando 3 s antes de volver |
| 3 | `%V302.3` | ReturnActive | Movimiento de vuelta activo |
| 4 | `%V302.4` | CycleDone | Ciclo completado correctamente |
| 5 | `%V302.5` | CycleErr | Error de ciclo |
| 6 | `%V302.6` | PabsOutDone | PABS de ida completado |
| 7 | `%V302.7` | PabsOutErr | Error en PABS de ida |
| 8 | `%V303.0` | PabsReturnDone | PABS de vuelta completado |
| 9 | `%V303.1` | PabsReturnErr | Error en PABS de vuelta |
| 10 | `%V303.2` | EnableOut | Driver habilitado logico; `%Q0.3` fisico es activo-bajo |
| 11 | `%V303.3` | WaitDone | Timer de espera terminado |
| 12 | `%V303.4` | PrelActive | Movimiento relativo activo |
| 13 | `%V303.5` | PrelDone | PREL completado correctamente |
| 14 | `%V303.6` | PrelErr | Error en PREL |
| 15 | `%V303.7` | PlcAlive | `MAIN_MAIN.ilp` esta escaneando en la PLC |

## Bits de estado HOME `40255` / `%VW308`

| Bit | Direccion | Nombre | Significado |
|---:|---|---|---|
| 0 | `%V308.0` | HomeActive | PHOME activo |
| 1 | `%V308.1` | HomeDone | PHOME terminado correctamente |
| 2 | `%V308.2` | HomeErr | Error en PHOME |
| 3 | `%V308.3` | HomeSensor | Estado de `%I0.0` |
| 4 | `%V308.4` | HomeDir | Direccion HOME activa |
| 5 | `%V308.5` | HomeResetPulse | Pulso que limpia `%SMD212` al terminar HOME |

## Cableado PLC usado por `MAIN_MAIN.ilp`

| Funcion | PLC |
|---|---|
| STEP / PUL | `%Q0.0` |
| DIR | `%Q0.2` |
| Enable driver | `%Q0.3` activo-bajo (`0` habilita, `1` deshabilita) |
| Sensor HOME | `%I0.0` |

`%Q0.0` y `%Q0.2` los manejan las instrucciones `PABS`/`PREL`/`PHOME`; no se fuerzan desde ladder.
`%SM201.7` y `%SM201.3` se fuerzan a `0` en el PLC. `%SM201.6` es reset de
posicion PTO0: `MAIN_MAIN.ilp` lo pulsa en el primer scan y al terminar HOME,
pero no debe quedar mantenido en `1` porque limpiaria continuamente `%SMD212`
y puede dejar PABS sin movimiento util aunque los registros Modbus se escriban correctamente.

## Configuracion MODBUS RTU

| Parametro | Valor |
|---|---|
| Slave ID PLC | `1` |
| Baudrate | `115200` (verificar que KincoBuilder lo permita; docs oficiales listan 9600/19200 — probar 115200 o fallback a 19200) |
| Formato | `8N1` |
| Timeout firmware | 700 ms |

## Indicador LED

| Estado | LED |
|---|---|
| Error MODBUS o error de ciclo | Rojo |
| Ciclo activo o motor moviendo | Amarillo |
| PLC responde y ciclo idle | Verde |
| Solo WiFi / sin lectura PLC | Azul |
| Boot | Blanco |

La actividad MODBUS puede aparecer como flash breve de lectura/escritura.

## Estructura del proyecto

```text
nema23_kinco_modbus_esp32/
|-- CMakeLists.txt
|-- sdkconfig / sdkconfig.defaults
|-- partitions.csv
|-- main/
|   |-- main.cpp
|   |-- pin_config.h
|   |-- wifi_manager.cpp / .h
|   |-- bridge_rs485.cpp / .h
|   |-- status_led.cpp / .h
|   |-- can_bus.cpp / .h
|   `-- relay_control.cpp / .h
|-- Info/
|   |-- programas_prueba/
|   |   |-- Kinco_esp_Modbus_test_2.kpr
|   |   `-- Kinco_esp_Modbus_test_2/
|   |       |-- MAIN_MAIN.ilp
|   |       |-- Kinco_esp_Modbus_test_2.kgv
|   |       `-- Kinco_esp_Modbus_test_2_var_global.csv
|   `-- preview_kinco_ui.html
|-- scripts/
|   |-- build.ps1
|   |-- flash_monitor.ps1
|   `-- ensure_idf.ps1
`-- managed_components/
```

## Requisitos

- ESP-IDF v5.5.1
- Python 3.12 con entorno ESP-IDF configurado
- Kinco MK043E-20DT con `Kinco_esp_Modbus_test_2` cargado
- Driver NEMA23 externo y fuente de alimentacion

## Archivos relacionados

| Archivo | Descripcion |
|---|---|
| `Info/programas_prueba/Kinco_esp_Modbus_test_2.kpr` | Proyecto KincoBuilder actual |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/MAIN_MAIN.ilp` | Programa PLC en IL para PABS/PREL/HOME/JOG |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/Kinco_esp_Modbus_test_2.kgv` | Variables globales PLC |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/Kinco_esp_Modbus_test_2_var_global.csv` | CSV para KincoBuilder |
| `Info/KincoBuilder_MK043E-20DT_programa_basico.md` | Programa basico de referencia |
| `Info/kinco_mk043e_nema23_context.md` | Contexto tecnico del proyecto |
