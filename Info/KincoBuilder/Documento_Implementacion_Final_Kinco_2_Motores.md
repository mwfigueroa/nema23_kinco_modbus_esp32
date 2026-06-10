# Documento de Implementacion Final Kinco - 2 Motores

Proyecto validado: `Kinco_esp_Modbus_test_2`
PLC: Kinco MK043E-20DT
Control maestro: ESP32 por Modbus RTU RS485, Slave ID `1`
Estado: dos motores funcionando desde la interfaz web del ESP32
Fecha de validacion: 2026-06-08

Este documento reemplaza el esquema anterior basado en palabras de control
`40051/40101`. El programa final usa registros dedicados por funcion:
PABS, HOME, PREL y JOG. El mapa antiguo queda solo como contexto historico
en `Info/kinco_mk043e_nema23_context.md`.

## 1. Archivos Actuales

| Archivo | Uso |
|---|---|
| `Info/programas_prueba/Kinco_esp_Modbus_test_2.kpr` | Proyecto KincoBuilder actual |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/MAIN_MAIN.ilp` | Logica IL validada |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/Kinco_esp_Modbus_test_2.kgv` | Variables globales |
| `Info/programas_prueba/Kinco_esp_Modbus_test_2/Kinco_esp_Modbus_test_2_var_global.csv` | CSV alternativo para variables globales |
| `Info/programas_prueba/programa_final_2motores_il_ladder.md` | Referencia operativa del programa validado |

## 2. Hardware y Cableado PLC

| Funcion | Motor 1 / AXIS 0 | Motor 2 / AXIS 1 | Nota |
|---|---|---|---|
| STEP/PUL | `%Q0.0` | `%Q0.1` | Salidas PTO internas |
| DIR | `%Q0.2` | `%Q0.3` | Salidas PTO internas |
| Enable driver | `%Q0.4` activo-bajo | `%Q0.5` activo-bajo | `0` habilita, `1` deshabilita |
| Sensor HOME | `%I0.0` | `%I0.3` | Entrada fisica de cero |
| JOG forward fisico | `%I0.1` | `%I0.4` | Entrada manual |
| JOG backward fisico | `%I0.2` | `%I0.5` | Entrada manual |
| Posicion PTO Kinco | `%SMD212` | `%SMD242` | Contadores internos |
| Posicion Modbus | `%VD200` | `%VD500` | Copia leida por ESP32 |

Nota critica: `%Q0.3` es direccion del motor 2. No usar `%Q0.3` como enable.
Los enables reales del programa final son `%Q0.4` y `%Q0.5`.

## 3. Configuracion KincoBuilder

| Parametro | Valor |
|---|---|
| Protocolo COM1 | Modbus RTU Slave |
| Station ID | `1` |
| Baudrate | `115200` |
| Formato | `8N1` |
| Programa principal | `MAIN_MAIN.ilp` |
| Variables globales | `Kinco_esp_Modbus_test_2.kgv` o CSV importado |

## 4. Arquitectura del Programa

El ESP32 no envia una palabra de control general. En su lugar escribe
parametros y luego el registro de comando correspondiente:

```text
PABS absoluto: destino en VD + escribir 1 en Cmd_PabsStart
HOME:          comando distinto de 0
PREL relativo: distancia distinta de 0
JOG:           1 forward, 2 backward, 0 stop
STOP:          escribir 1 en stop individual o stop ambos
```

El destino PABS puede ser `0`. El PLC captura el start PABS, limpia el
registro de start, habilita el driver durante el movimiento y publica
estado/errores en registros de diagnostico.

El stop operativo usa `PSTOP` del PLC y se ejecuta por eje dentro del scan.
No reemplaza una parada de emergencia de seguridad cableada por hardware.

## 5. Mapa Modbus Motor 1 / AXIS 0

### Escritura ESP32 -> PLC

| Modbus | PLC | Tipo | Funcion |
|---:|---|---|---|
| `40151-40152` | `%VD100` | DINT | Destino PABS absoluto; puede ser `0` |
| `40153-40154` | `%VD104` | DWORD | Frecuencia maxima PABS |
| `40155` | `%VW108` | WORD | Frecuencia minima PABS |
| `40156` | `%VW110` | WORD | Tiempo acel/decel PABS |
| `40164` | `%VW126` | WORD | Start PABS simple; escribir `1` arranca movimiento absoluto |
| `40165` | `%VW128` | WORD | Stop operativo motor 1; escribir `1` ejecuta PSTOP |
| `40166` | `%VW130` | WORD | Stop operativo ambos motores; escribir `1` ejecuta PSTOP en ambos ejes |
| `40157` | `%VW112` | WORD | Comando HOME; distinto de `0` arranca PHOME |
| `40158` | `%VW114` | INT | Modo HOME; `1` solo sensor HOME |
| `40159` | `%VW116` | INT | Direccion HOME; `0` forward, `1` backward |
| `40160` | `%VW118` | WORD | Frecuencia minima HOME |
| `40161-40162` | `%VD120` | DWORD | Frecuencia maxima HOME |
| `40163` | `%VW124` | WORD | Tiempo acel/decel HOME |
| `40167-40168` | `%VD132` | DINT | Distancia PREL; distinto de `0` arranca relativo |
| `40169-40170` | `%VD136` | DWORD | Frecuencia maxima PREL |
| `40171` | `%VW140` | WORD | Frecuencia minima PREL |
| `40172` | `%VW142` | WORD | Tiempo acel/decel PREL |
| `40175` | `%VW148` | WORD | Comando JOG: `0` stop, `1` forward, `2` backward |
| `40176` | `%VW150` | INT | Direccion JOG activa |
| `40177-40178` | `%VD152` | DWORD | Velocidad JOG |

### Lectura PLC -> ESP32

| Modbus | PLC | Tipo | Funcion |
|---:|---|---|---|
| `40201-40202` | `%VD200` | DINT | Posicion actual, copia de `%SMD212` |
| `40252` | `%VW302` | WORD | Estado del ciclo |
| `40253` | `%VW304` | WORD | Error PABS, low byte `%VB304` |
| `40254` | `%VW306` | WORD | Error PREL low byte, debug high byte |
| `40255` | `%VW308` | WORD | Estado HOME |
| `40256` | `%VW310` | WORD | Error HOME, low byte `%VB310` |
| `40257` | `%VW312` | WORD | Estado JOG |
| `40258` | `%VW314` | WORD | Error JOG low byte, error PSTOP high byte |

## 6. Mapa Modbus Motor 2 / AXIS 1

### Escritura ESP32 -> PLC

| Modbus | PLC | Tipo | Funcion |
|---:|---|---|---|
| `40301-40302` | `%VD400` | DINT | Destino PABS absoluto; puede ser `0` |
| `40303-40304` | `%VD404` | DWORD | Frecuencia maxima PABS |
| `40305` | `%VW408` | WORD | Frecuencia minima PABS |
| `40306` | `%VW410` | WORD | Tiempo acel/decel PABS |
| `40314` | `%VW426` | WORD | Start PABS simple; escribir `1` arranca movimiento absoluto |
| `40315` | `%VW428` | WORD | Stop operativo motor 2; escribir `1` ejecuta PSTOP |
| `40307` | `%VW412` | WORD | Comando HOME; distinto de `0` arranca PHOME |
| `40308` | `%VW414` | INT | Modo HOME; `1` solo sensor HOME |
| `40309` | `%VW416` | INT | Direccion HOME; `0` forward, `1` backward |
| `40310` | `%VW418` | WORD | Frecuencia minima HOME |
| `40311-40312` | `%VD420` | DWORD | Frecuencia maxima HOME |
| `40313` | `%VW424` | WORD | Tiempo acel/decel HOME |
| `40317-40318` | `%VD432` | DINT | Distancia PREL; distinto de `0` arranca relativo |
| `40319-40320` | `%VD436` | DWORD | Frecuencia maxima PREL |
| `40321` | `%VW440` | WORD | Frecuencia minima PREL |
| `40322` | `%VW442` | WORD | Tiempo acel/decel PREL |
| `40325` | `%VW448` | WORD | Comando JOG: `0` stop, `1` forward, `2` backward |
| `40326` | `%VW450` | INT | Direccion JOG activa |
| `40327-40328` | `%VD452` | DWORD | Velocidad JOG |

### Lectura PLC -> ESP32

| Modbus | PLC | Tipo | Funcion |
|---:|---|---|---|
| `40351-40352` | `%VD500` | DINT | Posicion actual, copia de `%SMD242` |
| `40402` | `%VW602` | WORD | Estado del ciclo |
| `40403` | `%VW604` | WORD | Error PABS, low byte `%VB604` |
| `40404` | `%VW606` | WORD | Error PREL low byte, debug high byte |
| `40405` | `%VW608` | WORD | Estado HOME |
| `40406` | `%VW610` | WORD | Error HOME, low byte `%VB610` |
| `40407` | `%VW612` | WORD | Estado JOG |
| `40408` | `%VW614` | WORD | Error JOG low byte, error PSTOP high byte |

## 7. Direcciones Base 0

| Registro Modbus | Address base 0 |
|---:|---:|
| `40151` | `150` |
| `40153` | `152` |
| `40155` | `154` |
| `40156` | `155` |
| `40157` | `156` |
| `40164` | `163` |
| `40165` | `164` |
| `40166` | `165` |
| `40167` | `166` |
| `40175` | `174` |
| `40201` | `200` |
| `40252` | `251` |
| `40301` | `300` |
| `40303` | `302` |
| `40305` | `304` |
| `40306` | `305` |
| `40307` | `306` |
| `40314` | `313` |
| `40315` | `314` |
| `40317` | `316` |
| `40325` | `324` |
| `40351` | `350` |
| `40402` | `401` |

## 8. Bits de Estado

Motor 1 usa `%VW302`; motor 2 usa `%VW602`.

| Bit | Motor 1 | Motor 2 | Significado |
|---:|---|---|---|
| 0 | `%V302.0` | `%V602.0` | CycleActive |
| 1 | `%V302.1` | `%V602.1` | PabsActive |
| 2 | `%V302.2` | `%V602.2` | Reservado, antes WaitReturnActive |
| 3 | `%V302.3` | `%V602.3` | Reservado, antes ReturnActive |
| 4 | `%V302.4` | `%V602.4` | CycleDone |
| 5 | `%V302.5` | `%V602.5` | CycleErr |
| 6 | `%V302.6` | `%V602.6` | PabsDone |
| 7 | `%V302.7` | `%V602.7` | PabsErr |
| 8 | `%V303.0` | `%V603.0` | StopDone |
| 9 | `%V303.1` | `%V603.1` | StopErr |
| 10 | `%V303.2` | `%V603.2` | EnableOut logico |
| 11 | `%V303.3` | `%V603.3` | WaitDone |
| 12 | `%V303.4` | `%V603.4` | PrelActive |
| 13 | `%V303.5` | `%V603.5` | PrelDone |
| 14 | `%V303.6` | `%V603.6` | PrelErr |
| 15 | `%V303.7` | `%V603.7` | PlcAlive |

## 9. Bits HOME y JOG

HOME motor 1 usa `%VW308`; HOME motor 2 usa `%VW608`.

| Bit | Significado |
|---:|---|
| 0 | HomeActive |
| 1 | HomeDone |
| 2 | HomeErr |
| 3 | HomeSensor |
| 4 | HomeDir |
| 5 | HomeResetPulse |

JOG motor 1 usa `%VW312`; JOG motor 2 usa `%VW612`.

| Bit | Significado |
|---:|---|
| 0 | JogActive |
| 1 | JogDone |
| 2 | JogErr |
| 3 | JogDir |
| 4 | Entrada JOG forward |
| 5 | Entrada JOG backward |
| 6 | Comando web JOG activo |
| 7 | Error PSTOP JOG |

## 10. Bloques IL Usados

| Bloque | Motor 1 | Motor 2 |
|---|---|---|
| PABS | `PABS AXIS=0` | `PABS AXIS=1` |
| PREL | `PREL AXIS=0` | `PREL AXIS=1` |
| PHOME | `PHOME AXIS=0`, HOME `%I0.0` | `PHOME AXIS=1`, HOME `%I0.3` |
| PJOG | `PJOG AXIS=0` | `PJOG AXIS=1` |
| PSTOP | `PSTOP AXIS=0` | `PSTOP AXIS=1` |
| Reset posicion | `%SM201.6` | `%SM231.6` |
| Busy PTO debug | `%SM66.7` | `%SM76.7` |

## 11. API ESP32

La interfaz web y la API seleccionan motor con `axis`:

```http
GET /api/status?axis=0
GET /api/status?axis=1
GET /api/fast_status?axis=0
GET /api/fast_status?axis=1
```

Ejemplos de comandos:

```json
{"cmd":"kinco_pabs","axis":0,"arg":5000,"speed":2000,"minf":300,"time":300}
{"cmd":"kinco_pabs","axis":1,"arg":5000,"speed":2000,"minf":300,"time":300}
{"cmd":"kinco_prel","axis":1,"arg":7000,"speed":2000,"minf":300,"time":300}
{"cmd":"kinco_home","axis":1,"dir":0,"mode":1,"speed":1000,"minf":200,"time":300}
{"cmd":"kinco_jog_fwd","axis":1,"speed":1000}
{"cmd":"kinco_jog_stop","axis":1}
```

Stop operativo directo por Modbus:

```text
Motor 1: escribir 1 en 40165 / %VW128
Motor 2: escribir 1 en 40315 / %VW428
Ambos:   escribir 1 en 40166 / %VW130
```

Comandos rechazados en esta version porque pertenecen al esquema viejo:
`kinco_enable`, `kinco_stop`, `kinco_reset_pos`, `kinco_reset_status`.

## 12. Checklist de Validacion

1. Cargar `MAIN_MAIN.ilp` y `Kinco_esp_Modbus_test_2.kgv` en KincoBuilder.
2. Confirmar PLC en RUN y Modbus RTU Slave ID `1`.
3. Verificar salidas:
   - Motor 1: `%Q0.0`, `%Q0.2`, `%Q0.4`.
   - Motor 2: `%Q0.1`, `%Q0.3`, `%Q0.5`.
4. Verificar entradas:
   - Motor 1: `%I0.0`, `%I0.1`, `%I0.2`.
   - Motor 2: `%I0.3`, `%I0.4`, `%I0.5`.
5. Abrir la interfaz web del ESP32 y probar Motor 1 con recorrido corto.
6. Cambiar selector a Motor 2 y probar recorrido corto.
7. Confirmar que `%Q0.3` solo actua como direccion de motor 2.
