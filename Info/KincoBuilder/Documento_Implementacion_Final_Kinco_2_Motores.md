# Documento de Implementacion Final Kinco - 2 Motores

Proyecto: Kinco MK043E-20DT + dos motores NEMA23 con drivers MD-2545  
Control maestro: ESP32 por MODBUS RTU RS-485  
Archivo base de contexto: `kinco_mk043e_nema23_context.md`  
Proyecto Kinco revisado: `Final_kinco_project_2motores.kpr`  
Fecha: 2026-06-01

## 1. Objetivo

Implementar el control de dos motores NEMA23 desde un PLC Kinco MK043E-20DT. El ESP32 actua como maestro MODBUS RTU y escribe comandos/parametros en el area `%V` del PLC. El PLC ejecuta la logica de movimiento con las instrucciones nativas de posicionamiento de KincoBuilder.

Alcance del proyecto Kinco local revisado:

- Dos ejes PTO reales: `AXIS = 0` y `AXIS = 1`.
- Funciones cargadas en el programa local: `PHOME`, `PABS` y `PSTOP`.
- Control, estado y parametros por registros MODBUS holding.
- Un sensor HOME por motor.
- Una salida de enable independiente por driver.
- PREL/PJOG quedan reservados y documentados como extension de mapa, pero no aparecen cargados en el `MAIN_MAIN.ilp` local revisado.

Decision final de arquitectura: no se implementa tercer eje en la MK043E-20DT. Las instrucciones de posicionamiento documentadas por KincoBuilder aceptan solo `AXIS = 0` y `AXIS = 1`.

## 2. Hardware y Cableado

| Funcion | Motor 1 / Axis 0 | Motor 2 / Axis 1 | Nota |
|---|---|---|---|
| STEP/PUL | `%Q0.0` | `%Q0.1` | Salida PTO interna |
| DIR | `%Q0.2` | `%Q0.3` | Salida PTO interna |
| Enable driver | `%Q0.4` | `%Q0.5` | Salidas dedicadas del programa final local |
| Sensor HOME | `%I0.0` | `%I0.1` | Entrada fisica de cero |
| Posicion actual Kinco | `%SMD212` | `%SMD242` | Copiada a registros `%VD` |

Nota critica: para dos ejes, `%Q0.3` se usa como direccion del `AXIS = 1`. Por eso el enable de los drivers se separa a `%Q0.4` y `%Q0.5`.

## 3. Configuracion KincoBuilder

Configuracion recomendada del puerto serie COM1:

| Parametro | Valor |
|---|---|
| Protocolo | MODBUS RTU Slave |
| Station ID | `1` |
| Baudrate | `115200` (probar; KincoBuilder puede limitar a 9600/19200 — fallback 19200) |
| Formato | `8N1` |

Modelo objetivo: Kinco MK043E-20DT o el modelo equivalente que KincoBuilder muestre para esta CPU.

Todas las variables usadas por el programa se declaran en `VAR_GLOBAL` con direccion absoluta. La tabla local de `MAIN` queda vacia.

## 4. Reglas de Implementacion IL

KincoBuilder usa prefijo `%` en direcciones absolutas:

```text
%I0.0
%Q0.0
%M10.0
%VW100
%VD104
%VB140
%SM0.0
%SMD212
```

Comentarios validos:

```text
(* comentario *)
```

No usar como comentarios:

```text
// comentario
; comentario
```

Las instrucciones de movimiento disparan por flanco ascendente en `EXEC`. Por eso, despues de escribir un comando de inicio desde MODBUS, el ESP32 debe bajar el bit de start y dejar solo enable.

## 5. Mapa MODBUS Principal

Equivalencia usada:

```text
%VW0   -> 40001
%VW100 -> 40051
%VW200 -> 40101
```

Muchas librerias MODBUS usan direccion base 0:

```text
40051 -> address 50
40101 -> address 100
```

### 5.1 Motor 1 / Axis 0

| MODBUS | PLC | Tipo | Funcion |
|---:|---|---|---|
| `40051` | `%VW100` | WORD | Control motor 1 |
| `40053-40054` | `%VD104` | DINT | Posicion absoluta PABS |
| `40055-40056` | `%VD108` | DWORD | Frecuencia maxima PABS |
| `40057` | `%VW112` | WORD | Frecuencia minima PABS |
| `40058` | `%VW114` | WORD | Tiempo aceleracion PABS |
| `40059` | `%VW116` | INT | Modo HOME |
| `40060` | `%VW118` | INT | Direccion HOME |
| `40061` | `%VW120` | WORD | Frecuencia minima HOME |
| `40063-40064` | `%VD124` | DWORD | Frecuencia maxima HOME |
| `40065` | `%VW128` | WORD | Tiempo aceleracion HOME |
| `40066` | `%VW130` | WORD | Estado motor 1 |
| `40067-40068` | `%VD132` | DINT | Posicion actual copiada de `%SMD212` |
| bytes `%VB140-%VB142` | `%VB140-%VB142` | BYTE | Error IDs PABS/HOME/STOP |

### 5.2 Motor 2 / Axis 1

| MODBUS | PLC | Tipo | Funcion |
|---:|---|---|---|
| `40101` | `%VW200` | WORD | Control motor 2 |
| `40103-40104` | `%VD204` | DINT | Posicion absoluta PABS |
| `40105-40106` | `%VD208` | DWORD | Frecuencia maxima PABS |
| `40107` | `%VW212` | WORD | Frecuencia minima PABS |
| `40108` | `%VW214` | WORD | Tiempo aceleracion PABS |
| `40109` | `%VW216` | INT | Modo HOME |
| `40110` | `%VW218` | INT | Direccion HOME |
| `40111` | `%VW220` | WORD | Frecuencia minima HOME |
| `40113-40114` | `%VD224` | DWORD | Frecuencia maxima HOME |
| `40115` | `%VW228` | WORD | Tiempo aceleracion HOME |
| `40116` | `%VW230` | WORD | Estado motor 2 |
| `40117-40118` | `%VD232` | DINT | Posicion actual copiada de `%SMD242` |
| bytes `%VB240-%VB242` | `%VB240-%VB242` | BYTE | Error IDs PABS/HOME/STOP |

Validar endianness de los valores de 32 bits (`%VD`) en el maestro ESP32. Algunas librerias intercambian palabra alta/baja.

## 6. Bits de Control

Los bits son iguales para ambos ejes. En axis 0 se usan `%V100.x`; en axis 1 se usan `%V200.x`.

| Bit | Axis 0 | Axis 1 | Funcion |
|---:|---|---|---|
| 0 | `%V100.0` | `%V200.0` | Enable driver |
| 1 | `%V100.1` | `%V200.1` | Reset posicion logica |
| 2 | `%V100.2` | `%V200.2` | Start PABS |
| 3 | `%V100.3` | `%V200.3` | Start HOME |
| 4 | `%V100.4` | `%V200.4` | Reset estados internos |
| 5 | `%V100.5` | `%V200.5` | Stop |

Valores de comando con enable activo:

| Accion | Valor |
|---|---:|
| Enable | `0x0001` |
| Reset posicion | `0x0003` |
| Start PABS | `0x0005` |
| Start HOME | `0x0009` |
| Stop | `0x0021` |

Despues de `HOME` o `PABS`, volver a escribir `0x0001` para dejar solo enable.

## 7. Bits de Estado

Los bits son iguales para ambos ejes. Axis 0 usa `%VW130`; axis 1 usa `%VW230`.

| Bit | Axis 0 | Axis 1 | Significado |
|---:|---|---|---|
| 0 | `%V130.0` | `%V230.0` | HomeOK |
| 1 | `%V130.1` | `%V230.1` | HomeDone |
| 2 | `%V130.2` | `%V230.2` | HomeErr |
| 3 | `%V130.3` | `%V230.3` | PabsDone |
| 4 | `%V130.4` | `%V230.4` | PabsErr |
| 5 | `%V130.5` | `%V230.5` | AxisBusy |
| 6 | `%V130.6` | `%V230.6` | HomingActive |
| 7 | `%V130.7` | `%V230.7` | PabsActive |
| 8 | `%V131.0` | `%V231.0` | HomeSensor |
| 9 | `%V131.1` | `%V231.1` | SystemReady |
| 10 | `%V131.2` | `%V231.2` | EnableOut |
| 11 | `%V131.3` | `%V231.3` | StopDone |

## 8. Secuencias MODBUS

### 8.1 HOME motor 1

```text
40051 = 0x0001
40059 = 1
40060 = 0
40061 = 200
40063-40064 = 1000
40065 = 300
40051 = 0x0009
40051 = 0x0001
Leer 40066 hasta bit 0 = 1 o bit 2 = 1
```

### 8.2 HOME motor 2

```text
40101 = 0x0001
40109 = 1
40110 = 0
40111 = 200
40113-40114 = 1000
40115 = 300
40101 = 0x0009
40101 = 0x0001
Leer 40116 hasta bit 0 = 1 o bit 2 = 1
```

### 8.3 PABS motor 1

```text
40053-40054 = posicion destino
40055-40056 = MAXF
40057 = MINF
40058 = TIME
40051 = 0x0005
40051 = 0x0001
Leer 40066 hasta bit 3 = 1 o bit 4 = 1
```

### 8.4 PABS motor 2

```text
40103-40104 = posicion destino
40105-40106 = MAXF
40107 = MINF
40108 = TIME
40101 = 0x0005
40101 = 0x0001
Leer 40116 hasta bit 3 = 1 o bit 4 = 1
```

### 8.5 STOP

```text
Motor 1: 40051 = 0x0021
Motor 2: 40101 = 0x0021
Luego volver a enable si corresponde:
Motor 1: 40051 = 0x0001
Motor 2: 40101 = 0x0001
```

## 9. Sintaxis de Bloques Kinco

### 9.1 PABS

```text
PABS AXIS, EXEC, MINF, MAXF, TIME, POS, DONE, ERR, ERRID
```

Tipos:

| Pin | Tipo | Uso en proyecto |
|---|---|---|
| `AXIS` | INT | `0` o `1` |
| `EXEC` | BOOL | Bit de start por flanco |
| `MINF` | WORD | `%VW112` / `%VW212` |
| `MAXF` | DWORD | `%VD108` / `%VD208` |
| `TIME` | WORD | `%VW114` / `%VW214` |
| `POS` | DINT | `%VD104` / `%VD204` |
| `DONE` | BOOL | `%M11.1` / `%M21.1` |
| `ERR` | BOOL | `%M11.2` / `%M21.2` |
| `ERRID` | BYTE | `%VB140` / `%VB240` |

### 9.2 PHOME

```text
PHOME AXIS, EXEC, HOME, NHOME, MODE, DIRC, MINF, MAXF, TIME, DONE, ERR, ERRID
```

Tipos:

| Pin | Tipo | Uso en proyecto |
|---|---|---|
| `AXIS` | INT | `0` o `1` |
| `EXEC` | BOOL | Bit de start por flanco |
| `HOME` | BOOL | `%I0.0` / `%I0.1` |
| `NHOME` | BOOL | `%M10.6` / `%M20.6`, falso |
| `MODE` | INT | `%VW116` / `%VW216` |
| `DIRC` | INT | `%VW118` / `%VW218` |
| `MINF` | WORD | `%VW120` / `%VW220` |
| `MAXF` | DWORD | `%VD124` / `%VD224` |
| `TIME` | WORD | `%VW128` / `%VW228` |
| `DONE` | BOOL | `%M12.1` / `%M22.1` |
| `ERR` | BOOL | `%M12.2` / `%M22.2` |
| `ERRID` | BYTE | `%VB141` / `%VB241` |

Modo HOME:

| Valor | Significado |
|---:|---|
| `0` | Usa `HOME` y `NHOME` |
| `1` | Usa solo `HOME` |

Direccion HOME:

| Valor | Significado |
|---:|---|
| `0` | Forward |
| `1` | Backward |

### 9.3 PSTOP

```text
PSTOP AXIS, EXEC, DONE, ERRID
```

Uso:

```text
PSTOP 0, A0_Ctrl_Stop, A0_M_StopDone, A0_ErrID_STOP
PSTOP 1, A1_Ctrl_Stop, A1_M_StopDone, A1_ErrID_STOP
```

## 10. Resumen del Programa PLC Local

El programa `MAIN_MAIN.ilp` revisado contiene networks `0` a `79`.

| Network | Funcion |
|---:|---|
| 0-1 | Limpieza de bits especiales PTO usados por el entorno Kinco |
| 2-12 | Defaults e inicializacion axis 0 |
| 13-23 | Defaults e inicializacion axis 1 |
| 24-25 | Enable de drivers `%Q0.4` y `%Q0.5` |
| 26-27 | Reset de posicion logica `%SM201.6` y `%SM231.6` |
| 28-29 | `PSTOP` axis 0 y axis 1 |
| 30-31 | Liberacion de stop por software cuando hay start HOME/PABS |
| 32-33 | `PHOME` axis 0 y axis 1 |
| 34-37 | Set/reset de `HomeOK` |
| 38-43 | Calculo de errores negados y `SystemReady` |
| 44-45 | `PABS` axis 0 y axis 1 |
| 46-53 | Marcas internas de movimiento activo |
| 54-55 | Copia de posicion actual `%SMD212/%SMD242` a `%VD132/%VD232` |
| 56-67 | Estado MODBUS axis 0 |
| 68-79 | Estado MODBUS axis 1 |

## 11. Variables Globales Principales

| Simbolo | Direccion | Tipo | Comentario |
|---|---|---|---|
| `A0_STEP` | `%Q0.0` | BOOL | STEP motor 1 |
| `A1_STEP` | `%Q0.1` | BOOL | STEP motor 2 |
| `A0_DIR` | `%Q0.2` | BOOL | DIR motor 1 |
| `A1_DIR` | `%Q0.3` | BOOL | DIR motor 2 |
| `A0_ENABLE_OUT` | `%Q0.4` | BOOL | Enable driver motor 1 |
| `A1_ENABLE_OUT` | `%Q0.5` | BOOL | Enable driver motor 2 |
| `A0_HOME_SENSOR` | `%I0.0` | BOOL | Sensor HOME motor 1 |
| `A1_HOME_SENSOR` | `%I0.1` | BOOL | Sensor HOME motor 2 |
| `A0_Ctrl_Word` | `%VW100` | WORD | Control motor 1 |
| `A1_Ctrl_Word` | `%VW200` | WORD | Control motor 2 |
| `A0_Status_Word` | `%VW130` | WORD | Estado motor 1 |
| `A1_Status_Word` | `%VW230` | WORD | Estado motor 2 |
| `A0_Status_PosActual` | `%VD132` | DINT | Posicion actual motor 1 |
| `A1_Status_PosActual` | `%VD232` | DINT | Posicion actual motor 2 |

## 12. Checklist de Puesta en Marcha

1. Confirmar modelo de PLC seleccionado en KincoBuilder.
2. Configurar COM1 como MODBUS RTU Slave con Station ID `1`.
3. Cargar la tabla global de variables.
4. Compilar el proyecto Kinco sin errores.
5. Descargar programa al PLC.
6. Verificar que `%Q0.0/%Q0.2` mueven STEP/DIR del motor 1.
7. Verificar que `%Q0.1/%Q0.3` mueven STEP/DIR del motor 2.
8. Verificar que `%Q0.4/%Q0.5` habilitan los drivers.
9. Validar polaridad de sensores HOME `%I0.0/%I0.1`.
10. Probar HOME de un eje por vez con baja velocidad.
11. Probar PABS de un eje por vez con recorridos cortos.
12. Leer registros de estado y posicion desde el ESP32.
13. Confirmar endianness de registros de 32 bits.
14. Probar `STOP` durante movimiento.
15. Solo despues de validar un eje, habilitar pruebas simultaneas de ambos motores.

## 13. Extension Reservada PREL/PJOG

El contexto adjunto define una extension final con `PREL` y `PJOG`, incluyendo mapa reservado en `%VD140/%VD144/%VW148/%VW150/%VD152/%VW156` para axis 0 y `%VD240/%VD244/%VW248/%VW250/%VD252/%VW256` para axis 1.

Sin embargo, el archivo local revisado `Final_kinco_project_2motores/MAIN_MAIN.ilp` no contiene las networks `80-121` de esa extension. Si se decide cargar PREL/PJOG en KincoBuilder, hay que:

- Agregar las networks `80-121` del contexto.
- Actualizar `SystemReady` para bloquear tambien por error `PREL` y `PJOG`.
- Actualizar `AxisBusy` para incluir homing, PABS, PREL y PJOG.
- Definir estados adicionales para bits 12-15.
- Confirmar si el mapa final debe migrar estados/posicion desde `%VW130/%VD132` y `%VW230/%VD232` hacia `%VW160/%VD164` y `%VW260/%VD264`.

Comandos reservados de PREL/PJOG:

| Accion | Axis 0 | Axis 1 |
|---|---:|---:|
| PREL | `40051 = 0x0041`, luego `0x0001` | `40101 = 0x0041`, luego `0x0001` |
| JOG Forward | `40051 = 0x0081`, mantener | `40101 = 0x0081`, mantener |
| JOG Backward | `40051 = 0x0101`, mantener | `40101 = 0x0101`, mantener |
| Soltar JOG | `40051 = 0x0001` | `40101 = 0x0001` |

## 14. Pendientes de Validacion en Campo

- Confirmar fisicamente la polaridad y cableado de HOME en ambos ejes.
- Confirmar si los drivers MD-2545 requieren enable activo alto o activo bajo.
- Validar que el maestro ESP32 escriba correctamente los pares de registros `%VD`.
- Definir limites mecanicos y manejo de finales de carrera si se agregan.
- Probar velocidades iniciales bajas antes de usar frecuencias nominales.
- Guardar una copia exportada del proyecto Kinco despues de compilar y descargar.
