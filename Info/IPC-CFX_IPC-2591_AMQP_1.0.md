# IPC-CFX / IPC-2591 / AMQP 1.0

Fecha de referencia: 2026-06-01

## Resumen

IPC-CFX significa Connected Factory Exchange. Es un estandar de intercambio de datos para manufactura digital, definido por IPC como IPC-2591. Su objetivo es que maquinas, estaciones manuales, estaciones semiautomaticas, sistemas MES/ERP, gateways e interfaces de fabrica puedan intercambiar informacion con una estructura comun.

En una arquitectura practica, CFX no reemplaza el control de bajo nivel de una maquina. El control de motores, entradas, salidas, enclavamientos y seguridad sigue resolviendose con PLC, drivers, firmware o buses industriales. CFX se usa por encima, para trazabilidad, eventos de produccion, estados de estacion, alarmas, resultados de proceso y comunicacion con sistemas de fabrica.

## Relacion entre los terminos

- IPC-2591: estandar formal publicado por IPC para Connected Factory Exchange.
- IPC-CFX: nombre usual de la tecnologia, ecosistema y conjunto de mensajes definidos por IPC-2591.
- AMQP 1.0: protocolo de mensajeria usado como capa principal de transporte.
- JSON: formato usado para codificar el contenido de los mensajes CFX.
- Endpoint: equipo, maquina, estacion, gateway o sistema de software que envia o recibe mensajes CFX.
- CFX Handle: identificador unico del endpoint dentro de la red CFX.

## Transporte sobre TCP/IP

CFX usa AMQP 1.0 sobre TCP/IP. Esto significa que el equipo no envia normalmente un texto JSON crudo por un socket TCP simple, sino un mensaje AMQP cuyo cuerpo contiene el JSON CFX.

Direcciones habituales:

```text
amqp://192.168.1.50:5672
amqps://192.168.1.50:5671
```

Puertos tipicos:

- 5672: AMQP sin TLS.
- 5671: AMQP seguro con TLS.
- 15672: interfaz web de administracion en brokers como RabbitMQ, si esta habilitada.

Topologias comunes:

```text
Maquina / Gateway / Estacion
        |
        | AMQP 1.0 sobre TCP/IP
        v
Broker AMQP o endpoint receptor
        |
        +--> MES
        +--> ERP
        +--> Dashboard
        +--> Historiador / base de datos
```

Tambien existen comunicaciones punto a punto request/response entre endpoints, ademas de publicacion/suscripcion mediante broker.

## Estructura de un mensaje CFX

El contenido de CFX viaja dentro de un sobre llamado CFXEnvelope. El sobre contiene metadatos comunes y el cuerpo especifico del mensaje.

Campos tipicos del envelope:

- MessageName: nombre completo del mensaje CFX.
- Version: version CFX del mensaje.
- TimeStamp: fecha y hora del evento, con zona horaria.
- UniqueID: identificador unico global del mensaje.
- Source: CFX Handle del emisor.
- Target: destino, si aplica.
- RequestID: usado en mensajes de request/response.
- MessageBody: contenido especifico del mensaje.

Ejemplo de envelope generico:

```json
{
  "MessageName": "CFX.ResourcePerformance.StationOnline",
  "Version": "2.0",
  "TimeStamp": "2026-06-01T12:30:00-03:00",
  "UniqueID": "4c4e0475-f1d3-4a91-89ef-95bfc61ab001",
  "Source": "Linea1.EstacionNEMA23.Gateway",
  "Target": null,
  "RequestID": null,
  "MessageBody": {
    "OfflineDuration": "00:00:10"
  }
}
```

## Ejemplos de mensajes utiles

### Estacion disponible

```json
{
  "MessageName": "CFX.ResourcePerformance.StationOnline",
  "Version": "2.0",
  "TimeStamp": "2026-06-01T12:30:00-03:00",
  "UniqueID": "6d83e2b0-293c-4a9d-a289-1f2d9d000001",
  "Source": "Linea1.EstacionNEMA23.Gateway",
  "Target": null,
  "RequestID": null,
  "MessageBody": {
    "OfflineDuration": "00:02:15"
  }
}
```

### Cambio de estado de estacion

```json
{
  "MessageName": "CFX.ResourcePerformance.StationStateChanged",
  "Version": "2.0",
  "TimeStamp": "2026-06-01T12:31:05-03:00",
  "UniqueID": "6d83e2b0-293c-4a9d-a289-1f2d9d000002",
  "Source": "Linea1.EstacionNEMA23.Gateway",
  "Target": null,
  "RequestID": null,
  "MessageBody": {
    "OldState": 2200,
    "OldStateDuration": "00:03:12",
    "NewState": 1100,
    "RelatedFault": null
  }
}
```

### Inicio de ciclo de trabajo

```json
{
  "MessageName": "CFX.Production.WorkStarted",
  "Version": "2.0",
  "TimeStamp": "2026-06-01T12:32:10-03:00",
  "UniqueID": "6d83e2b0-293c-4a9d-a289-1f2d9d000003",
  "Source": "Linea1.EstacionNEMA23.Gateway",
  "Target": null,
  "RequestID": null,
  "MessageBody": {
    "TransactionID": "TX-20260601-000123",
    "Lane": 1,
    "UnitCount": 1,
    "Units": [
      {
        "UnitIdentifier": "PIEZA-000123",
        "PositionNumber": 1,
        "PositionName": "EJE-X",
        "X": 0.0,
        "Y": 0.0,
        "Rotation": 0.0,
        "FlipX": false,
        "FlipY": false
      }
    ]
  }
}
```

### Fin de ciclo de trabajo

```json
{
  "MessageName": "CFX.Production.WorkCompleted",
  "Version": "2.0",
  "TimeStamp": "2026-06-01T12:32:42-03:00",
  "UniqueID": "6d83e2b0-293c-4a9d-a289-1f2d9d000004",
  "Source": "Linea1.EstacionNEMA23.Gateway",
  "Target": null,
  "RequestID": null,
  "MessageBody": {
    "TransactionID": "TX-20260601-000123",
    "Result": "Completed",
    "PrimaryIdentifier": "PIEZA-000123",
    "HermesIdentifier": null,
    "UnitCount": 1,
    "Units": []
  }
}
```

### Alarma o falla

```json
{
  "MessageName": "CFX.ResourcePerformance.StationStateChanged",
  "Version": "2.0",
  "TimeStamp": "2026-06-01T12:33:15-03:00",
  "UniqueID": "6d83e2b0-293c-4a9d-a289-1f2d9d000005",
  "Source": "Linea1.EstacionNEMA23.Gateway",
  "Target": null,
  "RequestID": null,
  "MessageBody": {
    "OldState": 1100,
    "OldStateDuration": "00:00:25",
    "NewState": 5500,
    "RelatedFault": {
      "Cause": "MechanicalFailure",
      "Severity": "Error",
      "FaultCode": "NEMA23_LIMIT_SWITCH_X_MAX",
      "FaultOccurrenceId": "e49c60a4-02bb-4f34-95a8-08154f070001",
      "Lane": null,
      "Stage": null,
      "SideLocation": "Unknown",
      "AccessType": "Unknown"
    }
  }
}
```

## Aplicacion al proyecto ESP32 / Kinco PLC / NEMA23

Para este proyecto, CFX puede servir como capa de informacion de fabrica, no como lazo de control del motor.

Arquitectura recomendada:

```text
ESP32 / Kinco PLC
    |
    | Senales digitales, Modbus, RS485, Ethernet o protocolo local
    v
Gateway local en PC industrial o Raspberry Pi
    |
    | AMQP 1.0 + JSON CFX
    v
Broker AMQP / MES / Dashboard / base de datos
```

Eventos que conviene mapear a CFX:

- estacion encendida o disponible;
- ciclo iniciado;
- ciclo terminado;
- pieza o lote procesado;
- posicion alcanzada;
- alarma de driver;
- fin de carrera activado;
- parada de emergencia;
- reset de alarma;
- cambio de receta o parametros;
- contador de piezas buenas y rechazadas.

Ejemplo de eventos internos simples antes de traducir a CFX:

```json
{
  "event": "cycle_completed",
  "station": "NEMA23_X",
  "piece_id": "PIEZA-000123",
  "position_mm": 125.5,
  "result": "ok"
}
```

Ese evento interno se puede transformar en un `CFX.Production.WorkCompleted` o en otro mensaje CFX mas especifico, segun el proceso real.

## Diferencia con MQTT, HTTP y OPC UA

- MQTT: muy util para telemetria IoT, pero no define por si mismo el contenido estandarizado de manufactura. CFX si define mensajes y campos.
- HTTP/REST: simple para integraciones puntuales, pero no es el transporte principal CFX.
- OPC UA: fuerte para modelado y acceso a datos industriales. CFX esta mas orientado a eventos y mensajes semanticos de manufactura electronica.
- Modbus TCP: simple y robusto para registros de control/estado, pero no define eventos de produccion ni trazabilidad de alto nivel.

## Recomendaciones de implementacion

1. Mantener el control critico en PLC/ESP32.
2. Usar CFX para reportar informacion, no para decisiones de seguridad o movimiento en tiempo real.
3. Implementar un gateway si el ESP32 no tiene recursos suficientes para AMQP 1.0 completo.
4. Definir un CFX Handle estable para cada estacion.
5. Usar `TransactionID` para unir inicio, etapas y fin de ciclo.
6. Registrar todos los mensajes salientes para trazabilidad y depuracion.
7. Usar `amqps://` con TLS si la informacion sale de la red OT local.
8. Validar la estructura de mensajes contra el SDK o herramientas CFX cuando se busque compatibilidad formal.

## Fuentes

- IPC-CFX FAQ: https://www.ipc.org/ipc-cfx-faq
- IPC-2591 Connected Factory Exchange: https://www.ipc.org/ipc-2591-connected-factory-exchange-cfx
- IPC-CFX AMQP Version 1.0 Guide: https://www.ipc.org/ipc-cfx-amqp-version-10-guide
- IPC CFX SDK and software resources: https://www.ipc.org/cfx-sdk-and-software-developer-resources
- IPC release IPC-2591 CFX Version 2.0, 2025-04-22: https://www.ipc.org/news-release/ipc-releases-version-20-ipc-2591-connected-factory-exchange-expanded-device-coverage
- AMQP 1.0 specifications: https://www.amqp.org/resources/specifications/
- CFXEnvelope reference: https://www.connectedfactoryexchange.com/CFXDemo/sdk/html/T_CFX_CFXEnvelope.htm
- CFX Production WorkStarted reference: https://www.connectedfactoryexchange.com/cfxdemo/sdk/html/T_CFX_Production_WorkStarted.htm
