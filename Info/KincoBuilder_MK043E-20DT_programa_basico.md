# Programa basico KincoBuilder para MK043E-20DT

Objetivo: dejar un programa minimo para probar comunicacion Modbus RTU con la ESP32.

La ESP usa:

- Slave ID PLC: `1`
- Baud: `115200` (configurar en KincoBuilder; ESP usa 115200 por defecto)
- Formato sugerido: `8N1`
- Modbus FC03 para leer
- Modbus FC16 para escribir 2 holding registers
- Holding register `100` = `%VW0`
- Holding register `101` = `%VW2`
- Valor de 32 bits completo = `%VD0`

En Kinco, `%VD0` ocupa dos words consecutivos:

- `%VW0`: word bajo
- `%VW2`: word alto

## Configuracion en KincoBuilder

1. Crear proyecto nuevo para la CPU `MK043E-20DT`.
2. En hardware/puerto serie RS485, configurar el PLC como Modbus RTU slave:
   - Address: `1`
   - Baudrate: `115200` (o el máximo que KincoBuilder permita para el slave RTU; ESP configurado a 115200)
   - Parity: `None`
   - Data bits: `8`
   - Stop bits: `1`
3. Cargar el siguiente programa en IL o armarlo en Ladder equivalente.
4. Descargar al PLC y dejarlo en RUN.

## Programa IL basico

Este programa:

- Inicializa `%VD0` en cero en el primer scan.
- Copia `%VD0` a `%VD10` como espejo/debug interno.
- Enciende `%Q0.0` si `%VD0` es distinto de cero.
- Permite resetear el contador con `%I0.0`.

```iecst
(* Network 0 - Inicializacion en primer scan *)
LD %SM0.1
MOVE DI#0, %VD0
MOVE DI#0, %VD10

(* Network 1 - Reset manual con entrada I0.0 *)
LD %I0.0
MOVE DI#0, %VD0

(* Network 2 - Espejo para debug *)
LD %SM0.0
MOVE %VD0, %VD10

(* Network 3 - Salida de estado: Q0.0 ON si el contador no es cero *)
LD %SM0.0
NE %VD0, DI#0
ST %Q0.0
```

## Prueba desde NEMA23 Gateway

En la UI de la ESP:

1. Pulsar `Leer VW0 / VW2`.
2. Deberia aparecer algo como:

```text
OK DINT=15 VW0=15 VW2=0
```

Cuando el contador supere `65535`, `VW0` vuelve a cero y `VW2` incrementa:

```text
OK DINT=65536 VW0=0 VW2=1
```

Si se pulsa `PLC Steps -> VW0/VW2`, la ESP sobrescribe `%VD0` con la posicion actual del NEMA23.
Para que la lectura posterior sea igual, el programa del PLC no debe escribir ni incrementar `%VD0`
en otra red de ladder.

## Nota de direccionamiento

El manual Kinco K5 indica que el area `V` es accesible por Modbus como AI/AO, y que `%VW0 --- %VW4094` corresponde a registros Modbus `100 --- 2147`.
