# Programa MODBUS Test - PABS (posicionamiento absoluto) con Kinco MK043E-20DT

## A usar cuando necesites "cantidad de pasos" exacta (PJOG no controla pasos)

## Mapa MODBUS

| Registro MODBUS | Direccion PLC | Tipo   | Descripcion                       |
|----------------:|--------------|--------|-----------------------------------|
| `40051-40052`   | `%VD100`     | DINT   | POS - destino en pulsos           |
| `40053-40054`   | `%VD104`     | DWORD  | MAXF - velocidad max (Hz)         |
| `40055`         | `%VW108`     | WORD   | MINF - velocidad inicial (Hz)     |
| `40056`         | `%VW110`     | WORD   | TIME - aceleracion (ms)           |
| `40070`         | `%VW138`     | WORD   | Palabra de control                |
| `40101-40102`   | `%VD200`     | DINT   | Posicion actual (%SMD212)         |
| `40152`         | `%VW302`     | WORD   | Palabra de estado                 |

## Bits de Control (40070 = %VW138)

| Bit | Nombre         | Funcion                             |
|-----|----------------|-------------------------------------|
| 0   | Ctrl_Enable    | Habilita driver (%Q0.3) + EN PABS  |
| 1   | Ctrl_ResetPos  | Reset posicion logica PTO0          |
| 2   | Ctrl_StartPABS | Arranca PABS por flanco ascendente  |
| 5   | Ctrl_Stop      | PSTOP                               |

## Bits de Estado (40152 = %VW302)

| Bit | Nombre       | Significado           |
|-----|-------------|-----------------------|
| 3   | PabsDone    | PABS terminado OK     |
| 4   | PabsErr     | Error en PABS         |
| 5   | PTO0_Status | %SM66.7               |
| 7   | PabsActive  | PABS en curso         |

## Programa IL

```
(* ================================================================ *)
(* Network 0 - Defaults en primer scan                               *)
(* ================================================================ *)
LD      %SM0.1
MOVE    DI#0, %VD100
MOVE    DW#2000, %VD104
MOVE    W#300, %VW108
MOVE    W#300, %VW110

(* ================================================================ *)
(* Network 1 - Enable driver                                         *)
(* ================================================================ *)
LD      %V138.0
ST      %Q0.3

(* ================================================================ *)
(* Network 2 - Reset posicion logica PTO0                            *)
(* ================================================================ *)
LD      %V138.1
ST      %SM201.6

(* ================================================================ *)
(* Network 3 - Reset emergency stop al arrancar PABS                 *)
(* ================================================================ *)
LD      %V138.2
R       %SM201.7

(* ================================================================ *)
(* Network 4 - PSTOP                                                 *)
(* ================================================================ *)
LD      %SM0.0
PSTOP   0, %V138.5, %M5.0, %VB5

(* ================================================================ *)
(* Network 5 - PABS: MINF/MAXF/TIME/POS todos variables              *)
(* ================================================================ *)
LD      %V138.0
PABS    0, %V138.2, %VW108, %VD104, %VW110, %VD100, %M2.0, %M2.1, %VB304

(* ================================================================ *)
(* Network 6 - Set PABS activo                                       *)
(* ================================================================ *)
LD      %V138.2
AND     %V138.0
S       %M1.2

(* ================================================================ *)
(* Network 7 - Reset PABS activo                                     *)
(* ================================================================ *)
LD      %M2.0
OR      %M2.1
R       %M1.2

(* ================================================================ *)
(* Network 8 - Copiar posicion actual a MODBUS                       *)
(* ================================================================ *)
LD      %SM0.0
MOVE    %SMD212, %VD200

(* ================================================================ *)
(* Network 9 - Estados hacia MODBUS                                  *)
(* ================================================================ *)
LD      %SM66.7
ST      %V302.5
LD      %M2.0
ST      %V302.3
LD      %M2.1
ST      %V302.4
LD      %M1.2
ST      %V302.7
```

## Diagrama Ladder

```
Network 0 - Defaults
   %SM0.1
----| |------------[ MOVE DI#0     -> %VD100 ]
                   [ MOVE DW#2000  -> %VD104 ]
                   [ MOVE W#300    -> %VW108 ]
                   [ MOVE W#300    -> %VW110 ]

Network 1 - Enable Driver
   %V138.0
----| |--------------------------------------------( %Q0.3 )

Network 2 - Reset Posicion
   %V138.1
----| |--------------------------------------------( %SM201.6 )

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

Network 5 - PABS
   %V138.0
----| |-----+--------------------------------------+
            | PABS                                  |
            | AXIS  = 0                             |
            | EXEC  = %V138.2                       |
            | MINF  = %VW108                        |
            | MAXF  = %VD104                        |
            | TIME  = %VW110                        |
            | POS   = %VD100                        |
            | DONE  = %M2.0                         |
            | ERR   = %M2.1                         |
            | ERRID = %VB304                        |
            +--------------------------------------+

Network 6 - Set PABS Activo
   %V138.2      %V138.0
----| |----------| |--------------------------------( S %M1.2 )

Network 7 - Reset PABS Activo
   %M2.0
----| |----+
           |
   %M2.1   |
----| |----+---------------------------------------( R %M1.2 )

Network 8 - Posicion Actual -> MODBUS
   %SM0.0
----| |------------[ MOVE %SMD212 -> %VD200 ]

Network 9 - PTO0 Status -> MODBUS
   %SM66.7
----| |--------------------------------------------( %V302.5 )

Network 10 - PABS DONE -> MODBUS
   %M2.0
----| |--------------------------------------------( %V302.3 )

Network 11 - PABS ERR -> MODBUS
   %M2.1
----| |--------------------------------------------( %V302.4 )

Network 12 - PABS Activo -> MODBUS
   %M1.2
----| |--------------------------------------------( %V302.7 )
```

## Secuencia ESP32 para PABS (con pasos)

```
1. Escribir destino:    40051-40052 = POS (DINT, pulsos, ej: DI#1000)
2. Escribir velocidad:  40053-40054 = MAXF (DWORD, Hz)
3. Escribir vel min:    40055       = MINF (WORD, Hz, min 125)
4. Escribir aceleracion:40056       = TIME (WORD, ms)
5. Enable driver:       40070 = 0x0001
6. Start PABS:          40070 = 0x0005   (Enable + StartPABS)
7. Bajar start:         40070 = 0x0001   (deja Enable, baja StartPABS)
8. Esperar:             leer 40152 hasta bit 3 = 1 (DONE) o bit 4 = 1 (ERR)
9. Leer posicion final: 40101-40102
```

## Resumen: PJOG vs PABS

| Caracteristica     | PJOG                       | PABS                        |
|--------------------|---------------------------|-----------------------------|
| Control de pasos   | NO - pulsos continuos     | SI - destino absoluto       |
| Velocidad          | MAXF (fija durante jog)  | MINF + MAXF + aceleracion   |
| Direccion          | DIRC (parametro)          | Automatica segun POS vs actual |
| Se detiene solo    | Al quitar EN o PSTOP      | Al llegar a POS             |
| DONE               | No muy util (jog infinito)| Si, cuando llega a destino  |
