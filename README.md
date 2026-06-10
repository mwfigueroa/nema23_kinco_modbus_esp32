# NEMA23 Kinco MODBUS ESP32 - Testing Tool

Herramienta de prueba para mover dos motores NEMA23 con un PLC Kinco
MK043E-20DT, usando un ESP32 LILYGO T-CAN485 como interfaz WiFi/HTTP y
puente MODBUS TCP -> MODBUS RTU por RS485.

El estado actual del proyecto esta enfocado en el programa PLC
`Kinco_esp_Modbus_test_2`: el ESP32 controla dos ejes PTO. Motor 1 usa
`AXIS=0`; motor 2 usa `AXIS=1`. Para cada motor puede escribir un destino
PABS, lanzar HOME, mover relativo con PREL o usar JOG.

Estado validado en banco el 2026-06-08: dos motores operando desde la
interfaz web del ESP32 con selector Motor 1 / Motor 2.

```text
idle -> habilita driver -> PABS a destino absoluto -> deshabilita
idle -> habilita driver -> PHOME hasta sensor HOME -> reset contador -> deshabilita
idle -> habilita driver -> PREL +/-distancia -> deshabilita
```

No se usa la palabra de control `40070` en esta version. `PSTOP` queda en el
PLC para detener JOG y para manejo interno de parada.

## Hardware

| Componente | Especificacion |
|---|---|
| MCU | ESP32 - LILYGO T-CAN485 |
| PLC | Kinco MK043E-20DT como esclavo MODBUS RTU |
| Motores | 2x NEMA23 con drivers externos |
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

Dejar la PLC en RUN con MODBUS RTU Slave ID `1`, `115200 8N1` (configurar en KincoBuilder -> hardware COM1; el firmware ESP usa 115200 por defecto).

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

La pagina principal (`/`) muestra el panel de control Kinco con selector
Motor 1 / Motor 2.

Funciones disponibles:

| Seccion | Funcion |
|---|---|
| Selector Motor | Elige `axis=0` o `axis=1` y actualiza etiquetas/registros |
| Posicion actual | Lee `40201-40202` para motor 1 o `40351-40352` para motor 2 |
| `Ir a +5000` / `Ir a 0` | Escribe destino PABS y start del motor seleccionado |
| `HOME forward/backward` | Escribe parametros HOME y dispara PHOME del motor seleccionado |
| `PREL +/-` | Ejecuta movimiento relativo sin vuelta automatica |
| `JOG forward/backward` | Mantiene JOG mientras se pulsa y envia stop al soltar |
| Parametros | Ajusta max Hz, min Hz y aceleracion antes de enviar el comando |
| Bits de ciclo | Muestra `40252` para motor 1 o `40402` para motor 2 |
| Log JSON | Ultima respuesta de `/api/command` |

## API HTTP

| Metodo | Endpoint | Descripcion |
|---|---|---|
| `GET` | `/` | Panel de control HTML |
| `GET` | `/api/status?axis=0\|1` | Estado completo de PLC/ESP32 para diagnostico |
| `GET` | `/api/fast_status?axis=0\|1` | Estado liviano para contador y refresco rapido |
| `POST` | `/api/command` | Comandos JSON hacia la PLC |

### Comandos utiles para `Kinco_esp_Modbus_test_2`

Usar `axis=0` para motor 1 y `axis=1` para motor 2.

Leer estado:

```json
{"cmd":"kinco_status"}
```

Enviar PABS simple a `+5000` pasos:

```json
{"cmd":"kinco_pabs","axis":0,"arg":5000,"speed":2000,"minf":300,"time":300}
```

Enviar PABS simple a `0`:

```json
{"cmd":"kinco_pabs","axis":0,"arg":0,"speed":2000,"minf":300,"time":300}
```

Enviar PABS simple a `-5000` pasos:

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

Lanzar HOME forward con el sensor HOME del motor seleccionado:

```json
{"cmd":"kinco_home","axis":0,"arg":0,"mode":1,"speed":1000,"minf":200,"time":300}
```

Lanzar HOME backward:

```json
{"cmd":"kinco_home","axis":0,"arg":1,"mode":1,"speed":1000,"minf":200,"time":300}
```

Mover JOG forward y detenerlo:

```json
{"cmd":"kinco_jog_fwd","axis":1,"speed":1000}
{"cmd":"kinco_jog_stop","axis":1}
```

En `kinco_pabs`, `arg=0` es valido y ordena ir a posicion absoluta cero.
En `kinco_prel`, `arg` no puede ser `0`, porque no habria movimiento relativo.
En `kinco_home`, `arg=0` significa forward y `arg=1` backward.

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

Motor 1 (`axis=0`):

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40151-40152 | `%VD100` | DINT | Destino PABS absoluto; puede ser `0` |
| 40153-40154 | `%VD104` | DWORD | Frecuencia maxima PABS |
| 40155 | `%VW108` | WORD | Frecuencia minima PABS |
| 40156 | `%VW110` | WORD | Tiempo de aceleracion/desaceleracion |
| 40164 | `%VW126` | WORD | Start PABS simple; escribir `1` arranca movimiento absoluto |
| 40165 | `%VW128` | WORD | Stop operativo motor 1; escribir `1` ejecuta `PSTOP` |
| 40166 | `%VW130` | WORD | Stop operativo ambos motores; escribir `1` ejecuta `PSTOP` en ambos ejes |
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
| 40175 | `%VW148` | WORD | Comando JOG: `0` stop, `1` forward, `2` backward |
| 40176 | `%VW150` | WORD | Direccion JOG activa |
| 40177-40178 | `%VD152` | DWORD | Velocidad JOG |

Motor 2 (`axis=1`):

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40301-40302 | `%VD400` | DINT | Destino PABS absoluto; puede ser `0` |
| 40303-40304 | `%VD404` | DWORD | Frecuencia maxima PABS |
| 40305 | `%VW408` | WORD | Frecuencia minima PABS |
| 40306 | `%VW410` | WORD | Tiempo de aceleracion/desaceleracion |
| 40314 | `%VW426` | WORD | Start PABS simple; escribir `1` arranca movimiento absoluto |
| 40315 | `%VW428` | WORD | Stop operativo motor 2; escribir `1` ejecuta `PSTOP` |
| 40307 | `%VW412` | WORD | Comando HOME; distinto de `0` arranca PHOME |
| 40308 | `%VW414` | WORD | Modo HOME; `1` usa solo sensor HOME |
| 40309 | `%VW416` | WORD | Direccion HOME; `0` forward, `1` backward |
| 40310 | `%VW418` | WORD | Frecuencia minima HOME |
| 40311-40312 | `%VD420` | DWORD | Frecuencia maxima HOME |
| 40313 | `%VW424` | WORD | Tiempo de aceleracion/desaceleracion HOME |
| 40317-40318 | `%VD432` | DINT | Distancia relativa PREL; distinto de `0` arranca movimiento |
| 40319-40320 | `%VD436` | DWORD | Frecuencia maxima PREL |
| 40321 | `%VW440` | WORD | Frecuencia minima PREL |
| 40322 | `%VW442` | WORD | Tiempo de aceleracion/desaceleracion PREL |
| 40325 | `%VW448` | WORD | Comando JOG: `0` stop, `1` forward, `2` backward |
| 40326 | `%VW450` | WORD | Direccion JOG activa |
| 40327-40328 | `%VD452` | DWORD | Velocidad JOG |

### Interno PLC

| Kinco | Tipo | Funcion |
|---|---|---|
| `%VD180` | DINT | Copia interna del destino recibido |
| `%VD184` | DINT | Reservado historico, antes destino de vuelta a cero |
| `%VD188` | DINT | Destino activo usado por la unica instruccion `PABS` |
| `%VD192` | DINT | Distancia activa usada por la instruccion `PREL` |
| `%M0.7` | BOOL | Pulso comun de arranque PABS |
| `%M4.5` | BOOL | Pulso de arranque PREL |
| `%M5.1` | BOOL | Pulso de arranque PHOME |

### Lectura PLC -> ESP32

Motor 1 (`axis=0`):

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40201-40202 | `%VD200` | DINT | Posicion actual copiada desde `%SMD212` |
| 40252 | `%VW302` | WORD | Bits de estado del ciclo |
| 40253 | `%VW304` | WORD | `Err_Pabs`, low byte `%VB304` |
| 40254 | `%VW306` | WORD | `Err_Prel`, low byte `%VB306`; debug en high byte |
| 40255 | `%VW308` | WORD | Bits de estado HOME |
| 40256 | `%VW310` | WORD | `Err_Home`, low byte `%VB310` |
| 40257 | `%VW312` | WORD | Bits de estado JOG |
| 40258 | `%VW314` | WORD | `Err_Jog` y `Err_JogStop` |

Motor 2 (`axis=1`):

| MODBUS | Kinco | Tipo | Funcion |
|---:|---|---|---|
| 40351-40352 | `%VD500` | DINT | Posicion actual copiada desde `%SMD242` |
| 40402 | `%VW602` | WORD | Bits de estado del ciclo |
| 40403 | `%VW604` | WORD | `Err_Pabs`, low byte `%VB604` |
| 40404 | `%VW606` | WORD | `Err_Prel`, low byte `%VB606`; debug en high byte |
| 40405 | `%VW608` | WORD | Bits de estado HOME |
| 40406 | `%VW610` | WORD | `Err_Home`, low byte `%VB610` |
| 40407 | `%VW612` | WORD | Bits de estado JOG |
| 40408 | `%VW614` | WORD | `Err_Jog` y `Err_JogStop` |

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
40164       -> address 163
40165       -> address 164
40166       -> address 165
40167-40168 -> address 166, quantity 2
40169-40170 -> address 168, quantity 2
40171       -> address 170
40172       -> address 171
40175       -> address 174
40176       -> address 175
40177-40178 -> address 176, quantity 2
40201-40202 -> address 200, quantity 2
40252       -> address 251
40255       -> address 254
40256       -> address 255
40257       -> address 256
40258       -> address 257
40301-40302 -> address 300, quantity 2
40303-40304 -> address 302, quantity 2
40305       -> address 304
40306       -> address 305
40307       -> address 306
40308       -> address 307
40309       -> address 308
40310       -> address 309
40311-40312 -> address 310, quantity 2
40313       -> address 312
40314       -> address 313
40315       -> address 314
40317-40318 -> address 316, quantity 2
40319-40320 -> address 318, quantity 2
40321       -> address 320
40322       -> address 321
40325       -> address 324
40326       -> address 325
40327-40328 -> address 326, quantity 2
40351-40352 -> address 350, quantity 2
40402       -> address 401
40405       -> address 404
40406       -> address 405
40407       -> address 406
40408       -> address 407
```

### Stop operativo por Modbus directo

El stop operativo usa la instruccion `PSTOP` de la PLC y se ejecuta por eje
dentro del scan. Es util para detener un movimiento activo desde un master
Modbus TCP/RTU, pero no reemplaza una parada de emergencia de seguridad
cableada por hardware.

Ejemplos con registros Modbus humanos:

```text
Stop motor 1: escribir WORD 1 en 40165
Stop motor 2: escribir WORD 1 en 40315
Stop ambos:   escribir WORD 1 en 40166
```

Si la libreria usa address base-0:

```text
Stop motor 1: write single register address 164 = 1
Stop motor 2: write single register address 314 = 1
Stop ambos:   write single register address 165 = 1
```

Confirmacion por estado:

```text
Motor 1: leer 40252; bit 8 StopDone, bit 9 StopErr
Motor 2: leer 40402; bit 8 StopDone, bit 9 StopErr
```

## Bits de estado de ciclo

Motor 1 usa `40252` / `%VW302`; motor 2 usa `40402` / `%VW602`.
La tabla muestra motor 1; para motor 2 usar los mismos bits en `%V602/%V603`.

| Bit | Direccion | Nombre | Significado |
|---:|---|---|---|
| 0 | `%V302.0` | CycleActive | Ciclo activo, driver habilitado |
| 1 | `%V302.1` | PabsActive | Movimiento absoluto activo |
| 2 | `%V302.2` | Reservado | Antes WaitReturnActive |
| 3 | `%V302.3` | Reservado | Antes ReturnActive |
| 4 | `%V302.4` | CycleDone | Movimiento completado correctamente |
| 5 | `%V302.5` | CycleErr | Error de ciclo |
| 6 | `%V302.6` | PabsDone | PABS completado |
| 7 | `%V302.7` | PabsErr | Error en PABS |
| 8 | `%V303.0` | StopDone | Stop operativo ejecutado |
| 9 | `%V303.1` | StopErr | Error de `PSTOP` operativo |
| 10 | `%V303.2` | EnableOut | Driver habilitado logico; salida fisica activa-bajo |
| 11 | `%V303.3` | WaitDone | Timer de espera terminado |
| 12 | `%V303.4` | PrelActive | Movimiento relativo activo |
| 13 | `%V303.5` | PrelDone | PREL completado correctamente |
| 14 | `%V303.6` | PrelErr | Error en PREL |
| 15 | `%V303.7` | PlcAlive | `MAIN_MAIN.ilp` esta escaneando en la PLC |

## Bits de estado HOME

Motor 1 usa `40255` / `%VW308`; motor 2 usa `40405` / `%VW608`.

| Bit | Direccion | Nombre | Significado |
|---:|---|---|---|
| 0 | `%V308.0` / `%V608.0` | HomeActive | PHOME activo |
| 1 | `%V308.1` / `%V608.1` | HomeDone | PHOME terminado correctamente |
| 2 | `%V308.2` / `%V608.2` | HomeErr | Error en PHOME |
| 3 | `%V308.3` / `%V608.3` | HomeSensor | Estado de `%I0.0` o `%I0.3` |
| 4 | `%V308.4` / `%V608.4` | HomeDir | Direccion HOME activa |
| 5 | `%V308.5` / `%V608.5` | HomeResetPulse | Pulso que limpia `%SMD212` o `%SMD242` al terminar HOME |

## Cableado PLC usado por `MAIN_MAIN.ilp`

| Funcion | PLC |
|---|---|
| STEP / PUL motor 1 | `%Q0.0` |
| DIR motor 1 | `%Q0.2` |
| Enable motor 1 | `%Q0.4` activo-bajo (`0` habilita, `1` deshabilita) |
| Sensor HOME motor 1 | `%I0.0` |
| JOG fisico motor 1 | `%I0.1` forward, `%I0.2` backward |
| STEP / PUL motor 2 | `%Q0.1` |
| DIR motor 2 | `%Q0.3` |
| Enable motor 2 | `%Q0.5` activo-bajo (`0` habilita, `1` deshabilita) |
| Sensor HOME motor 2 | `%I0.3` |
| JOG fisico motor 2 | `%I0.4` forward, `%I0.5` backward |

`%Q0.0/%Q0.2` y `%Q0.1/%Q0.3` los manejan las instrucciones
`PABS`/`PREL`/`PHOME`; no se fuerzan desde ladder. `%Q0.3` ya no puede usarse
como enable porque es la direccion fisica del `AXIS=1`.

`%SM201.7/%SM231.7` y `%SM201.3/%SM231.3` se fuerzan a `0` en el PLC.
`%SM201.6` y `%SM231.6` son reset de posicion PTO0/PTO1: `MAIN_MAIN.ilp`
los pulsa en el primer scan y al terminar HOME, pero no deben quedar mantenidos
en `1` porque limpiarian continuamente `%SMD212/%SMD242`.

## Configuracion MODBUS RTU

| Parametro | Valor |
|---|---|
| Slave ID PLC | `1` |
| Baudrate | `115200` (verificar que KincoBuilder lo permita; docs oficiales listan 9600/19200 - probar 115200 o fallback a 19200) |
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
|   |-- ensure_idf.ps1
|   `-- read_kinco_position.py   # cliente Modbus TCP puro para leer posición (sin deps)
`-- managed_components/
```

## Utilidad: lectura de posición por Modbus TCP (script Python)

`scripts/read_kinco_position.py` es un cliente Modbus TCP minimalista (sin dependencias como pymodbus) para leer el contador de pasos actual del PLC Kinco a través del puente del ESP32.

Por defecto apunta a `%VD200` (holding registers 40201-40202, address base-0 = 200) del slave 1, que es la posición actual del motor 1 (copia de %SMD212 en el programa PLC).

### Ejemplos de uso

```powershell
# Lectura única (IP del ESP32 obtenida del log serie o /api/status)
python scripts/read_kinco_position.py 192.168.1.123

# Polling continuo cada 200 ms (útil para observar movimiento en tiempo real)
python scripts/read_kinco_position.py 192.168.1.123 --interval 0.2

# Polling limitado + word order explícito (low-high = lo-word primero, default del proyecto)
python scripts/read_kinco_position.py 192.168.1.123 --interval 0.5 --count 30 --word-order low-high

# Usar número de registro humano (40201) en vez de address base-0
python scripts/read_kinco_position.py 192.168.1.123 --human-register 40201
```

Para motor 2 la posición está en 40351-40352 (address 350). Usa `--address 350`.

El script reporta:

```
position_steps=12345 regs=[0x3039,0x0000] address=200 human=40201
```

Maneja correctamente el signo (DINT signed) y valida transacciones.

Ver también el bridge en el README (sección Modbus TCP :502) y el cliente pymodbus de ejemplo que aparece en la documentación del mapa Modbus.

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
| `Info/programas_prueba/programa_final_2motores_il_ladder.md` | Documentacion operativa del programa validado de dos motores |
| `Info/KincoBuilder/Documento_Implementacion_Final_Kinco_2_Motores.md` | Documento final de implementacion Kinco |
| `Info/KincoBuilder_MK043E-20DT_programa_basico.md` | Programa basico de referencia |
| `Info/kinco_mk043e_nema23_context.md` | Contexto tecnico historico del proyecto |
