# Kinco MK043E-20DT - PABS basico por MODBUS con vuelta a cero

Objetivo: probar solo `PABS` en `AXIS = 0`, sin entradas fisicas y sin HOME.

Esta version usa una sola instruccion `PABS` para el eje 0. La ida y la vuelta
comparten el destino activo `%VD128`; esto evita tener dos bloques motion sobre
el mismo eje.

Flujo:

```text
Idle, driver deshabilitado
MODBUS escribe %VD100 con pasos destino distinto de 0
PLC copia el destino a %VD120 y %VD128, borra %VD100, habilita driver
PABS unico a %VD128
espera 3 segundos
PLC cambia %VD128 a 0
PABS unico a 0
driver deshabilitado
```

## Cableado usado

| Funcion | PLC |
|---|---|
| STEP / PUL | `%Q0.0` |
| DIR | `%Q0.2` |
| Enable driver | `%Q0.3` |

`%Q0.0` y `%Q0.2` son manejadas por `PABS`; no se fuerzan en ladder.
No se usa ninguna entrada fisica.

## Mapa MODBUS

| MODBUS | Kinco | Tipo | Uso |
|---:|---|---|---|
| 40051-40052 | `%VD100` | DINT | Escribir pasos destino; distinto de 0 arranca ciclo |
| 40053-40054 | `%VD104` | DWORD | Frecuencia maxima PABS |
| 40055 | `%VW108` | WORD | Frecuencia minima PABS |
| 40056 | `%VW110` | WORD | Tiempo aceleracion/desaceleracion |
| 40101-40102 | `%VD200` | DINT | Posicion actual copiada desde `%SMD212` |
| 40152 | `%VW302` | WORD | Bits de estado |
| 40153 | `%VW304` | WORD | `%VB304` error PABS actual |
| 40154 | `%VW306` | WORD | Reservado/limpiado |

Nota de direccionamiento: si el master usa base 0, `40051` se escribe como
address `50`.

## Bits de estado `%VW302` / 40152

| Bit | Direccion | Nombre |
|---:|---|---|
| 0 | `%V302.0` | CycleActive |
| 1 | `%V302.1` | MoveOutActive |
| 2 | `%V302.2` | WaitReturnActive |
| 3 | `%V302.3` | ReturnActive |
| 4 | `%V302.4` | CycleDone |
| 5 | `%V302.5` | CycleErr |
| 6 | `%V302.6` | PabsOutDone |
| 7 | `%V302.7` | PabsOutErr |
| 8 | `%V303.0` | PabsReturnDone |
| 9 | `%V303.1` | PabsReturnErr |
| 10 | `%V303.2` | EnableOut |
| 11 | `%V303.3` | WaitDone |

## Redes principales (OPTIMIZADO — 30 redes)

```text
N0   %SM0.0 -> R %SM201.7, R %SM201.3  (liberar stop + direccion auto)
N1   %SM0.1 -> ST %SM201.6             (reset posicion)
N2   %SM0.1 -> MOVE DI#0,%VD100...     (init registros datos)
N3   %SM0.1 -> MOVE W#0,%VW302...      (init registros estado)
N4   %SM0.1 -> R %M0.4...%M3.2         (init marcas internas)
N5   Detecta comando: %SM0.0 AND NOT CycleActive AND %VD100 != 0 -> %M0.4
N6   %M0.4 -> copia %VD100 a %VD120/%VD128; borra %VD100; limpia errores
N7   %M0.4 -> S CycleActive+MoveOutActive; R todos los estados anteriores
N8   CycleActive -> %Q0.3 enable driver
N9   TON T0, 3000 mientras WaitReturnActive -> %M0.6
N10  WaitReturnActive AND WaitDone -> %M0.5 (StartReturnPulse)
N11  %M0.5 -> carga %VD124(0) a %VD128; R espera; S ReturnActive; limpia flags
N12  %M0.4 OR %M0.5 -> %M0.7 (StartPabsPulse comun)
N13  PABS 0, %M0.7, %VW108, %VD104, %VW110, %VD128, %M2.0, %M2.1, %VB304
N14  MoveOutActive AND RawDone -> OutDone + WaitReturnActive + desactiva ida
N15  MoveOutActive AND RawErr -> OutErr + CycleErr + aborta ciclo
N16  ReturnActive AND RawDone -> ReturnDone + CycleDone + cierra ciclo
N17  ReturnActive AND RawErr -> ReturnErr + CycleErr + cierra ciclo
N18  Copia %SMD212 a %VD200
N19-N30 Publica bits hacia %VW302 / 40152 (1 LD+ST por bit = 12 redes)
```

**Reduccion**: de 88 redes a 30 redes (66% menos).
KincoBuilder **no permite multiples LD en una misma red IL** (cada red =
un rung de ladder con una unica conexion al power rail izquierdo).

## Prueba desde MODBUS

1. Cargar `pabs_basico_5000.ilp` y `pabs_basico_5000.kgv` en KincoBuilder.
2. Dejar la PLC en RUN y confirmar MODBUS RTU Slave, ID `1`, `9600 8N1`.
3. Escribir `40051-40052 = 5000` como DINT.
4. Debe verse `%Q0.3 = 1`, movimiento a `5000`, espera de 3 s, vuelta a `0`, `%Q0.3 = 0`.
5. Para sentido contrario, escribir `40051-40052 = -5000` como DINT.

Si el master usa direcciones base 0:

```text
40051-40052 -> address 50, cantidad 2 registros
40152       -> address 151
40101-40102 -> address 100
```
