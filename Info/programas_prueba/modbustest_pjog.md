# Programa MODBUS Test - PJOG (Jog) con Kinco MK043E-20DT

## Mapa MODBUS

| Registro MODBUS | Direccion PLC | Tipo   | Descripcion                    |
|----------------:|--------------|--------|--------------------------------|
| `40053-40054`   | `%VD104`     | DWORD  | MAXF - velocidad jog (Hz)      |
| `40057`         | `%VW112`     | INT    | DIRC - direccion (0=fwd, 1=rev)|
| `40070`         | `%VW138`     | WORD   | Palabra de control             |
| `40101-40102`   | `%VD200`     | DINT   | Posicion actual (%SMD212)      |
| `40152`         | `%VW302`     | WORD   | Palabra de estado              |

## Bits de Control (40070 = %VW138)

| Bit | Nombre         | Funcion                                      |
|-----|----------------|----------------------------------------------|
| 0   | Ctrl_Enable    | Habilita driver (%Q0.3) + EN del PJOG       |
| 2   | Ctrl_StartJog  | Arranca PJOG por flanco ascendente           |
| 5   | Ctrl_Stop      | PSTOP (parada de emergencia)                 |

## Bits de Estado (40152 = %VW302)

| Bit | Nombre       | Significado            |
|-----|-------------|------------------------|
| 3   | PjogDone    | PJOG DONE (%M2.0)     |
| 4   | PjogErr     | PJOG ERR  (%M2.1)     |
| 5   | PTO0_Status | %SM66.7               |
| 7   | PjogActive  | PJOG en curso (%M1.0) |

## Tabla de Variables Globales (VAR_GLOBAL)

```
Symbol              Address     Data Type   Comment
----------------    --------    ---------   ------------------------------
Param_MAXF_PJOG     %VD104      DWORD       Velocidad jog desde ESP32
Param_DIRC_PJOG     %VW112      INT         Direccion jog desde ESP32
Ctrl_Word           %VW138      WORD        Control desde ESP32 (40070)
Status_Word         %VW302      WORD        Estado hacia ESP32 (40152)
Status_PosActual    %VD200      DINT        Posicion actual hacia ESP32
M_PjogDone          %M2.0       BOOL        PJOG DONE
M_PjogErr           %M2.1       BOOL        PJOG ERR
M_PjogActive        %M1.0       BOOL        PJOG activo
Hw_DrvEnable        %Q0.3       BOOL        Enable driver
Sys_AlwaysON        %SM0.0      BOOL        Siempre ON
Sys_FirstScan       %SM0.1      BOOL        Primer scan
Sys_EmergStop       %SM201.7    BOOL        Stop PTO0
Sys_PTO0_Status     %SM66.7     BOOL        Estado PTO0
Sys_PosActual       %SMD212     DINT        Posicion actual PTO0
```

## Programa IL

```
(* ================================================================ *)
(* Network 0 - Default MAXF = 1000 Hz en primer scan                 *)
(* ================================================================ *)
LD      %SM0.1
MOVE    DW#1000, %VD104

(* ================================================================ *)
(* Network 1 - Default DIRC = 0 (forward) en primer scan             *)
(* ================================================================ *)
LD      %SM0.1
MOVE    I#0, %VW112

(* ================================================================ *)
(* Network 2 - Enable driver desde MODBUS                            *)
(* ================================================================ *)
LD      %V138.0
ST      %Q0.3

(* ================================================================ *)
(* Network 3 - Reset emergency stop si se activa StartJog            *)
(* ================================================================ *)
LD      %V138.2
R       %SM201.7

(* ================================================================ *)
(* Network 4 - PSTOP desde MODBUS                                    *)
(* ================================================================ *)
LD      %SM0.0
PSTOP   0, %V138.5, %M5.0, %VB5

(* ================================================================ *)
(* Network 5 - PJOG: EN=%V138.0, EXEC=%V138.2, speed/dir variables  *)
(* MAXF y DIRC son ambos variables -> cumple regla del help         *)
(* ================================================================ *)
LD      %V138.0
PJOG    0, %V138.2, %VD104, %VW112, %M2.0, %M2.1, %VB304

(* ================================================================ *)
(* Network 6 - Set PJOG activo                                       *)
(* ================================================================ *)
LD      %V138.2
AND     %V138.0
S       %M1.0

(* ================================================================ *)
(* Network 7 - Reset PJOG activo cuando DONE o ERR                   *)
(* ================================================================ *)
LD      %M2.0
OR      %M2.1
R       %M1.0

(* ================================================================ *)
(* Network 8 - Copiar posicion actual a MODBUS                       *)
(* ================================================================ *)
LD      %SM0.0
MOVE    %SMD212, %VD200

(* ================================================================ *)
(* Network 9 - Estado PTO0 hacia MODBUS                              *)
(* ================================================================ *)
LD      %SM66.7
ST      %V302.5

(* ================================================================ *)
(* Network 10 - Estado PJOG DONE hacia MODBUS                         *)
(* ================================================================ *)
LD      %M2.0
ST      %V302.3

(* ================================================================ *)
(* Network 11 - Estado PJOG ERR hacia MODBUS                          *)
(* ================================================================ *)
LD      %M2.1
ST      %V302.4

(* ================================================================ *)
(* Network 12 - Estado PJOG activo hacia MODBUS                       *)
(* ================================================================ *)
LD      %M1.0
ST      %V302.7
```

## Diagrama Ladder

```
Network 0 - Default MAXF = 1000 Hz
   %SM0.1
----| |------------[ MOVE DW#1000 -> %VD104 ]

Network 1 - Default DIRC = 0
   %SM0.1
----| |------------[ MOVE I#0 -> %VW112 ]

Network 2 - Enable Driver
   %V138.0
----| |--------------------------------------------( %Q0.3 )

Network 3 - Reset Emergency Stop
   %V138.2
----| |--------------------------------------------( R %SM201.7 )

Network 4 - PSTOP
   %SM0.0
----| |-----+--------------------------------------+
            | PSTOP                                 |
            | AXIS  = 0                             |
            | EXEC  = %V138.5                       |
            | DONE  = %M5.0                         |
            | ERRID = %VB5                          |
            +--------------------------------------+

Network 5 - PJOG
   %V138.0
----| |-----+--------------------------------------+
            | PJOG                                  |
            | AXIS  = 0                             |
            | EXEC  = %V138.2                       |
            | MAXF  = %VD104                        |
            | DIRC  = %VW112                        |
            | DONE  = %M2.0                         |
            | ERR   = %M2.1                         |
            | ERRID = %VB304                        |
            +--------------------------------------+

Network 6 - Set PJOG Activo
   %V138.2      %V138.0
----| |----------| |--------------------------------( S %M1.0 )

Network 7 - Reset PJOG Activo
   %M2.0
----| |----+
           |
   %M2.1   |
----| |----+---------------------------------------( R %M1.0 )

Network 8 - Posicion Actual -> MODBUS
   %SM0.0
----| |------------[ MOVE %SMD212 -> %VD200 ]

Network 9 - PTO0 Status -> MODBUS
   %SM66.7
----| |--------------------------------------------( %V302.5 )

Network 10 - PJOG DONE -> MODBUS
   %M2.0
----| |--------------------------------------------( %V302.3 )

Network 11 - PJOG ERR -> MODBUS
   %M2.1
----| |--------------------------------------------( %V302.4 )

Network 12 - PJOG Activo -> MODBUS
   %M1.0
----| |--------------------------------------------( %V302.7 )
```

## Secuencia ESP32 para PJOG

```
1. Escribir velocidad:  40053-40054 = MAXF (DWORD, Hz, min 125)
2. Escribir direccion:  40057 = 0 (forward) o 1 (backward)
3. Enable driver:       40070 = 0x0001   (bit 0)
4. Start jog:           40070 = 0x0005   (bit 0 + bit 2)
5. Bajar start:         40070 = 0x0001   (deja Enable, baja StartJog)
6. Leer estado:         40152
   - bit 7 = 1 -> PJOG activo
   - bit 3 = 1 -> terminado (PJOG no termina solo, solo por stop)
7. Leer posicion:       40101-40102 = %VD200
8. Para detener:        40070 = 0x0021   (bit 0 + bit 5 = PSTOP)
   o simplemente:       40070 = 0x0000   (quita Enable -> PJOG se detiene)
```

## Nota sobre "cantidad de pasos"

PJOG **no controla pasos**, genera pulsos continuamente mientras EXEC esta activo.
Para mover una cantidad exacta de pasos usar **PABS** (posicion absoluta) o
**PREL** (posicion relativa). Ver archivo `modbustest_pabs.md`.

## Diferencias con tu codigo original

| Original                     | Corregido                   | Motivo                                    |
|------------------------------|-----------------------------|-------------------------------------------|
| `ST ENA`                     | `ST %Q0.3`                  | ENA no es una direccion valida            |
| `ST DIR`                     | DIRC dentro de PJOG         | PJOG maneja DIR internamente              |
| `ST STP` en network PJOG     | Network separada PSTOP      | Una salida por network                    |
| `%MB1`                       | `%VB304`                    | ERRID debe ser BYTE (%VB)                 |
| `DW#1000` fijo en PJOG       | `%VD104` variable MODBUS    | Velocidad ajustable desde ESP32           |
| DIR no configurable          | `%VW112` variable MODBUS    | Direccion ajustable desde ESP32           |
