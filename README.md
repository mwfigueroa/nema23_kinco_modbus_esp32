# NEMA23 Kinco MODBUS ESP32 - Testing Tool

Herramienta de prueba para mover un motor NEMA23 con un PLC Kinco
MK043E-20DT, usando un ESP32 LILYGO T-CAN485 como interfaz WiFi/HTTP y
puente MODBUS TCP -> MODBUS RTU por RS485.

El estado actual del proyecto esta enfocado en el programa PLC
`pabs_basico_5000`: el ESP32 escribe un destino PABS en `40051-40052` y la PLC
hace el ciclo completo:

```text
idle -> habilita driver -> PABS a destino -> espera 3 s -> PABS a 0 -> deshabilita
```

No se usa HOME, STOP ni palabra de control `40070` en esta version de prueba.

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
| `Info/programas_prueba/pabs_basico_5000.ilp` | Programa IL del PLC |
| `Info/programas_prueba/pabs_basico_5000.kgv` | Variables globales |
| `Info/programas_prueba/pabs_basico_5000_var_global.csv` | CSV alternativo para importar variables |
| `Info/programas_prueba/pabs_basico_5000_ladder.md` | Documentacion del ladder |

Dejar la PLC en RUN con MODBUS RTU Slave ID `1`, `9600 8N1`.

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
| Posicion actual | Lee `40101-40102` y muestra la posicion copiada desde `%SMD212` |
| `+5000 y volver a 0` | Escribe `5000` en `40051-40052`; la PLC hace el ciclo completo |
| `-5000 y volver a 0` | Escribe `-5000` en `40051-40052`; la PLC hace el ciclo completo |
| Parametros | Ajusta max Hz, min Hz y aceleracion antes de enviar el destino |
| Bits `40152` | Muestra estado del ciclo PLC |
| Log JSON | Ultima respuesta de `/api/command` |

## API HTTP

| Metodo | Endpoint | Descripcion |
|---|---|---|
| `GET` | `/` | Panel de control HTML |
| `GET` | `/api/status` | Estado general del ESP32 |
| `POST` | `/api/command` | Comandos JSON hacia la PLC |

### Comandos utiles para `pabs_basico_5000`

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

`arg` no puede ser `0`, porque `40051-40052 = 0` se usa como estado idle.

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
y el firmware los rechaza para `pabs_basico_5000`:

```json
{"cmd":"kinco_enable","arg":1}
{"cmd":"kinco_home","arg":0}
{"cmd":"kinco_stop"}
{"cmd":"kinco_reset_pos"}
{"cmd":"kinco_reset_status"}
```

## Mapa MODBUS Kinco <-> ESP32

El programa PLC `pabs_basico_5000` expone:

### Escritura ESP32 -> PLC

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40051-40052 | `%VD100` | DINT | Comando de pasos destino; distinto de `0` arranca ciclo |
| 40053-40054 | `%VD104` | DWORD | Frecuencia maxima PABS |
| 40055 | `%VW108` | WORD | Frecuencia minima PABS |
| 40056 | `%VW110` | WORD | Tiempo de aceleracion/desaceleracion |

### Interno PLC

| Kinco | Tipo | Funcion |
|---|---|---|
| `%VD120` | DINT | Copia interna del destino recibido |
| `%VD124` | DINT | Destino de vuelta a cero |
| `%VD128` | DINT | Destino activo usado por la unica instruccion `PABS` |
| `%M0.7` | BOOL | Pulso comun de arranque PABS |

### Lectura PLC -> ESP32

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40101-40102 | `%VD200` | DINT | Posicion actual copiada desde `%SMD212` |
| 40152 | `%VW302` | WORD | Bits de estado del ciclo |
| 40153 | `%VW304` | WORD | `Err_Pabs`, low byte `%VB304` |
| 40154 | `%VW306` | WORD | Reservado/limpiado |

Si el master usa direcciones base 0:

```text
40051-40052 -> address 50, quantity 2
40053-40054 -> address 52, quantity 2
40055       -> address 54
40056       -> address 55
40101-40102 -> address 100, quantity 2
40152       -> address 151
```

## Bits de estado `40152` / `%VW302`

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
| 10 | `%V303.2` | EnableOut | Estado de `%Q0.3` |
| 11 | `%V303.3` | WaitDone | Timer de espera terminado |

## Cableado PLC usado por `pabs_basico_5000`

| Funcion | PLC |
|---|---|
| STEP / PUL | `%Q0.0` |
| DIR | `%Q0.2` |
| Enable driver | `%Q0.3` |

`%Q0.0` y `%Q0.2` los maneja la instruccion `PABS`; no se fuerzan desde ladder.

## Configuracion MODBUS RTU

| Parametro | Valor |
|---|---|
| Slave ID PLC | `1` |
| Baudrate | `9600` |
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
|   |   |-- pabs_basico_5000.ilp
|   |   |-- pabs_basico_5000.kgv
|   |   |-- pabs_basico_5000_var_global.csv
|   |   `-- pabs_basico_5000_ladder.md
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
- Kinco MK043E-20DT con `pabs_basico_5000` cargado
- Driver NEMA23 externo y fuente de alimentacion

## Archivos relacionados

| Archivo | Descripcion |
|---|---|
| `Info/programas_prueba/pabs_basico_5000.ilp` | Programa PLC en IL (21 redes logicas, ~27 fisicas — optimizado desde 88) |
| `Info/programas_prueba/pabs_basico_5000.kgv` | Variables globales PLC |
| `Info/programas_prueba/pabs_basico_5000_var_global.csv` | CSV para KincoBuilder |
| `Info/programas_prueba/pabs_basico_5000_ladder.md` | Documentacion del ladder |
| `Info/KincoBuilder_MK043E-20DT_programa_basico.md` | Programa basico de referencia |
| `Info/kinco_mk043e_nema23_context.md` | Contexto tecnico del proyecto |
