# Kinco MK043E-20DT - Ladder 1 motor MODBUS 40070

Este programa esta alineado con la interfaz web simplificada del ESP32.

## Mapa MODBUS usado por el ESP32

| MODBUS | Kinco | Tipo | Uso |
|---:|---|---|---|
| 40051-40052 | `%VD100` | DINT | Destino PABS absoluto |
| 40053-40054 | `%VD104` | DWORD | MAXF PABS |
| 40055 | `%VW108` | WORD | MINF PABS |
| 40056 | `%VW110` | WORD | TIME PABS |
| 40057 | `%VW112` | INT | MODE HOME, usar `1` |
| 40058 | `%VW114` | INT | DIRC HOME, `0` forward, `1` backward |
| 40059 | `%VW116` | WORD | MINF HOME |
| 40060-40061 | `%VD118` | DWORD | MAXF HOME |
| 40062 | `%VW122` | WORD | TIME HOME |
| 40070 | `%VW138` | WORD | Palabra de control |
| 40101-40102 | `%VD200` | DINT | Posicion actual copiada de `%SMD212` |
| 40152 | `%VW302` | WORD | Bits de estado |
| 40153 | `%VW304` | WORD | ErrID PABS/STOP |
| 40154 | `%VW306` | WORD | ErrID HOME |

## Palabra de control `%VW138` / 40070

| Bit | Direccion | Valor con enable | Funcion |
|---:|---|---:|---|
| 0 | `%V138.0` | `0x0001` | Enable driver |
| 1 | `%V138.1` | `0x0003` | Reset posicion PTO0 |
| 2 | `%V138.2` | `0x0005` | Start PABS |
| 3 | `%V138.3` | `0x0009` | Start HOME |
| 4 | `%V138.4` | `0x0011` | Reset estados |
| 5 | `%V138.5` | `0x0021` | PSTOP |

El ESP32 escribe el comando, espera alrededor de 100 ms y vuelve a `0x0001` para bajar el flanco de arranque.

## Registro de estado `%VW302` / 40152

| Bit | Direccion | Nombre | Significado |
|---:|---|---|---|
| 0 | `%V302.0` | HomeOK | HOME valido guardado |
| 1 | `%V302.1` | HomeDone | Salida DONE de `PHOME` |
| 2 | `%V302.2` | HomeErr | Salida ERR de `PHOME` |
| 3 | `%V302.3` | PabsDone | Salida DONE de `PABS` |
| 4 | `%V302.4` | PabsErr | Salida ERR de `PABS` |
| 5 | `%V302.5` | PTO0 | Copia de `%SM66.7` |
| 6 | `%V302.6` | HomingActive | HOME en curso |
| 7 | `%V302.7` | PabsActive | PABS en curso |
| 8 | `%V303.0` | HomeSensor | Copia de `%I0.0` |
| 9 | `%V303.1` | SystemReady | Enable + HomeOK + sin errores |
| 10 | `%V303.2` | EnableOut | Copia de `%Q0.3` |
| 11 | `%V303.3` | StopDone | Salida DONE de `PSTOP` |
| 12 | `%V303.4` | CtrlEnable | Espejo de `%V138.0` |
| 13 | `%V303.5` | CtrlStop | Espejo de `%V138.5` |

La UI actual muestra con nombre los bits 0 a 9 y tambien muestra el valor hexadecimal completo de 40152.

## Ladder por networks

### Networks 0 a 16 - Inicializacion

```text
N0   %SM0.0  -> R %SM201.3
N1   %SM0.1  -> MOVE DI#0    -> %VD100
N2   %SM0.1  -> MOVE DW#2000 -> %VD104
N3   %SM0.1  -> MOVE W#300   -> %VW108
N4   %SM0.1  -> MOVE W#300   -> %VW110
N5   %SM0.1  -> MOVE I#1     -> %VW112
N6   %SM0.1  -> MOVE I#0     -> %VW114
N7   %SM0.1  -> MOVE W#200   -> %VW116
N8   %SM0.1  -> MOVE DW#1000 -> %VD118
N9   %SM0.1  -> MOVE W#300   -> %VW122
N10  %SM0.1  -> MOVE W#0     -> %VW304
N11  %SM0.1  -> MOVE W#0     -> %VW306
N12  %SM0.1  -> R %M10.0
N13  %SM0.1  -> R %M1.0
N14  %SM0.1  -> R %M1.1
N15  %SM0.1  -> R %M1.2
N16  %SM0.1  -> R %SM201.7
```

### Network 17 - Enable driver

```text
%V138.0 ----------------------------------------( %Q0.3 )
```

Si el enable real esta cableado a otra salida, cambiar `%Q0.3` por la salida fisica correcta.

### Network 18 - Reset posicion

```text
%V138.1 ----------------------------------------( %SM201.6 )
```

### Network 19 - Stop

```text
%SM0.0 ----[ PSTOP AXIS=0 EXEC=%V138.5 DONE=%M5.0 ERRID=%VB305 ]
```

### Network 20 - Liberar stop antes de mover

```text
%V138.2 ----+
            +------------------------------------( R %SM201.7 )
%V138.3 ----+
```

### Network 21 - HOME

```text
%V138.0 ----[ PHOME AXIS=0
              EXEC=%V138.3
              HOME=%I0.0
              NHOME=%M10.0
              MODE=%VW112
              DIRC=%VW114
              MINF=%VW116
              MAXF=%VD118
              TIME=%VW122
              DONE=%M3.1
              ERR=%M3.2
              ERRID=%VB306 ]
```

### Networks 22 a 26 - Estados internos

```text
N22  NOT %M3.2 AND %M3.1 ----------------------( S %M1.0 )
N23  %V138.4 OR %V138.1 -----------------------( R %M1.0 )
N24  NOT %M3.2 --------------------------------( %M1.4 )
N25  NOT %M2.2 --------------------------------( %M1.5 )
N26  %V138.0 AND %M1.0 AND %M1.4 AND %M1.5 ----( %M1.3 )
```

`%M1.3` es `SystemReady`. Sin ese bit no se ejecuta `PABS`.

### Network 27 - Movimiento PABS

```text
%M1.3 ----[ PABS AXIS=0
            EXEC=%V138.2
            MINF=%VW108
            MAXF=%VD104
            TIME=%VW110
            POS=%VD100
            DONE=%M2.1
            ERR=%M2.2
            ERRID=%VB304 ]
```

Los botones `Adelantar 5000` y `Retroceder 5000` del ESP32 leen `%VD200`, calculan el nuevo destino y escriben `%VD100` antes de pulsar `%V138.2`.

### Networks 28 a 33 - Activos y reset de errores

```text
N28  %V138.3 AND %V138.0 ----------------------( S %M1.1 )
N29  %M3.1 OR %M3.2 OR %V138.5 OR %V138.4 ----( R %M1.1 )
N30  %V138.2 AND %M1.3 ------------------------( S %M1.2 )
N31  %M2.1 OR %M2.2 OR %V138.5 OR %V138.4 ----( R %M1.2 )
N32  %V138.4 -> MOVE W#0 -> %VW304
N33  %V138.4 -> MOVE W#0 -> %VW306
```

### Network 34 - Posicion actual

```text
%SM0.0 ----[ MOVE %SMD212 -> %VD200 ]
```

### Networks 35 a 48 - Estado hacia ESP32

```text
N35  %M1.0    -> %V302.0   HomeOK
N36  %M3.1    -> %V302.1   HomeDone
N37  %M3.2    -> %V302.2   HomeErr
N38  %M2.1    -> %V302.3   PabsDone
N39  %M2.2    -> %V302.4   PabsErr
N40  %SM66.7  -> %V302.5   PTO0
N41  %M1.1    -> %V302.6   HomingActive
N42  %M1.2    -> %V302.7   PabsActive
N43  %I0.0    -> %V303.0   HomeSensor
N44  %M1.3    -> %V303.1   SystemReady
N45  %Q0.3    -> %V303.2   EnableOut
N46  %M5.0    -> %V303.3   StopDone
N47  %V138.0  -> %V303.4   CtrlEnable
N48  %V138.5  -> %V303.5   CtrlStop
```

## Secuencia de prueba recomendada

1. Cargar este programa en la Kinco y dejar COM1 como MODBUS RTU Slave, station ID `1`.
2. Desde la web del ESP32 pulsar `Leer estados PLC`.
3. Pulsar `Enable`.
4. Pulsar `HOME`.
5. Esperar `HomeOK = 1` y `SystemReady = 1`.
6. Probar `Adelantar 5000`.
7. Esperar `PabsDone = 1`.
8. Probar `Retroceder 5000`.

Si `SystemReady` no llega a `1`, revisar `HomeOK`, `HomeErr`, `PabsErr`, sensor `%I0.0` y la salida de enable.
