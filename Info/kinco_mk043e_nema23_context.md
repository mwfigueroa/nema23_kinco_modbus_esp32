# Kinco MK043E-20DT - NEMA23 / MD-2545 Context

## Objetivo

Controlar un motor NEMA23 con driver MD-2545 usando un PLC Kinco MK043E-20DT, comandos por MODBUS RTU RS-485 y posicionamiento con funciones `PHOME`, `PABS`, `PSTOP` y eventualmente `PJOG`.

## Hardware

| Elemento | Asignacion |
|---|---|
| PLC | Kinco MK043E-20DT |
| Driver motor | MD-2545 |
| Motor | NEMA23 |
| Comunicacion | MODBUS RTU por RS-485 COM1 |
| Eje usado | `AXIS = 0` / `K0` |
| PTO | `Q0.0` |
| Direccion PTO0 | `Q0.2` segun help Kinco |
| Enable driver | `Q0.3` |
| Sensor Home | `I0.0` |
| Near Home opcional | `I0.1` |

## Software Local Encontrado

KincoBuilder esta instalado en Windows en:

```text
C:\Program Files (x86)\Kinco\KincoBuilderEn\
```

Ejecutable:

```text
C:\Program Files (x86)\Kinco\KincoBuilderEn\KincoBuilderV85En.exe
```

Archivo de ayuda:

```text
C:\Program Files (x86)\Kinco\KincoBuilderEn\KincoBuilder_V1.1.5.0En.chm
```

Desde WSL/Linux esta accesible en:

```text
/mnt/c/Program Files (x86)/Kinco/KincoBuilderEn/KincoBuilder_V1.1.5.0En.chm
```

El help fue extraido temporalmente en:

```text
/tmp/kinco_help/
```

Secciones relevantes del help:

```text
/tmp/kinco_help/html-all/6Zhang/96DingWeiKongZhi/03PHOME.htm
/tmp/kinco_help/html-all/6Zhang/96DingWeiKongZhi/04PABS.htm
/tmp/kinco_help/html-all/6Zhang/96DingWeiKongZhi/05PREL.htm
/tmp/kinco_help/html-all/6Zhang/96DingWeiKongZhi/07PSTOP.htm
/tmp/kinco_help/html-all/6Zhang/96DingWeiKongZhi/08LiZi.htm
```

## Sintaxis IL Correcta Confirmada

El formato IL de KincoBuilder usa prefijo `%` en variables:

```text
%I0.0
%Q0.0
%M1.0
%VW100
%VD100
%VB100
%SM0.0
%SM201.7
```

Comentarios correctos:

```text
(* comentario *)
```

No sirven como comentarios:

```text
// comentario
; comentario
```

Instrucciones confirmadas en ejemplos del help:

```text
LD      %SM0.0
MOVE    W#400, %VW300
MOVE    DW#5000, %VD302
MOVE    DI#16000, %VD306
ANDN    %I0.4
OR      %I0.4
ST      %M10.0
R       %SM201.7
PABS    0, %I0.0, %VW300, %VD302, %VW304, %VD306, %M2.0, %M2.1, %VB2
PHOME   0, %I0.2, %I0.5, %I0.6, %VW396, %VW398, %VW400, %VD2402, %VW404, %M3.0, %M3.1, %VB3
PSTOP   0, %I0.7, %M5.0, %VB5
```

`EU` no es instruccion valida. El help lista detector de flanco como `R_TRIG`, pero las instrucciones de posicionamiento ejecutan por flanco ascendente de `EXEC`, asi que no siempre hace falta generar pulso aparte.

## PABS Confirmado

Sintaxis IL oficial:

```text
PABS AXIS, EXEC, MINF, MAXF, TIME, POS, DONE, ERR, ERRID
```

Ejemplo oficial:

```text
PABS 0, %I0.0, %VW300, %VD2302, %VW304, %VD306, %M2.0, %M2.1, %VB2
```

Tipos de datos:

| Pin | Tipo | Nota |
|---|---|---|
| `AXIS` | INT | Constante `0` o `1` |
| `EXEC` | BOOL | Ejecuta por flanco ascendente si `EN`/CR esta activo |
| `MINF` | WORD | Frecuencia inicial, minimo 125 Hz |
| `MAXF` | DWORD | Frecuencia maxima, usar `%VD` |
| `TIME` | WORD | Aceleracion/desaceleracion en ms |
| `POS` | DINT | Posicion destino absoluta en pulsos, usar `%VD` |
| `DONE` | BOOL | Movimiento finalizado |
| `ERR` | BOOL | Error durante ejecucion |
| `ERRID` | BYTE | Codigo de error, usar `%VB` |

Nota critica del help: `MINF`, `MAXF`, `TIME`, `POS` deben ser todos constantes o todos variables al mismo tiempo. Para proyecto MODBUS conviene que todos sean variables.

## PHOME Confirmado

Sintaxis IL oficial:

```text
PHOME AXIS, EXEC, HOME, NHOME, MODE, DIRC, MINF, MAXF, TIME, DONE, ERR, ERRID
```

Ejemplo oficial:

```text
PHOME 0, %I0.2, %I0.5, %I0.6, %VW396, %VW398, %VW400, %VD2402, %VW404, %M3.0, %M3.1, %VB3
```

`MODE`:

| Valor | Significado |
|---:|---|
| `0` | Usa `HOME` y `NHOME` |
| `1` | Usa solo `HOME` |

`DIRC`:

| Valor | Significado |
|---:|---|
| `0` | Forward |
| `1` | Backward |

## Registros Especiales Relevantes

| Registro | Funcion |
|---|---|
| `%SM0.0` | Siempre ON |
| `%SM0.1` | Primer scan |
| `%SM201.6` | Reset valor actual PTO0: `1` limpia `%SMD212`, `0` mantiene |
| `%SM201.7` | Emergency stop / stop por software PTO0 |
| `%SMD212` | Posicion actual PTO0 |
| `%SMD242` | Posicion actual PTO1 |
| `%SM66.7` | Estado PTO0 segun contexto previo |
| `%SM76.7` | Estado PTO1 segun contexto previo |

## Registros MODBUS Propuestos

El ESP32 debe actuar como MODBUS RTU Master y la Kinco MK043E-20DT como MODBUS RTU Slave. El ESP32 no envia `PABS` directamente; escribe registros del area `%V` y el ladder del PLC ejecuta la logica de movimiento.

Configuracion sugerida de COM1 en KincoBuilder:

```text
Protocol: MODBUS RTU Slave
Station ID: 1
Baudrate: 9600 o 19200
Data: 8N1
```

## Capacidad de Registros MODBUS Accesibles

Para la MK043E-20DT, el area de retencion `%V` reportada para el modelo es:

```text
VB0 - VB1907 = 1908 bytes
```

Equivalencia MODBUS Holding Registers:

```text
1 holding register = 16 bits = 2 bytes
1908 bytes / 2 = 954 registros
```

Mapa aproximado:

```text
%VW0    -> 40001
%VW2    -> 40002
%VW4    -> 40003
...
%VW1906 -> 40954
```

Para 32 bits:

```text
%VD0   -> 40001-40002
%VD100 -> 40051-40052
%VD104 -> 40053-40054
```

Resumen:

| Memoria Kinco | MODBUS aproximado | Cantidad |
|---|---:|---:|
| `%VB0` a `%VB1907` | bytes internos | 1908 bytes |
| `%VW0` a `%VW1906` | `40001` a `40954` | 954 words |
| `%VD0` a `%VD1904` | pares de registros | 477 double words |

Recomendacion: para acceso desde ESP32 usar solo `%V`, `%VW`, `%VD`, `%VB`. Si se necesita leer `%M`, `%I`, `%Q`, `%SM` o `%SMD`, copiarlos previamente a variables `%V` desde ladder.

Muchas librerias MODBUS en ESP32 usan direccion base 0:

```text
MODBUS 40001 -> address 0
MODBUS 40051 -> address 50
MODBUS 40152 -> address 151
```

Confirmar endianness para valores de 32 bits (`%VD`): algunas librerias o HMIs usan palabra alta/baja invertida.

### Control desde maestro MODBUS

| MODBUS | PLC | Tipo | Descripcion |
|---|---|---|---|
| `40051-40052` | `%VD100` | DINT | Posicion absoluta destino para PABS |
| `40053-40054` | `%VD104` | DWORD | `MAXF` para PABS, corregido a 32 bits |
| `40055` | `%VW108` | WORD | `MINF` para PABS |
| `40056` | `%VW110` | WORD | `TIME` para PABS |
| `40057` | `%VW112` | WORD | `MODE` para PHOME |
| `40058` | `%VW114` | WORD | `DIRC` para PHOME |
| `40059` | `%VW116` | WORD | `MINF` para PHOME |
| `40060-40061` | `%VD118` | DWORD | `MAXF` para PHOME, corregido a 32 bits |
| `40062` | `%VW122` | WORD | `TIME` para PHOME |
| `40070` | `%VW138` | WORD | Palabra de control corregida para evitar solape |

Nota: el contexto inicial usaba `%VW104` y `%VW118` para `MAXF`, pero el help confirma que `MAXF` es `DWORD`, por lo tanto conviene moverlos a `%VD104` y `%VD118`.

Nota critica de solapamiento: si `%VD104` se mapea como `40053-40054`, entonces no usar `40054` como palabra de control porque pisa la segunda palabra de `MAXF`. Por eso se propone mover control a `%VW138` / `40070`.

### Palabra de control sugerida

| Bit | Nombre | Funcion |
|---|---|---|
| `%V138.0` | `CmdEnable` | Habilita driver |
| `%V138.1` | `CmdResetPosition` | Reset logico de posicion |
| `%V138.2` | `CmdStartPABS` | Start movimiento absoluto |
| `%V138.3` | `CmdStartHome` | Start homing |
| `%V138.4` | `CmdResetStatus` | Reset estados/errores |
| `%V138.5` | `CmdStop` | Stop/cancelar movimiento |

Secuencia MODBUS PABS desde ESP32:

```text
1. Escribir POS en 40051-40052 (%VD100)
2. Escribir MAXF en 40053-40054 (%VD104)
3. Escribir MINF en 40055 (%VW108)
4. Escribir TIME en 40056 (%VW110)
5. Enable: 40070 = 0x0001
6. Start PABS: 40070 = 0x0005  ; Enable + StartPABS
7. Bajar start: 40070 = 0x0001
8. Leer estado general en 40152 (%VW302)
```

Bits esperados en estado general si se implementa `%VW302`:

```text
%V302.3 = PABS DONE, bit 3 de %VW302
%V302.4 = PABS ERR, bit 4 de %VW302
%V302.7 = PABS activo, bit 7 de %VW302
```

## Ladder MODBUS con Sensor HOME Externo

Objetivo: controlar el eje desde ESP32 por MODBUS RTU, usando el sensor fisico de cero conectado a `%I0.0`. El ESP32 manda comandos escribiendo `%VW138` y parametros de movimiento en `%V`. El PLC ejecuta `PHOME`, `PABS` y `PSTOP`.

### Asignacion de senales

| Senal | PLC | Origen / destino | Nota |
|---|---|---|---|
| Sensor HOME externo | `%I0.0` | Entrada fisica | Sensor de cero |
| STEP / PUL | `%Q0.0` | Salida PTO interna | Controlada por `PHOME/PABS AXIS=0` |
| DIR | `%Q0.2` | Salida PTO interna | Controlada por `PHOME/PABS AXIS=0` |
| Enable driver | `%Q0.3` | Salida fisica | Revisar conflicto si se usa eje 1 |
| Control MODBUS | `%VW138` | ESP32 escribe | Registro `40070` |
| Estado MODBUS | `%VW302` | ESP32 lee | Registro `40152` |
| Posicion actual copiada | `%VD200` | ESP32 lee | Registro `40101-40102` |

### Bits de control desde ESP32

| Bit | Nombre | Funcion |
|---|---|---|
| `%V138.0` | `CmdEnable` | Habilita salida `%Q0.3` |
| `%V138.1` | `CmdResetPosition` | Reset logico de posicion PTO0 |
| `%V138.2` | `CmdStartPABS` | Ejecuta `PABS` por flanco ascendente |
| `%V138.3` | `CmdStartHome` | Ejecuta `PHOME` por flanco ascendente |
| `%V138.4` | `CmdResetStatus` | Limpia estados internos |
| `%V138.5` | `CmdStop` | Ejecuta `PSTOP` |

### Bits de estado hacia ESP32

| Bit | Nombre | Significado |
|---|---|---|
| `%V302.0` | `HomeOK` | Homing terminado correctamente, bit 0 de `%VW302` |
| `%V302.1` | `HomeDone` | `PHOME DONE`, bit 1 de `%VW302` |
| `%V302.2` | `HomeErr` | Error en `PHOME`, bit 2 de `%VW302` |
| `%V302.3` | `PabsDone` | `PABS DONE`, bit 3 de `%VW302` |
| `%V302.4` | `PabsErr` | Error en `PABS`, bit 4 de `%VW302` |
| `%V302.5` | `Pto0Status` | Copia de `%SM66.7`, bit 5 de `%VW302` |
| `%V302.6` | `HomingActive` | Homing en curso, bit 6 de `%VW302` |
| `%V302.7` | `PabsActive` | Movimiento absoluto en curso, bit 7 de `%VW302` |
| `%V303.0` | `HomeSensor` | Copia de `%I0.0`, bit 8 de `%VW302` |
| `%V303.1` | `SystemReady` | Enable + HomeOK + sin errores, bit 9 de `%VW302` |

### Marcas internas auxiliares

| Marca | Funcion |
|---|---|
| `%M1.0` | `HomeOK` interno |
| `%M1.1` | Homing activo |
| `%M1.2` | PABS activo |
| `%M1.3` | Sistema listo para PABS |
| `%M1.4` | Auxiliar sin error PHOME (`NOT %M3.2`) |
| `%M1.5` | Auxiliar sin error PABS (`NOT %M2.2`) |
| `%M2.1` | `PABS DONE` |
| `%M2.2` | `PABS ERR` |
| `%M3.1` | `PHOME DONE` |
| `%M3.2` | `PHOME ERR` |
| `%M10.0` | `NHOME` falso/no usado |

### Regla practica KincoBuilder

Para evitar errores de network invalida, usar una sola bobina o un solo bloque principal por network. Si hay que hacer varias acciones con la misma condicion, dividir en networks separadas. Si hay dos condiciones para una misma bobina/reset, unirlas con `OR` dentro de la misma network.

### Tabla de variables globales / simbolos

Sugerencia para la tabla de simbolos en KincoBuilder.

#### CTRL (Control, ESP32 escribe en MODBUS)

| Direccion | Simbolo | Tipo | Descripcion |
|---|---|---|---|
| `%VW138` | `Ctrl_Word` | WORD | Palabra de control (40070) |
| `%V138.0` | `Ctrl_Enable` | BOOL | Habilita driver |
| `%V138.1` | `Ctrl_ResetPos` | BOOL | Reset posicion logica |
| `%V138.2` | `Ctrl_StartPABS` | BOOL | Inicia movimiento absoluto |
| `%V138.3` | `Ctrl_StartHome` | BOOL | Inicia homing |
| `%V138.4` | `Ctrl_ResetStatus` | BOOL | Limpia estados |
| `%V138.5` | `Ctrl_Stop` | BOOL | Stop del eje |

#### PARAM (Parametros, ESP32 escribe en MODBUS)

| Direccion | Simbolo | Tipo | Descripcion |
|---|---|---|---|
| `%VD100` | `Param_POS` | DINT | Posicion destino PABS (40051-40052) |
| `%VD104` | `Param_MAXF_PABS` | DWORD | Frecuencia maxima PABS (40053-40054) |
| `%VW108` | `Param_MINF_PABS` | WORD | Frecuencia minima PABS (40055) |
| `%VW110` | `Param_TIME_PABS` | WORD | Aceleracion PABS (40056) |
| `%VW112` | `Param_MODE_HOME` | INT | Modo homing (40057) |
| `%VW114` | `Param_DIRC_HOME` | INT | Direccion homing (40058) |
| `%VW116` | `Param_MINF_HOME` | WORD | Frecuencia minima HOME (40059) |
| `%VD118` | `Param_MAXF_HOME` | DWORD | Frecuencia maxima HOME (40060-40061) |
| `%VW122` | `Param_TIME_HOME` | WORD | Aceleracion HOME (40062) |

#### STATUS (Estado, ESP32 lee de MODBUS)

| Direccion | Simbolo | Tipo | Descripcion |
|---|---|---|---|
| `%VW302` | `Status_Word` | WORD | Palabra de estado (40152) |
| `%V302.0` | `Status_HomeOK` | BOOL | Homing correcto |
| `%V302.1` | `Status_HomeDone` | BOOL | PHOME termino |
| `%V302.2` | `Status_HomeErr` | BOOL | Error PHOME |
| `%V302.3` | `Status_PabsDone` | BOOL | PABS termino |
| `%V302.4` | `Status_PabsErr` | BOOL | Error PABS |
| `%V302.5` | `Status_PTO0` | BOOL | Estado PTO0 |
| `%V302.6` | `Status_HomingAct` | BOOL | Homing en curso |
| `%V302.7` | `Status_PabsAct` | BOOL | PABS en curso |
| `%V303.0` | `Status_HomeSensor` | BOOL | Sensor HOME fisico |
| `%V303.1` | `Status_SystemReady` | BOOL | Sistema listo |
| `%VD200` | `Status_PosActual` | DINT | Posicion actual (40101-40102) |

#### INTERNAL (Marcas internas, solo PLC)

| Direccion | Simbolo | Tipo | Descripcion |
|---|---|---|---|
| `%M1.0` | `M_HomeOK` | BOOL | HomeOK interno |
| `%M1.1` | `M_HomingActive` | BOOL | Homing activo |
| `%M1.2` | `M_PabsActive` | BOOL | PABS activo |
| `%M1.3` | `M_SystemReady` | BOOL | Sistema listo |
| `%M1.4` | `M_NoHomeErr` | BOOL | Sin error PHOME |
| `%M1.5` | `M_NoPabsErr` | BOOL | Sin error PABS |
| `%M2.1` | `M_PabsDone` | BOOL | PABS DONE |
| `%M2.2` | `M_PabsErr` | BOOL | PABS ERR |
| `%M3.1` | `M_HomeDone` | BOOL | PHOME DONE |
| `%M3.2` | `M_HomeErr` | BOOL | PHOME ERR |
| `%M10.0` | `M_NHOME` | BOOL | NHOME no usado |

#### HARDWARE (Fijo del equipo)

| Direccion | Simbolo | Tipo | Descripcion |
|---|---|---|---|
| `%I0.0` | `Hw_HomeSensor` | BOOL | Sensor HOME externo |
| `%Q0.0` | `Hw_StepPul` | BOOL | STEP/PUL PTO0 |
| `%Q0.2` | `Hw_Dir` | BOOL | DIR PTO0 |
| `%Q0.3` | `Hw_DrvEnable` | BOOL | Enable driver |

#### REGISTROS ESPECIALES (Solo lectura/logica interna)

| Direccion | Simbolo | Tipo | Descripcion |
|---|---|---|---|
| `%SM0.0` | `Sys_AlwaysON` | BOOL | Siempre ON |
| `%SM0.1` | `Sys_FirstScan` | BOOL | Primer scan |
| `%SM201.6` | `Sys_ResetPos` | BOOL | Reset posicion PTO0 |
| `%SM201.7` | `Sys_EmergStop` | BOOL | Stop PTO0 |
| `%SMD212` | `Sys_PosActual` | DINT | Posicion actual PTO0 |
| `%SM66.7` | `Sys_PTO0_Status` | BOOL | Estado PTO0 |

### Donde declarar cada variable en KincoBuilder

Todas las variables de este proyecto usan direcciones absolutas del PLC (`%V`, `%M`, `%I`, `%Q`, `%SM`, `%SMD`). Por lo tanto van en la **Global Variable Table (VAR_GLOBAL)**, que tiene las columnas `Symbol | Address | Data Type | Comment`.

La **Local Variable Table de MAIN** queda vacia, porque no usamos variables simbolicas puras sin direccion fija.

```
VAR_GLOBAL  ->  todo (CTRL, PARAM, STATUS, INTERNAL, HARDWARE, REGISTROS ESPECIALES)
MAIN local  ->  vacio
```

### Confirmado: MK043E-20DT soporta variables en PABS/PHOME

El programa compila correctamente con parametros variables. Los errores anteriores eran solo por tipos mal declarados. El MK043E-20DT SI permite que la posicion (`POS`) y la velocidad (`MINF`, `MAXF`, `TIME`) se lean desde `%V` por MODBUS.

### Cuidado con INT vs WORD

Ambos ocupan 16 bits pero la interpretacion es distinta. Si el compilador espera `INT` y recibe `WORD`, tira error.

| Tipo | Signo | Rango | Prefijo IL | Ejemplo |
|---|---|---|---|---|
| INT | Con signo | -32768 a 32767 | `I#` | `MOVE I#1, %VW112` |
| WORD | Sin signo | 0 a 65535 | `W#` | `MOVE W#300, %VW108` |
| DINT | 32 bits con signo | -2^31 a 2^31-1 | `DI#` | `MOVE DI#0, %VD100` |
| DWORD | 32 bits sin signo | 0 a 2^32-1 | `DW#` | `MOVE DW#2000, %VD104` |

En PHOME: `MODE` y `DIRC` son `INT`, el resto `WORD`/`DWORD`.
En PABS: `POS` es `DINT`, `MINF`/`TIME` son `WORD`, `MAXF` es `DWORD`.

### Network 0 - POS PABS default

```text
   %SM0.1
----| |------------[ MOVE DI#0 -> %VD100 ]
```

IL:

```text
LD     %SM0.1
MOVE   DI#0, %VD100
```

### Network 1 - MAXF PABS default

```text
   %SM0.1
----| |------------[ MOVE DW#2000 -> %VD104 ]
```

IL:

```text
LD     %SM0.1
MOVE   DW#2000, %VD104
```

### Network 2 - MINF PABS default

```text
   %SM0.1
----| |------------[ MOVE W#300 -> %VW108 ]
```

IL:

```text
LD     %SM0.1
MOVE   W#300, %VW108
```

### Network 3 - TIME PABS default

```text
   %SM0.1
----| |------------[ MOVE W#300 -> %VW110 ]
```

IL:

```text
LD     %SM0.1
MOVE   W#300, %VW110
```

### Network 4 - MODE HOME default

```text
   %SM0.1
----| |------------[ MOVE I#1 -> %VW112 ]
```

IL:

```text
LD     %SM0.1
MOVE   I#1, %VW112
```

### Network 5 - DIRC HOME default

```text
   %SM0.1
----| |------------[ MOVE I#0 -> %VW114 ]
```

IL:

```text
LD     %SM0.1
MOVE   I#0, %VW114
```

### Network 6 - MINF HOME default

```text
   %SM0.1
----| |------------[ MOVE W#200 -> %VW116 ]
```

IL:

```text
LD     %SM0.1
MOVE   W#200, %VW116
```

### Network 7 - MAXF HOME default

```text
   %SM0.1
----| |------------[ MOVE DW#1000 -> %VD118 ]
```

IL:

```text
LD     %SM0.1
MOVE   DW#1000, %VD118
```

### Network 8 - TIME HOME default

```text
   %SM0.1
----| |------------[ MOVE W#300 -> %VW122 ]
```

IL:

```text
LD     %SM0.1
MOVE   W#300, %VW122
```

### Network 9 - NHOME falso/no usado

```text
   %SM0.1
----| |--------------------------------------------( R %M10.0 )
```

IL:

```text
LD     %SM0.1
R      %M10.0
```

### Network 10 - HomeOK inicial apagado

```text
   %SM0.1
----| |--------------------------------------------( R %M1.0 )
```

IL:

```text
LD     %SM0.1
R      %M1.0
```

### Network 11 - Enable driver

```text
   %V138.0
----| |--------------------------------------------( %Q0.3 )
```

IL:

```text
LD     %V138.0
ST     %Q0.3
```

### Network 12 - Reset posicion logica PTO0

```text
   %V138.1
----| |--------------------------------------------( %SM201.6 )
```

IL:

```text
LD     %V138.1
ST     %SM201.6
```

Nota: `%SM201.6` esta confirmado en el help como reset del valor actual del PTO0. Con `1` limpia `%SMD212`; con `0` mantiene la posicion actual.

### Network 13 - PSTOP

```text
   %SM0.0
----| |-----+------------------------------------------------+
            | PSTOP                                          |
            |                                                |
            | AXIS  = 0                                      |
            | EXEC  = %V138.5                                |
            | DONE  = %M5.0                                  |
            | ERRID = %VB5                                   |
            +------------------------------------------------+
```

IL:

```text
LD     %SM0.0
PSTOP  0, %V138.5, %M5.0, %VB5
```

### Network 14 - Reset stop antes de mover

```text
   %V138.2
----| |----+
          |
   %V138.3|
----| |----+---------------------------------------( R %SM201.7 )
```

IL equivalente valido para una sola network:

```text
LD     %V138.2
OR     %V138.3
R      %SM201.7
```

Nota: evitar dos secuencias separadas `LD/R` dentro de la misma network. Si se quiere hacerlo separado, crear dos networks distintas.

### Network 15 - PHOME desde MODBUS

```text
   %V138.0
----| |-----+------------------------------------------------+
            | PHOME                                          |
            |                                                |
            | AXIS  = 0                                      |
            | EXEC  = %V138.3      ; StartHome desde ESP32   |
            | HOME  = %I0.0        ; sensor cero fisico      |
            | NHOME = %M10.0       ; no usado, siempre 0     |
            | MODE  = %VW112       ; 1 = solo HOME           |
            | DIRC  = %VW114       ; sentido de busqueda     |
            | MINF  = %VW116                                 |
            | MAXF  = %VD118                                 |
            | TIME  = %VW122                                 |
            | DONE  = %M3.1        ; PHOME terminado         |
            | ERR   = %M3.2        ; error PHOME             |
            | ERRID = %VB306       ; codigo error PHOME      |
            +------------------------------------------------+
```

IL:

```text
LD     %V138.0
PHOME  0, %V138.3, %I0.0, %M10.0, %VW112, %VW114, %VW116, %VD118, %VW122, %M3.1, %M3.2, %VB306
```

### Network 16 - Set HomeOK

```text
   %M3.2       %M3.1
----|/|--------| |---------------------------------( S %M1.0 )
```

IL:

```text
LDN    %M3.2
AND    %M3.1
S      %M1.0
```

### Network 17 - Reset HomeOK

```text
   %V138.4
----| |----+
          |
   %V138.1|
----| |----+---------------------------------------( R %M1.0 )
```

IL:

```text
LD     %V138.4
OR     %V138.1
R      %M1.0
```

Nota: `ANDN` existe segun el help, pero si KincoBuilder marca error, usar la forma equivalente `LDN %M3.2` + `AND %M3.1`.

Nota: se resetea `HomeOK` tambien con `CmdResetPosition`, porque si se cambia el cero logico conviene obligar a hacer HOME otra vez.

### Network 18 - Auxiliar sin error PHOME

```text
   %M3.2
----|/|--------------------------------------------( %M1.4 )
```

IL:

```text
LDN    %M3.2
ST     %M1.4
```

### Network 19 - Auxiliar sin error PABS

```text
   %M2.2
----|/|--------------------------------------------( %M1.5 )
```

IL:

```text
LDN    %M2.2
ST     %M1.5
```

### Network 20 - Sistema listo para PABS

```text
   %V138.0      %M1.0       %M1.4       %M1.5
----| |---------| |---------| |---------| |--------( %M1.3 )
```

IL:

```text
LD     %V138.0
AND    %M1.0
AND    %M1.4
AND    %M1.5
ST     %M1.3
```

### Network 21 - PABS desde MODBUS

```text
   %M1.3
----| |-----+------------------------------------------------+
            | PABS                                           |
            |                                                |
            | AXIS  = 0                                      |
            | EXEC  = %V138.2      ; StartPABS desde ESP32   |
            | MINF  = %VW108                                 |
            | MAXF  = %VD104                                 |
            | TIME  = %VW110                                 |
            | POS   = %VD100       ; destino desde ESP32     |
            | DONE  = %M2.1        ; PABS terminado          |
            | ERR   = %M2.2        ; error PABS              |
            | ERRID = %VB304       ; codigo error PABS       |
            +------------------------------------------------+
```

IL:

```text
LD     %M1.3
PABS   0, %V138.2, %VW108, %VD104, %VW110, %VD100, %M2.1, %M2.2, %VB304
```

### Network 22 - Set homing activo

```text
   %V138.3
----| |--------------------------------------------( S %M1.1 )
```

IL:

```text
LD     %V138.3
S      %M1.1
```

### Network 23 - Reset homing activo

```text

   %M3.1
----| |----+
          |
   %M3.2  |
----| |----+---------------------------------------( R %M1.1 )
```

IL:

```text
LD     %M3.1
OR     %M3.2
R      %M1.1
```

### Network 24 - Set PABS activo

```text
   %V138.2      %M1.3
----| |---------| |--------------------------------( S %M1.2 )
```

IL:

```text
LD     %V138.2
AND    %M1.3
S      %M1.2
```

### Network 25 - Reset PABS activo

```text

   %M2.1
----| |----+
          |
   %M2.2  |
----| |----+---------------------------------------( R %M1.2 )
```

IL:

```text
LD     %M2.1
OR     %M2.2
R      %M1.2
```

### Network 26 - Copiar posicion actual para MODBUS

```text
   %SM0.0
----| |------------[ MOVE %SMD212 -> %VD200 ]
```

IL:

```text
LD     %SM0.0
MOVE   %SMD212, %VD200
```

### Network 27 - Estado HomeOK hacia ESP32

```text
   %M1.0
----| |--------------------------------------------( %V302.0 )
```

IL:

```text
LD     %M1.0
ST     %V302.0
```

### Network 28 - Estado PHOME DONE hacia ESP32

```text

   %M3.1
----| |--------------------------------------------( %V302.1 )
```

IL:

```text
LD     %M3.1
ST     %V302.1
```

### Network 29 - Estado PHOME ERR hacia ESP32

```text

   %M3.2
----| |--------------------------------------------( %V302.2 )
```

IL:

```text
LD     %M3.2
ST     %V302.2
```

### Network 30 - Estado PABS DONE hacia ESP32

```text

   %M2.1
----| |--------------------------------------------( %V302.3 )
```

IL:

```text
LD     %M2.1
ST     %V302.3
```

### Network 31 - Estado PABS ERR hacia ESP32

```text

   %M2.2
----| |--------------------------------------------( %V302.4 )
```

IL:

```text
LD     %M2.2
ST     %V302.4
```

### Network 32 - Estado PTO0 hacia ESP32

```text

   %SM66.7
----| |--------------------------------------------( %V302.5 )
```

IL:

```text
LD     %SM66.7
ST     %V302.5
```

### Network 33 - Estado homing activo hacia ESP32

```text

   %M1.1
----| |--------------------------------------------( %V302.6 )
```

IL:

```text
LD     %M1.1
ST     %V302.6
```

### Network 34 - Estado PABS activo hacia ESP32

```text

   %M1.2
----| |--------------------------------------------( %V302.7 )
```

IL:

```text
LD     %M1.2
ST     %V302.7
```

### Network 35 - Sensor HOME hacia ESP32

```text

   %I0.0
----| |--------------------------------------------( %V303.0 )
```

IL:

```text
LD     %I0.0
ST     %V303.0
```

### Network 36 - Sistema listo hacia ESP32

```text

   %M1.3
----| |--------------------------------------------( %V303.1 )
```

IL:

```text
LD     %M1.3
ST     %V303.1
```

### Secuencia ESP32 recomendada

```text
1. Enable driver:
   40070 = 0x0001

2. Ejecutar HOME:
   40070 = 0x0009   ; Enable + StartHome
   40070 = 0x0001   ; bajar StartHome para dejar listo otro flanco

3. Esperar HOME OK:
   leer 40152 hasta que bit 0 = 1
   si bit 2 = 1, hubo error PHOME

4. Cargar destino y velocidad PABS:
   40051-40052 = posicion absoluta en pasos, DINT
   40053-40054 = MAXF PABS, DWORD
   40055       = MINF PABS, WORD
   40056       = TIME PABS, WORD

5. Ejecutar PABS:
   40070 = 0x0005   ; Enable + StartPABS
   40070 = 0x0001   ; bajar StartPABS

6. Esperar fin de movimiento:
   leer 40152 hasta que bit 3 = 1 o bit 4 = 1

7. Leer posicion actual:
   40101-40102 = %VD200, copia de %SMD212
```

Punto importante: `PHOME` y `PABS` ejecutan por flanco ascendente de `EXEC`. Por eso el ESP32 debe subir y bajar `CmdStartHome` o `CmdStartPABS`; si deja el bit clavado en 1, no se genera un nuevo arranque.

### Donde se mandan los pulsos desde ESP32

Los pulsos de comando no son pulsos electricos a entradas fisicas del PLC. Son pulsos logicos por MODBUS sobre la palabra de control `%VW138`, accesible como holding register `40070`.

El ESP32 debe escribir `1` en el bit correspondiente y luego volverlo a `0`, manteniendo normalmente el bit de enable `%V138.0` activo. En KincoBuilder los bits del area `%V` se escriben como `%V138.0`, `%V138.1`, etc.; la palabra completa MODBUS sigue siendo `%VW138`.

| Comando | Bit PLC | Registro MODBUS | Valor con Enable activo |
|---|---|---:|---:|
| Reset posicion | `%V138.1` | `40070` | `0x0003` |
| Start PABS | `%V138.2` | `40070` | `0x0005` |
| Start HOME | `%V138.3` | `40070` | `0x0009` |
| Stop | `%V138.5` | `40070` | `0x0021` |

Ejemplo para HOME:

```text
ESP32 escribe 40070 = 0x0009   ; Enable + StartHome
espera aprox. 100 ms
ESP32 escribe 40070 = 0x0001   ; deja Enable, baja StartHome
```

Ese pulso aparece dentro del PLC como `%V138.3` y entra al bloque:

```text
PHOME EXEC = %V138.3
```

Ejemplo para PABS:

```text
ESP32 escribe 40070 = 0x0005   ; Enable + StartPABS
espera aprox. 100 ms
ESP32 escribe 40070 = 0x0001   ; deja Enable, baja StartPABS
```

Ese pulso aparece dentro del PLC como `%V138.2` y entra al bloque:

```text
PABS EXEC = %V138.2
```

Resumen de flujo:

```text
ESP32 MODBUS -> 40070 -> %VW138 -> bits EXEC de PHOME/PABS
```

## Programa Simple Probado Conceptualmente: 1000 Pasos Ida y Vuelta

Objetivo: al activar `%I0.0`, ir a posicion absoluta `1000` y luego volver automaticamente a `0`.

```text
(* Network 0 - Parametros iniciales *)
LD     %SM0.1
MOVE   W#300, %VW300
MOVE   DW#2000, %VD302
MOVE   W#300, %VW304
MOVE   DI#1000, %VD306
MOVE   DI#0, %VD316

(* Network 1 - Enable driver *)
LD     %SM0.0
ST     %Q0.3

(* Network 2 - Reset emergency stop al iniciar *)
LD     %I0.0
R      %SM201.7

(* Network 3 - Movimiento horario: ir a posicion 1000 *)
LD     %SM0.0
PABS   0, %I0.0, %VW300, %VD302, %VW304, %VD306, %M2.0, %M2.1, %VB2

(* Network 4 - Movimiento antihorario: volver a posicion 0 *)
LD     %SM0.0
PABS   0, %M2.0, %VW300, %VD302, %VW304, %VD316, %M3.0, %M3.1, %VB3

END
```

Si KincoBuilder marca desconocido un bloque como `PREL`, usar `PABS`, que esta confirmado en el help y en el flujo actual.

## Diagrama Ladder Conceptual Completo: 1000 Pasos Ida/Vuelta

Objetivo: al activar `%I0.0`, ir a posicion absoluta `1000` y luego volver automaticamente a `0`.

### Network 0 - Parametros Iniciales

```text
   %SM0.1
----| |------------[ MOVE W#300    -> %VW300 ]     ; MINF
                  [ MOVE DW#2000   -> %VD302 ]     ; MAXF
                  [ MOVE W#300     -> %VW304 ]     ; TIME
                  [ MOVE DI#1000   -> %VD306 ]     ; POS ida
                  [ MOVE DI#0      -> %VD316 ]     ; POS vuelta
```

### Network 1 - Enable Driver

```text
   %SM0.0
----| |--------------------------------------------( %Q0.3 )
```

Nota: revisar conflicto de `Q0.3`. Para `AXIS = 0`, el help indica `Q0.0` como STEP y `Q0.2` como DIR. Para `AXIS = 1`, puede usarse `Q0.1` como STEP y `Q0.3` como DIR. Si se usa solo `AXIS = 0`, `Q0.3` queda disponible para enable, pero conviene confirmarlo en hardware/configuracion.

### Network 2 - Reset Stop / Emergency Stop PTO0

```text
   %I0.0
----| |--------------------------------------------( R %SM201.7 )
```

### Network 3 - PABS Ida: Posicion +1000

```text
   %SM0.0
----| |-----+------------------------------------------------+
            | PABS                                           |
            |                                                |
            | AXIS  = 0                                      |
            | EXEC  = %I0.0        ; boton start             |
            | MINF  = %VW300       ; 300 Hz                  |
            | MAXF  = %VD302       ; 2000 Hz, DWORD          |
            | TIME  = %VW304       ; 300 ms                  |
            | POS   = %VD306       ; +1000 pulsos, DINT      |
            |                                                |
            | ENO   = %M2.2        ; solo si Ladder lo expone |
            | DONE  = %M2.0        ; termina ida             |
            | ERR   = %M2.1        ; error ida               |
            | ERRID = %VB2         ; codigo error ida        |
            +------------------------------------------------+
```

### Network 4 - PABS Vuelta: Posicion 0

```text
   %SM0.0
----| |-----+------------------------------------------------+
            | PABS                                           |
            |                                                |
            | AXIS  = 0                                      |
            | EXEC  = %M2.0        ; DONE del primer PABS    |
            | MINF  = %VW300       ; 300 Hz                  |
            | MAXF  = %VD302       ; 2000 Hz, DWORD          |
            | TIME  = %VW304       ; 300 ms                  |
            | POS   = %VD316       ; 0 pulsos, DINT          |
            |                                                |
            | ENO   = %M3.2        ; solo si Ladder lo expone |
            | DONE  = %M3.0        ; termina vuelta          |
            | ERR   = %M3.1        ; error vuelta            |
            | ERRID = %VB3         ; codigo error vuelta     |
            +------------------------------------------------+
```

### Network 5 - Ciclo Terminado

```text
   %M3.0
----| |--------------------------------------------( %M10.0 )
```

## Salidas Fisicas PTO / Driver

Para `AXIS = 0`, el bloque `PABS` controla internamente las salidas fisicas del PTO. No se deben dibujar como bobinas normales.

| Funcion | Salida Fisica | Nota |
|---|---|---|
| STEP / PUL | `%Q0.0` | Generada internamente por `PABS AXIS=0` |
| DIR | `%Q0.2` | Generada internamente por `PABS AXIS=0` |
| ENABLE | `%Q0.3` | Propuesta para habilitar driver, revisar conflicto |

Cableado sugerido para MD-2545:

```text
PLC Q0.0 -> PUL / STEP del driver
PLC Q0.2 -> DIR del driver
PLC Q0.3 -> ENA / ENABLE del driver
PLC 0V   -> comun de senales del driver
```

`Q0.0` y `Q0.2` no aparecen como bobinas en el ladder cuando se usa `PABS`; el bloque las usa internamente.

## ENO en PABS

En IL, la sintaxis oficial del help no incluye `ENO`:

```text
PABS AXIS, EXEC, MINF, MAXF, TIME, POS, DONE, ERR, ERRID
```

En Ladder el bloque puede exponer `ENO` visualmente. Si aparece, se puede conectar a una marca, por ejemplo:

| Bloque | ENO sugerido |
|---|---|
| PABS ida | `%M2.2` |
| PABS vuelta | `%M3.2` |

Si el bloque Ladder no permite definir `ENO`, no es obligatorio para el programa.

## Por Que Se Usan Dos PABS

Se usan dos `PABS` porque son dos movimientos absolutos con destinos distintos:

| Movimiento | Destino |
|---|---:|
| Ida | `%VD306 = 1000` |
| Vuelta | `%VD316 = 0` |

`PABS` ejecuta un movimiento absoluto por flanco ascendente de `EXEC`. Con dos bloques, la secuencia es simple:

```text
I0.0 -> PABS ida a 1000 -> DONE M2.0 -> PABS vuelta a 0 -> DONE M3.0
```

Se podria hacer con un solo `PABS`, cambiando dinamicamente `POS` y generando dos flancos de `EXEC`, pero requiere logica adicional de estados y pulsos. Para prueba y aprendizaje, dos `PABS` es mas claro.

## Simulacion / Prueba

KincoBuilder puede simular o monitorear logica basica, pero `PABS`, `PHOME`, PTO, `Q0.0`, `Q0.2`, `%SMD212` y registros especiales dependen del hardware real. Probar primero en Online Monitor con PLC conectado, velocidad baja y, si es posible, sin potencia al driver.

## Simulacion

KincoBuilder puede simular/monitorizar logica basica, pero PTO, `PABS`, `PHOME`, salidas fisicas y registros especiales dependen del hardware. Para probar, usar PLC real en Online Monitor, inicialmente sin potencia al driver o con velocidad baja.

## Pendientes / A Confirmar

- Si el modelo seleccionado en KincoBuilder es exactamente MK043E-20DT o aparece como HP043-20DT/KINCO-HP043-20DT en el help.
- Confirmar si `PABS` aparece como bloque LD en la libreria de posicionamiento al seleccionar la CPU correcta.
- Confirmar cableado real de direccion PTO0: el help indica direccion automatica en `Q0.2` para axis 0, mientras el contexto inicial asignaba `Q0.3` a enable driver.
- Confirmar que `Q0.3` no sea usado por direccion del eje si se usa modo direction-output automatico.
- Validar mapeo MODBUS exacto para doble palabra (`VD`) y endianness del maestro.
