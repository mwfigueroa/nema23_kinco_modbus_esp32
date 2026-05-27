# Programa basico KincoBuilder para MK043E-20DT

Objetivo: dejar un programa minimo para probar comunicacion Modbus RTU con la ESP32.

La ESP usa:

- Slave ID PLC: `1`
- Baud: `9600`
- Formato sugerido: `8N1`
- Modbus FC03 para leer
- Modbus FC16 para escribir 2 holding registers
- Holding register `100` = `%VW0`
- Holding register `101` = `%VW2`
- Valor de 32 bits completo = `%VD0`

En Kinco, `%VD0` ocupa dos words consecutivos:

- `%VW0`: word alto
- `%VW2`: word bajo

## Configuracion en KincoBuilder

1. Crear proyecto nuevo para la CPU `MK043E-20DT`.
2. En hardware/puerto serie RS485, configurar el PLC como Modbus RTU slave:
   - Address: `1`
   - Baudrate: `9600`
   - Parity: `None`
   - Data bits: `8`
   - Stop bits: `1`
3. Cargar el siguiente programa en IL o armarlo en Ladder equivalente.
4. Descargar al PLC y dejarlo en RUN.

## Programa IL basico

Este programa:

- Inicializa `%VD0` en cero en el primer scan.
- Incrementa `%VD0` una vez por segundo usando `%SM0.3`.
- Copia `%VD0` a `%VD10` como espejo/debug interno.
- Enciende `%Q0.0` si `%VD0` es distinto de cero.
- Permite resetear el contador con `%I0.0`.

```iecst
(* Network 0 - Inicializacion en primer scan *)
LD %SM0.1
MOVE DI#0, %VD0
MOVE DI#0, %VD10

(* Network 1 - Incremento de 32 bits una vez por segundo *)
LD %SM0.3
R_TRIG
INC %VD0

(* Network 2 - Reset manual con entrada I0.0 *)
LD %I0.0
MOVE DI#0, %VD0

(* Network 3 - Espejo para debug *)
LD %SM0.0
MOVE %VD0, %VD10

(* Network 4 - Salida de estado: Q0.0 ON si el contador no es cero *)
LD %SM0.0
NE %VD0, DI#0
ST %Q0.0
```

## Prueba desde NEMA23 Gateway

En la UI de la ESP:

1. Pulsar `Leer VW0 / VW2`.
2. Deberia aparecer algo como:

```text
OK DINT=15 VW0=0 VW2=15
```

Cuando el contador supere `65535`, `VW2` vuelve a cero y `VW0` incrementa:

```text
OK DINT=65536 VW0=1 VW2=0
```

Si se pulsa `PLC Steps -> VW0/VW2`, la ESP sobrescribe `%VD0` con la posicion actual del NEMA23.

## Nota de direccionamiento

El manual Kinco K5 indica que el area `V` es accesible por Modbus como AI/AO, y que `%VW0 --- %VW4094` corresponde a registros Modbus `100 --- 2147`.

