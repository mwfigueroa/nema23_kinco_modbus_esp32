# Kinco MK043E-20DT - Programa Final IL y Ladder (2 Motores)

Proyecto: `Final_kinco_project_2motores.kpr`  
PLC: Kinco MK043E-20DT  
Control maestro: ESP32 por MODBUS RTU RS-485 (COM1, Slave ID=1)  
Fuente: `MAIN_MAIN.ilp` (Networks 0-79) + `Documento_Implementacion_Final_Kinco_2_Motores.md`

---

## 1. Hardware

| Funcion | Motor 1 (Axis 0) | Motor 2 (Axis 1) |
|---|---|---|
| STEP/PUL | `%Q0.0` | `%Q0.1` |
| DIR | `%Q0.2` | `%Q0.3` |
| Enable driver | `%Q0.4` | `%Q0.5` |
| Sensor HOME | `%I0.0` | `%I0.1` |
| Posicion actual | `%SMD212` | `%SMD242` |

---

## 2. Mapa MODBUS

### 2.1 Motor 1 (Axis 0)

| MODBUS | Direccion PLC | Tipo | Simbolo | Funcion |
|---|---:|---|---|---|
| `40051` | `%VW100` | WORD | `A0_Ctrl_Word` | Palabra de control |
| `40053-40054` | `%VD104` | DINT | `A0_Param_POS` | Posicion destino PABS |
| `40055-40056` | `%VD108` | DWORD | `A0_Param_MAXF` | Frecuencia maxima PABS |
| `40057` | `%VW112` | WORD | `A0_Param_MINF` | Frecuencia minima PABS |
| `40058` | `%VW114` | WORD | `A0_Param_TIME` | Aceleracion PABS (ms) |
| `40059` | `%VW116` | INT | `A0_Param_MODE` | Modo HOME |
| `40060` | `%VW118` | INT | `A0_Param_DIRC` | Direccion HOME |
| `40061` | `%VW120` | WORD | `A0_Param_MINF_HOME` | Frecuencia minima HOME |
| `40063-40064` | `%VD124` | DWORD | `A0_Param_MAXF_HOME` | Frecuencia maxima HOME |
| `40065` | `%VW128` | WORD | `A0_Param_TIME_HOME` | Aceleracion HOME (ms) |
| `40066` | `%VW130` | WORD | `A0_Status_Word` | Palabra de estado |
| `40067-40068` | `%VD132` | DINT | `A0_Status_PosActual` | Posicion actual (copia %SMD212) |

### 2.2 Motor 2 (Axis 1)

| MODBUS | Direccion PLC | Tipo | Simbolo | Funcion |
|---|---:|---|---|---|
| `40101` | `%VW200` | WORD | `A1_Ctrl_Word` | Palabra de control |
| `40103-40104` | `%VD204` | DINT | `A1_Param_POS` | Posicion destino PABS |
| `40105-40106` | `%VD208` | DWORD | `A1_Param_MAXF` | Frecuencia maxima PABS |
| `40107` | `%VW212` | WORD | `A1_Param_MINF` | Frecuencia minima PABS |
| `40108` | `%VW214` | WORD | `A1_Param_TIME` | Aceleracion PABS (ms) |
| `40109` | `%VW216` | INT | `A1_Param_MODE` | Modo HOME |
| `40110` | `%VW218` | INT | `A1_Param_DIRC` | Direccion HOME |
| `40111` | `%VW220` | WORD | `A1_Param_MINF_HOME` | Frecuencia minima HOME |
| `40113-40114` | `%VD224` | DWORD | `A1_Param_MAXF_HOME` | Frecuencia maxima HOME |
| `40115` | `%VW228` | WORD | `A1_Param_TIME_HOME` | Aceleracion HOME (ms) |
| `40116` | `%VW230` | WORD | `A1_Status_Word` | Palabra de estado |
| `40117-40118` | `%VD232` | DINT | `A1_Status_PosActual` | Posicion actual (copia %SMD242) |

### 2.3 Error IDs

| Motor | Byte | Error ID de |
|---|---|---|
| A0 | `%VB140` | PABS |
| A0 | `%VB141` | PHOME |
| A0 | `%VB142` | PSTOP |
| A1 | `%VB240` | PABS |
| A1 | `%VB241` | PHOME |
| A1 | `%VB242` | PSTOP |

---

## 3. Bits de Control (mismos para ambos ejes)

| Bit | A0 | A1 | Simbolo | Funcion |
|---|---:|---|---|---|
| 0 | `%V100.0` | `%V200.0` | `Ctrl_Enable` | Habilita driver |
| 1 | `%V100.1` | `%V200.1` | `Ctrl_ResetPos` | Reset posicion logica |
| 2 | `%V100.2` | `%V200.2` | `Ctrl_StartPABS` | Inicia PABS |
| 3 | `%V100.3` | `%V200.3` | `Ctrl_StartHome` | Inicia PHOME |
| 4 | `%V100.4` | `%V200.4` | `Ctrl_ResetStatus` | Limpia estados |
| 5 | `%V100.5` | `%V200.5` | `Ctrl_Stop` | PSTOP |

**Valores MODBUS con Enable=1 activo:**

| Accion | A0 (40051) | A1 (40101) |
|---|---|---|
| Enable | `0x0001` | `0x0001` |
| Reset Posicion | `0x0003` | `0x0003` |
| Start PABS | `0x0005` | `0x0005` |
| Start HOME | `0x0009` | `0x0009` |
| Reset Estados | `0x0011` | `0x0011` |
| Stop | `0x0021` | `0x0021` |

---

## 4. Bits de Estado (mismos para ambos ejes)

| Bit | A0 | A1 | Simbolo | Significado |
|---|---:|---|---|---|
| 0 | `%V130.0` | `%V230.0` | `Status_HomeOK` | HomeOK |
| 1 | `%V130.1` | `%V230.1` | `Status_HomeDone` | PHOME DONE |
| 2 | `%V130.2` | `%V230.2` | `Status_HomeErr` | PHOME ERR |
| 3 | `%V130.3` | `%V230.3` | `Status_PabsDone` | PABS DONE |
| 4 | `%V130.4` | `%V230.4` | `Status_PabsErr` | PABS ERR |
| 5 | `%V130.5` | `%V230.5` | `Status_AxisBusy` | Eje ocupado |
| 6 | `%V130.6` | `%V230.6` | `Status_HomingActive` | Homing en curso |
| 7 | `%V130.7` | `%V230.7` | `Status_PabsActive` | PABS en curso |
| 8 | `%V131.0` | `%V231.0` | `Status_HomeSensor` | Sensor HOME |
| 9 | `%V131.1` | `%V231.1` | `Status_SystemReady` | Sistema listo |
| 10 | `%V131.2` | `%V231.2` | `Status_EnableOut` | Enable salida |
| 11 | `%V131.3` | `%V231.3` | `Status_StopDone` | Stop DONE |

---

## 5. Marcas Internas

### 5.1 Axis 0 (Motor 1)

| Direccion | Simbolo | Funcion |
|---|---|---|
| `%M10.0` | `A0_M_HomeOK` | HomeOK interno |
| `%M10.1` | `A0_M_HomingActive` | Homing activo |
| `%M10.2` | `A0_M_PabsActive` | PABS activo |
| `%M10.3` | `A0_M_SystemReady` | Sistema listo PABS |
| `%M10.4` | `A0_M_NoHomeErr` | Sin error PHOME (!%M12.2) |
| `%M10.5` | `A0_M_NoPabsErr` | Sin error PABS (!%M11.2) |
| `%M10.6` | `A0_M_NHOME` | NHOME falso/no usado |
| `%M11.1` | `A0_M_PabsDone` | PABS DONE |
| `%M11.2` | `A0_M_PabsErr` | PABS ERR |
| `%M12.1` | `A0_M_HomeDone` | PHOME DONE |
| `%M12.2` | `A0_M_HomeErr` | PHOME ERR |
| `%M13.0` | `A0_M_StopDone` | PSTOP DONE |

### 5.2 Axis 1 (Motor 2)

| Direccion | Simbolo | Funcion |
|---|---|---|
| `%M20.0` | `A1_M_HomeOK` | HomeOK interno |
| `%M20.1` | `A1_M_HomingActive` | Homing activo |
| `%M20.2` | `A1_M_PabsActive` | PABS activo |
| `%M20.3` | `A1_M_SystemReady` | Sistema listo PABS |
| `%M20.4` | `A1_M_NoHomeErr` | Sin error PHOME (!%M22.2) |
| `%M20.5` | `A1_M_NoPabsErr` | Sin error PABS (!%M21.2) |
| `%M20.6` | `A1_M_NHOME` | NHOME falso/no usado |
| `%M21.1` | `A1_M_PabsDone` | PABS DONE |
| `%M21.2` | `A1_M_PabsErr` | PABS ERR |
| `%M22.1` | `A1_M_HomeDone` | PHOME DONE |
| `%M22.2` | `A1_M_HomeErr` | PHOME ERR |
| `%M23.0` | `A1_M_StopDone` | PSTOP DONE |

---

## 6. Registros Especiales

| Registro | Simbolo | Funcion |
|---|---|---|
| `%SM0.0` | `Sys_AlwaysON` | Siempre ON |
| `%SM0.1` | `Sys_FirstScan` | Primer scan |
| `%SM201.3` | `Sys_DirCtrl_A0` | Control direccion PTO0 (0=auto) |
| `%SM231.3` | `Sys_DirCtrl_A1` | Control direccion PTO1 (0=auto) |
| `%SM201.6` | `Sys_ResetPos_A0` | Reset posicion PTO0 |
| `%SM231.6` | `Sys_ResetPos_A1` | Reset posicion PTO1 |
| `%SM201.7` | `Sys_EmergStop_A0` | Emergency stop PTO0 |
| `%SM231.7` | `Sys_EmergStop_A1` | Emergency stop PTO1 |
| `%SMD212` | `Sys_PosActual_A0` | Posicion actual PTO0 |
| `%SMD242` | `Sys_PosActual_A1` | Posicion actual PTO1 |

---

## 7. Tabla VAR_GLOBAL Completa

```
Symbol                  Address     Data Type   Comment
----------------------  --------    ---------   -------------------------
(* Hardware *)
A0_STEP                 %Q0.0       BOOL        STEP motor 1 (PTO interno)
A1_STEP                 %Q0.1       BOOL        STEP motor 2 (PTO interno)
A0_DIR                  %Q0.2       BOOL        DIR motor 1 (PTO interno)
A1_DIR                  %Q0.3       BOOL        DIR motor 2 (PTO interno)
A0_ENABLE_OUT           %Q0.4       BOOL        Enable driver motor 1
A1_ENABLE_OUT           %Q0.5       BOOL        Enable driver motor 2
A0_HOME_SENSOR          %I0.0       BOOL        Sensor HOME motor 1
A1_HOME_SENSOR          %I0.1       BOOL        Sensor HOME motor 2

(* Control - ESP32 escribe *)
A0_Ctrl_Word            %VW100      WORD        Control motor 1 (40051)
A0_Param_POS            %VD104      DINT        Posicion PABS motor 1 (40053)
A0_Param_MAXF           %VD108      DWORD       Velocidad max PABS motor 1 (40055)
A0_Param_MINF           %VW112      WORD        Velocidad min PABS motor 1 (40057)
A0_Param_TIME           %VW114      WORD        Aceleracion PABS motor 1 (40058)
A0_Param_MODE           %VW116      INT         Modo HOME motor 1 (40059)
A0_Param_DIRC           %VW118      INT         Direccion HOME motor 1 (40060)
A0_Param_MINF_HOME      %VW120      WORD        Velocidad min HOME motor 1 (40061)
A0_Param_MAXF_HOME      %VD124      DWORD       Velocidad max HOME motor 1 (40063)
A0_Param_TIME_HOME      %VW128      WORD        Aceleracion HOME motor 1 (40065)
A1_Ctrl_Word            %VW200      WORD        Control motor 2 (40101)
A1_Param_POS            %VD204      DINT        Posicion PABS motor 2 (40103)
A1_Param_MAXF           %VD208      DWORD       Velocidad max PABS motor 2 (40105)
A1_Param_MINF           %VW212      WORD        Velocidad min PABS motor 2 (40107)
A1_Param_TIME           %VW214      WORD        Aceleracion PABS motor 2 (40108)
A1_Param_MODE           %VW216      INT         Modo HOME motor 2 (40109)
A1_Param_DIRC           %VW218      INT         Direccion HOME motor 2 (40110)
A1_Param_MINF_HOME      %VW220      WORD        Velocidad min HOME motor 2 (40111)
A1_Param_MAXF_HOME      %VD224      DWORD       Velocidad max HOME motor 2 (40113)
A1_Param_TIME_HOME      %VW228      WORD        Aceleracion HOME motor 2 (40115)

(* Estado - ESP32 lee *)
A0_Status_Word          %VW130      WORD        Estado motor 1 (40066)
A0_Status_PosActual     %VD132      DINT        Posicion actual motor 1 (40067)
A0_ErrID_PABS           %VB140      BYTE        Error ID PABS motor 1
A0_ErrID_HOME           %VB141      BYTE        Error ID PHOME motor 1
A0_ErrID_STOP           %VB142      BYTE        Error ID PSTOP motor 1
A1_Status_Word          %VW230      WORD        Estado motor 2 (40116)
A1_Status_PosActual     %VD232      DINT        Posicion actual motor 2 (40117)
A1_ErrID_PABS           %VB240      BYTE        Error ID PABS motor 2
A1_ErrID_HOME           %VB241      BYTE        Error ID PHOME motor 2
A1_ErrID_STOP           %VB242      BYTE        Error ID PSTOP motor 2

(* Marcas internas Axis 0 *)
A0_M_HomeOK             %M10.0      BOOL        HomeOK interno axis 0
A0_M_HomingActive       %M10.1      BOOL        Homing activo axis 0
A0_M_PabsActive         %M10.2      BOOL        PABS activo axis 0
A0_M_SystemReady        %M10.3      BOOL        Sistema listo PABS axis 0
A0_M_NoHomeErr          %M10.4      BOOL        Sin error PHOME axis 0
A0_M_NoPabsErr          %M10.5      BOOL        Sin error PABS axis 0
A0_M_NHOME              %M10.6      BOOL        NHOME falso axis 0
A0_M_PabsDone           %M11.1      BOOL        PABS DONE axis 0
A0_M_PabsErr            %M11.2      BOOL        PABS ERR axis 0
A0_M_HomeDone           %M12.1      BOOL        PHOME DONE axis 0
A0_M_HomeErr            %M12.2      BOOL        PHOME ERR axis 0
A0_M_StopDone           %M13.0      BOOL        PSTOP DONE axis 0

(* Marcas internas Axis 1 *)
A1_M_HomeOK             %M20.0      BOOL        HomeOK interno axis 1
A1_M_HomingActive       %M20.1      BOOL        Homing activo axis 1
A1_M_PabsActive         %M20.2      BOOL        PABS activo axis 1
A1_M_SystemReady        %M20.3      BOOL        Sistema listo PABS axis 1
A1_M_NoHomeErr          %M20.4      BOOL        Sin error PHOME axis 1
A1_M_NoPabsErr          %M20.5      BOOL        Sin error PABS axis 1
A1_M_NHOME              %M20.6      BOOL        NHOME falso axis 1
A1_M_PabsDone           %M21.1      BOOL        PABS DONE axis 1
A1_M_PabsErr            %M21.2      BOOL        PABS ERR axis 1
A1_M_HomeDone           %M22.1      BOOL        PHOME DONE axis 1
A1_M_HomeErr            %M22.2      BOOL        PHOME ERR axis 1
A1_M_StopDone           %M23.0      BOOL        PSTOP DONE axis 1

(* Registros especiales *)
Sys_AlwaysON            %SM0.0      BOOL        Siempre ON
Sys_FirstScan           %SM0.1      BOOL        Primer scan
Sys_DirCtrl_A0          %SM201.3    BOOL        Control direccion PTO0
Sys_DirCtrl_A1          %SM231.3    BOOL        Control direccion PTO1
Sys_ResetPos_A0         %SM201.6    BOOL        Reset posicion PTO0
Sys_ResetPos_A1         %SM231.6    BOOL        Reset posicion PTO1
Sys_EmergStop_A0        %SM201.7    BOOL        Emergency stop PTO0
Sys_EmergStop_A1        %SM231.7    BOOL        Emergency stop PTO1
Sys_PosActual_A0        %SMD212     DINT        Posicion actual PTO0
Sys_PosActual_A1        %SMD242     DINT        Posicion actual PTO1
```

---

## 8. Programa IL Completo (Networks 0-79)

```
(* ================================================================== *)
(* CONFIGURACION INICIAL PTO - Networks 0-1                           *)
(* ================================================================== *)
(* NW0 - Direccion PTO0 automatica *)
LD      %SM0.0
R       %SM201.3

(* NW1 - Direccion PTO1 automatica *)
LD      %SM0.0
R       %SM231.3

(* ================================================================== *)
(* DEFAULTS AXIS 0 (MOTOR 1) - Networks 2-12                           *)
(* ================================================================== *)
(* NW2 - POS PABS default = 0 *)
LD      %SM0.1
MOVE    DI#0, %VD104

(* NW3 - MAXF PABS default = 2000 Hz *)
LD      %SM0.1
MOVE    DW#2000, %VD108

(* NW4 - MINF PABS default = 300 Hz *)
LD      %SM0.1
MOVE    W#300, %VW112

(* NW5 - TIME PABS default = 300 ms *)
LD      %SM0.1
MOVE    W#300, %VW114

(* NW6 - MODE HOME default = 1 (solo HOME) *)
LD      %SM0.1
MOVE    I#1, %VW116

(* NW7 - DIRC HOME default = 0 (forward) *)
LD      %SM0.1
MOVE    I#0, %VW118

(* NW8 - MINF HOME default = 200 Hz *)
LD      %SM0.1
MOVE    W#200, %VW120

(* NW9 - MAXF HOME default = 1000 Hz *)
LD      %SM0.1
MOVE    DW#1000, %VD124

(* NW10 - TIME HOME default = 300 ms *)
LD      %SM0.1
MOVE    W#300, %VW128

(* NW11 - NHOME falso *)
LD      %SM0.1
R       %M10.6

(* NW12 - HomeOK inicial apagado *)
LD      %SM0.1
R       %M10.0

(* ================================================================== *)
(* DEFAULTS AXIS 1 (MOTOR 2) - Networks 13-23                          *)
(* ================================================================== *)
(* NW13 - POS PABS default = 0 *)
LD      %SM0.1
MOVE    DI#0, %VD204

(* NW14 - MAXF PABS default = 2000 Hz *)
LD      %SM0.1
MOVE    DW#2000, %VD208

(* NW15 - MINF PABS default = 300 Hz *)
LD      %SM0.1
MOVE    W#300, %VW212

(* NW16 - TIME PABS default = 300 ms *)
LD      %SM0.1
MOVE    W#300, %VW214

(* NW17 - MODE HOME default = 1 (solo HOME) *)
LD      %SM0.1
MOVE    I#1, %VW216

(* NW18 - DIRC HOME default = 0 (forward) *)
LD      %SM0.1
MOVE    I#0, %VW218

(* NW19 - MINF HOME default = 200 Hz *)
LD      %SM0.1
MOVE    W#200, %VW220

(* NW20 - MAXF HOME default = 1000 Hz *)
LD      %SM0.1
MOVE    DW#1000, %VD224

(* NW21 - TIME HOME default = 300 ms *)
LD      %SM0.1
MOVE    W#300, %VW228

(* NW22 - NHOME falso *)
LD      %SM0.1
R       %M20.6

(* NW23 - HomeOK inicial apagado *)
LD      %SM0.1
R       %M20.0

(* ================================================================== *)
(* ENABLE DRIVERS - Networks 24-25                                     *)
(* ================================================================== *)
(* NW24 - Enable driver motor 1: %V100.0 -> %Q0.4 *)
LD      %V100.0
ST      %Q0.4

(* NW25 - Enable driver motor 2: %V200.0 -> %Q0.5 *)
LD      %V200.0
ST      %Q0.5

(* ================================================================== *)
(* RESET POSICION LOGICA - Networks 26-27                              *)
(* ================================================================== *)
(* NW26 - Reset posicion PTO0 desde MODBUS *)
LD      %V100.1
ST      %SM201.6

(* NW27 - Reset posicion PTO1 desde MODBUS *)
LD      %V200.1
ST      %SM231.6

(* ================================================================== *)
(* PSTOP - Networks 28-29                                              *)
(* ================================================================== *)
(* NW28 - PSTOP motor 1 *)
LD      %SM0.0
PSTOP   0, %V100.5, %M13.0, %VB142

(* NW29 - PSTOP motor 2 *)
LD      %SM0.0
PSTOP   1, %V200.5, %M23.0, %VB242

(* ================================================================== *)
(* RESET STOP ANTES DE MOVER - Networks 30-31                          *)
(* ================================================================== *)
(* NW30 - Liberar stop PTO0 al iniciar HOME o PABS *)
LD      %V100.2
OR      %V100.3
R       %SM201.7

(* NW31 - Liberar stop PTO1 al iniciar HOME o PABS *)
LD      %V200.2
OR      %V200.3
R       %SM231.7

(* ================================================================== *)
(* PHOME - Networks 32-33                                              *)
(* ================================================================== *)
(* NW32 - PHOME motor 1 (AXIS=0, HOME=%I0.0, EXEC=%V100.3) *)
LD      %V100.0
PHOME   0, %V100.3, %I0.0, %M10.6, %VW116, %VW118, %VW120, %VD124, %VW128, %M12.1, %M12.2, %VB141

(* NW33 - PHOME motor 2 (AXIS=1, HOME=%I0.1, EXEC=%V200.3) *)
LD      %V200.0
PHOME   1, %V200.3, %I0.1, %M20.6, %VW216, %VW218, %VW220, %VD224, %VW228, %M22.1, %M22.2, %VB241

(* ================================================================== *)
(* SET/RESET HomeOK - Networks 34-37                                   *)
(* ================================================================== *)
(* NW34 - Set HomeOK motor 1: PHOME DONE sin error *)
LDN     %M12.2
AND     %M12.1
S       %M10.0

(* NW35 - Set HomeOK motor 2: PHOME DONE sin error *)
LDN     %M22.2
AND     %M22.1
S       %M20.0

(* NW36 - Reset HomeOK motor 1: Ctrl_ResetStatus o Ctrl_ResetPos *)
LD      %V100.4
OR      %V100.1
R       %M10.0

(* NW37 - Reset HomeOK motor 2: Ctrl_ResetStatus o Ctrl_ResetPos *)
LD      %V200.4
OR      %V200.1
R       %M20.0

(* ================================================================== *)
(* AUXILIARES SIN ERROR - Networks 38-39                               *)
(* ================================================================== *)
(* NW38 - Sin error PHOME motor 1 *)
LDN     %M12.2
ST      %M10.4

(* NW39 - Sin error PABS motor 1 *)
LDN     %M11.2
ST      %M10.5

(* ================================================================== *)
(* SYSTEM READY - Networks 40-43                                       *)
(* ================================================================== *)
(* NW40 - Sistema listo motor 1: Enable + HomeOK + NoHomeErr + NoPabsErr *)
LD      %V100.0
AND     %M10.0
AND     %M10.4
AND     %M10.5
ST      %M10.3

(* NW41 - Sin error PHOME motor 2 *)
LDN     %M22.2
ST      %M20.4

(* NW42 - Sin error PABS motor 2 *)
LDN     %M21.2
ST      %M20.5

(* NW43 - Sistema listo motor 2: Enable + HomeOK + NoHomeErr + NoPabsErr *)
LD      %V200.0
AND     %M20.0
AND     %M20.4
AND     %M20.5
ST      %M20.3

(* ================================================================== *)
(* PABS - Networks 44-45                                               *)
(* ================================================================== *)
(* NW44 - PABS motor 1 (SystemReady como EN) *)
LD      %M10.3
PABS    0, %V100.2, %VW112, %VD108, %VW114, %VD104, %M11.1, %M11.2, %VB140

(* NW45 - PABS motor 2 (SystemReady como EN) *)
LD      %M20.3
PABS    1, %V200.2, %VW212, %VD208, %VW214, %VD204, %M21.1, %M21.2, %VB240

(* ================================================================== *)
(* MARCAS INTERNAS DE MOVIMIENTO ACTIVO - Networks 46-53               *)
(* ================================================================== *)
(* NW46 - Set HomingActive motor 1 *)
LD      %V100.3
AND     %V100.0
S       %M10.1

(* NW47 - Set HomingActive motor 2 *)
LD      %V200.3
AND     %V200.0
S       %M20.1

(* NW48 - Reset HomingActive motor 1 (HomeDone, HomeErr, Stop, ResetStatus) *)
LD      %M12.1
OR      %M12.2
OR      %V100.5
OR      %V100.4
R       %M10.1

(* NW49 - Reset HomingActive motor 2 *)
LD      %M22.1
OR      %M22.2
OR      %V200.5
OR      %V200.4
R       %M20.1

(* NW50 - Set PabsActive motor 1 *)
LD      %V100.2
AND     %M10.3
S       %M10.2

(* NW51 - Set PabsActive motor 2 *)
LD      %V200.2
AND     %M20.3
S       %M20.2

(* NW52 - Reset PabsActive motor 1 (PabsDone, PabsErr, Stop, ResetStatus) *)
LD      %M11.1
OR      %M11.2
OR      %V100.5
OR      %V100.4
R       %M10.2

(* NW53 - Reset PabsActive motor 2 *)
LD      %M21.1
OR      %M21.2
OR      %V200.5
OR      %V200.4
R       %M20.2

(* ================================================================== *)
(* COPIA POSICION ACTUAL - Networks 54-55                              *)
(* ================================================================== *)
(* NW54 - Posicion actual PTO0 a MODBUS *)
LD      %SM0.0
MOVE    %SMD212, %VD132

(* NW55 - Posicion actual PTO1 a MODBUS *)
LD      %SM0.0
MOVE    %SMD242, %VD232

(* ================================================================== *)
(* ESTADO MODBUS AXIS 0 - Networks 56-67                               *)
(* ================================================================== *)
(* NW56 - HomeOK *)
LD      %M10.0
ST      %V130.0

(* NW57 - HomeDone *)
LD      %M12.1
ST      %V130.1

(* NW58 - HomeErr *)
LD      %M12.2
ST      %V130.2

(* NW59 - PabsDone *)
LD      %M11.1
ST      %V130.3

(* NW60 - PabsErr *)
LD      %M11.2
ST      %V130.4

(* NW61 - AxisBusy (HomingActive o PabsActive) *)
LD      %M10.1
OR      %M10.2
ST      %V130.5

(* NW62 - HomingActive *)
LD      %M10.1
ST      %V130.6

(* NW63 - PabsActive *)
LD      %M10.2
ST      %V130.7

(* NW64 - HomeSensor *)
LD      %I0.0
ST      %V131.0

(* NW65 - SystemReady *)
LD      %M10.3
ST      %V131.1

(* NW66 - EnableOut *)
LD      %Q0.4
ST      %V131.2

(* NW67 - StopDone *)
LD      %M13.0
ST      %V131.3

(* ================================================================== *)
(* ESTADO MODBUS AXIS 1 - Networks 68-79                               *)
(* ================================================================== *)
(* NW68 - HomeOK *)
LD      %M20.0
ST      %V230.0

(* NW69 - HomeDone *)
LD      %M22.1
ST      %V230.1

(* NW70 - HomeErr *)
LD      %M22.2
ST      %V230.2

(* NW71 - PabsDone *)
LD      %M21.1
ST      %V230.3

(* NW72 - PabsErr *)
LD      %M21.2
ST      %V230.4

(* NW73 - AxisBusy *)
LD      %M20.1
OR      %M20.2
ST      %V230.5

(* NW74 - HomingActive *)
LD      %M20.1
ST      %V230.6

(* NW75 - PabsActive *)
LD      %M20.2
ST      %V230.7

(* NW76 - HomeSensor *)
LD      %I0.1
ST      %V231.0

(* NW77 - SystemReady *)
LD      %M20.3
ST      %V231.1

(* NW78 - EnableOut *)
LD      %Q0.5
ST      %V231.2

(* NW79 - StopDone *)
LD      %M23.0
ST      %V231.3

END
```

---

## 9. Diagrama Ladder Completo

### Networks 0-1: Configuracion PTO

```
NW0 - Direccion PTO0 automatica
   %SM0.0
----| |--------------------------------------------( R %SM201.3 )

NW1 - Direccion PTO1 automatica
   %SM0.0
----| |--------------------------------------------( R %SM231.3 )
```

### Networks 2-12: Defaults Axis 0

```
NW2  %SM0.1 ----| |----[ MOVE DI#0     -> %VD104 ]
NW3  %SM0.1 ----| |----[ MOVE DW#2000  -> %VD108 ]
NW4  %SM0.1 ----| |----[ MOVE W#300    -> %VW112 ]
NW5  %SM0.1 ----| |----[ MOVE W#300    -> %VW114 ]
NW6  %SM0.1 ----| |----[ MOVE I#1      -> %VW116 ]
NW7  %SM0.1 ----| |----[ MOVE I#0      -> %VW118 ]
NW8  %SM0.1 ----| |----[ MOVE W#200    -> %VW120 ]
NW9  %SM0.1 ----| |----[ MOVE DW#1000  -> %VD124 ]
NW10 %SM0.1 ----| |----[ MOVE W#300    -> %VW128 ]
NW11 %SM0.1 ----| |--------------------------------( R %M10.6 )
NW12 %SM0.1 ----| |--------------------------------( R %M10.0 )
```

### Networks 13-23: Defaults Axis 1

```
NW13 %SM0.1 ----| |----[ MOVE DI#0     -> %VD204 ]
NW14 %SM0.1 ----| |----[ MOVE DW#2000  -> %VD208 ]
NW15 %SM0.1 ----| |----[ MOVE W#300    -> %VW212 ]
NW16 %SM0.1 ----| |----[ MOVE W#300    -> %VW214 ]
NW17 %SM0.1 ----| |----[ MOVE I#1      -> %VW216 ]
NW18 %SM0.1 ----| |----[ MOVE I#0      -> %VW218 ]
NW19 %SM0.1 ----| |----[ MOVE W#200    -> %VW220 ]
NW20 %SM0.1 ----| |----[ MOVE DW#1000  -> %VD224 ]
NW21 %SM0.1 ----| |----[ MOVE W#300    -> %VW228 ]
NW22 %SM0.1 ----| |--------------------------------( R %M20.6 )
NW23 %SM0.1 ----| |--------------------------------( R %M20.0 )
```

### Networks 24-25: Enable Drivers

```
NW24
   %V100.0
----| |--------------------------------------------( %Q0.4 )

NW25
   %V200.0
----| |--------------------------------------------( %Q0.5 )
```

### Networks 26-27: Reset Posicion

```
NW26
   %V100.1
----| |--------------------------------------------( %SM201.6 )

NW27
   %V200.1
----| |--------------------------------------------( %SM231.6 )
```

### Networks 28-29: PSTOP

```
NW28
   %SM0.0
----| |----+---------------------------------------+
           | PSTOP                                  |
           | AXIS  = 0                              |
           | EXEC  = %V100.5                        |
           | DONE  = %M13.0                         |
           | ERRID = %VB142                         |
           +---------------------------------------+

NW29
   %SM0.0
----| |----+---------------------------------------+
           | PSTOP                                  |
           | AXIS  = 1                              |
           | EXEC  = %V200.5                        |
           | DONE  = %M23.0                         |
           | ERRID = %VB242                         |
           +---------------------------------------+
```

### Networks 30-31: Liberar Stop

```
NW30
   %V100.2
----| |----+
           |
   %V100.3 |
----| |----+---------------------------------------( R %SM201.7 )

NW31
   %V200.2
----| |----+
           |
   %V200.3 |
----| |----+---------------------------------------( R %SM231.7 )
```

### Networks 32-33: PHOME

```
NW32
   %V100.0
----| |----+---------------------------------------+
           | PHOME                                  |
           | AXIS  = 0                              |
           | EXEC  = %V100.3                        |
           | HOME  = %I0.0                          |
           | NHOME = %M10.6                         |
           | MODE  = %VW116                         |
           | DIRC  = %VW118                         |
           | MINF  = %VW120                         |
           | MAXF  = %VD124                         |
           | TIME  = %VW128                         |
           | DONE  = %M12.1                         |
           | ERR   = %M12.2                         |
           | ERRID = %VB141                         |
           +---------------------------------------+

NW33
   %V200.0
----| |----+---------------------------------------+
           | PHOME                                  |
           | AXIS  = 1                              |
           | EXEC  = %V200.3                        |
           | HOME  = %I0.1                          |
           | NHOME = %M20.6                         |
           | MODE  = %VW216                         |
           | DIRC  = %VW218                         |
           | MINF  = %VW220                         |
           | MAXF  = %VD224                         |
           | TIME  = %VW228                         |
           | DONE  = %M22.1                         |
           | ERR   = %M22.2                         |
           | ERRID = %VB241                         |
           +---------------------------------------+
```

### Networks 34-37: Set/Reset HomeOK

```
NW34
   %M12.2      %M12.1
----|/|---------| |--------------------------------( S %M10.0 )

NW35
   %M22.2      %M22.1
----|/|---------| |--------------------------------( S %M20.0 )

NW36
   %V100.4
----| |----+
           |
   %V100.1 |
----| |----+---------------------------------------( R %M10.0 )

NW37
   %V200.4
----| |----+
           |
   %V200.1 |
----| |----+---------------------------------------( R %M20.0 )
```

### Networks 38-43: Auxiliares y SystemReady

```
NW38
   %M12.2
----|/|--------------------------------------------( %M10.4 )

NW39
   %M11.2
----|/|--------------------------------------------( %M10.5 )

NW40
   %V100.0    %M10.0    %M10.4    %M10.5
----| |--------| |-------| |-------| |-------------( %M10.3 )

NW41
   %M22.2
----|/|--------------------------------------------( %M20.4 )

NW42
   %M21.2
----|/|--------------------------------------------( %M20.5 )

NW43
   %V200.0    %M20.0    %M20.4    %M20.5
----| |--------| |-------| |-------| |-------------( %M20.3 )
```

### Networks 44-45: PABS

```
NW44
   %M10.3
----| |----+---------------------------------------+
           | PABS                                   |
           | AXIS  = 0                              |
           | EXEC  = %V100.2                        |
           | MINF  = %VW112                         |
           | MAXF  = %VD108                         |
           | TIME  = %VW114                         |
           | POS   = %VD104                         |
           | DONE  = %M11.1                         |
           | ERR   = %M11.2                         |
           | ERRID = %VB140                         |
           +---------------------------------------+

NW45
   %M20.3
----| |----+---------------------------------------+
           | PABS                                   |
           | AXIS  = 1                              |
           | EXEC  = %V200.2                        |
           | MINF  = %VW212                         |
           | MAXF  = %VD208                         |
           | TIME  = %VW214                         |
           | POS   = %VD204                         |
           | DONE  = %M21.1                         |
           | ERR   = %M21.2                         |
           | ERRID = %VB240                         |
           +---------------------------------------+
```

### Networks 46-53: Marcas de Movimiento Activo

```
NW46
   %V100.3     %V100.0
----| |---------| |--------------------------------( S %M10.1 )

NW47
   %V200.3     %V200.0
----| |---------| |--------------------------------( S %M20.1 )

NW48
   %M12.1
----| |----+
           |
   %M12.2  |
----| |----+
           |
   %V100.5 |
----| |----+
           |
   %V100.4 |
----| |----+---------------------------------------( R %M10.1 )

NW49
   %M22.1
----| |----+
           |
   %M22.2  |
----| |----+
           |
   %V200.5 |
----| |----+
           |
   %V200.4 |
----| |----+---------------------------------------( R %M20.1 )

NW50
   %V100.2     %M10.3
----| |---------| |--------------------------------( S %M10.2 )

NW51
   %V200.2     %M20.3
----| |---------| |--------------------------------( S %M20.2 )

NW52
   %M11.1
----| |----+
           |
   %M11.2  |
----| |----+
           |
   %V100.5 |
----| |----+
           |
   %V100.4 |
----| |----+---------------------------------------( R %M10.2 )

NW53
   %M21.1
----| |----+
           |
   %M21.2  |
----| |----+
           |
   %V200.5 |
----| |----+
           |
   %V200.4 |
----| |----+---------------------------------------( R %M20.2 )
```

### Networks 54-55: Posicion Actual

```
NW54
   %SM0.0
----| |------------[ MOVE %SMD212 -> %VD132 ]

NW55
   %SM0.0
----| |------------[ MOVE %SMD242 -> %VD232 ]
```

### Networks 56-67: Estado MODBUS Axis 0

```
NW56  %M10.0  ----| |-----------------------------( %V130.0 )   HomeOK
NW57  %M12.1  ----| |-----------------------------( %V130.1 )   HomeDone
NW58  %M12.2  ----| |-----------------------------( %V130.2 )   HomeErr
NW59  %M11.1  ----| |-----------------------------( %V130.3 )   PabsDone
NW60  %M11.2  ----| |-----------------------------( %V130.4 )   PabsErr
NW61  %M10.1  ----| |--+
           %M10.2  ----| |--+--------------------( %V130.5 )   AxisBusy
NW62  %M10.1  ----| |-----------------------------( %V130.6 )   HomingActive
NW63  %M10.2  ----| |-----------------------------( %V130.7 )   PabsActive
NW64  %I0.0   ----| |-----------------------------( %V131.0 )   HomeSensor
NW65  %M10.3  ----| |-----------------------------( %V131.1 )   SystemReady
NW66  %Q0.4   ----| |-----------------------------( %V131.2 )   EnableOut
NW67  %M13.0  ----| |-----------------------------( %V131.3 )   StopDone
```

### Networks 68-79: Estado MODBUS Axis 1

```
NW68  %M20.0  ----| |-----------------------------( %V230.0 )   HomeOK
NW69  %M22.1  ----| |-----------------------------( %V230.1 )   HomeDone
NW70  %M22.2  ----| |-----------------------------( %V230.2 )   HomeErr
NW71  %M21.1  ----| |-----------------------------( %V230.3 )   PabsDone
NW72  %M21.2  ----| |-----------------------------( %V230.4 )   PabsErr
NW73  %M20.1  ----| |--+
           %M20.2  ----| |--+--------------------( %V230.5 )   AxisBusy
NW74  %M20.1  ----| |-----------------------------( %V230.6 )   HomingActive
NW75  %M20.2  ----| |-----------------------------( %V230.7 )   PabsActive
NW76  %I0.1   ----| |-----------------------------( %V231.0 )   HomeSensor
NW77  %M20.3  ----| |-----------------------------( %V231.1 )   SystemReady
NW78  %Q0.5   ----| |-----------------------------( %V231.2 )   EnableOut
NW79  %M23.0  ----| |-----------------------------( %V231.3 )   StopDone
```

---

## 10. Secuencias MODBUS desde ESP32

### HOME Motor 1

```
1. 40051 = 0x0001                          ; Enable
2. 40059 = 1                               ; MODE (1=solo HOME)
3. 40060 = 0                               ; DIRC (0=forward)
4. 40061 = 200                             ; MINF HOME
5. 40063-40064 = 1000                      ; MAXF HOME
6. 40065 = 300                             ; TIME HOME
7. 40051 = 0x0009                          ; Enable + StartHome
8. delay(100ms)
9. 40051 = 0x0001                          ; Bajar StartHome
10. Leer 40066 hasta bit0=1 (OK) o bit2=1 (ERR)
```

### HOME Motor 2

```
1. 40101 = 0x0001
2. 40109 = 1
3. 40110 = 0
4. 40111 = 200
5. 40113-40114 = 1000
6. 40115 = 300
7. 40101 = 0x0009
8. delay(100ms)
9. 40101 = 0x0001
10. Leer 40116 hasta bit0=1 o bit2=1
```

### PABS Motor 1

```
1. 40053-40054 = POS (DINT, pulsos destino)
2. 40055-40056 = MAXF (DWORD, Hz)
3. 40057 = MINF (WORD, Hz)
4. 40058 = TIME (WORD, ms)
5. 40051 = 0x0005                          ; Enable + StartPABS
6. delay(100ms)
7. 40051 = 0x0001                          ; Bajar StartPABS
8. Leer 40066 hasta bit3=1 (DONE) o bit4=1 (ERR)
```

### PABS Motor 2

```
1. 40103-40104 = POS
2. 40105-40106 = MAXF
3. 40107 = MINF
4. 40108 = TIME
5. 40101 = 0x0005
6. delay(100ms)
7. 40101 = 0x0001
8. Leer 40116 hasta bit3=1 o bit4=1
```

### STOP

```
Motor 1: 40051 = 0x0021   (Enable + Stop)
         40051 = 0x0001   (Volver a Enable)
Motor 2: 40101 = 0x0021
         40101 = 0x0001
```

### Leer Posicion Actual

```
Motor 1: Leer 40067-40068 = %VD132 (DINT, copia de %SMD212)
Motor 2: Leer 40117-40118 = %VD232 (DINT, copia de %SMD242)
```

---

## 11. Notas Importantes

1. `%SM201.3` y `%SM231.3` en 0 -> direccion automatica via `%Q0.2`/`%Q0.3`
2. `PHOME`/`PABS` ejecutan por **flanco ascendente** de EXEC. El ESP32 debe subir el bit y volverlo a bajar.
3. `%Q0.3` se usa como DIR del motor 2 (no como enable). Los enables estan en `%Q0.4` y `%Q0.5`.
4. `MAXF`, `MINF`, `TIME`, `POS` de PABS son **todos variables** -> cumplen la regla de Kinco.
5. Endianness de registros de 32 bits: verificar palabra alta/baja en la libreria MODBUS del ESP32.
6. `%M10.6`/`%M20.6` (NHOME) siempre en 0 porque se usa MODE=1 (solo HOME, sin near home).
