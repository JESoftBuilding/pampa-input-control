# pampa-input-control

Núcleo **teleop** de Pampa: el enlace **mando ↔ robot** empaquetado como librería
PlatformIO independiente y versionada. Es la **fuente única de verdad** del protocolo, así
el mando (`PampaController`) y los robots (`PampaV2`, `PampaV3`) no pueden divergir en el cable
— comparten la misma versión por construcción.

> Extraído de `PampaV3/lib/input_control/` (módulo hexagonal). Estado inicial: **0.1.0**
> (en extracción; todavía no consumido por los repos vía `lib_deps`).

## Qué incluye (agnóstico al robot)

| Pieza | Archivo | Rol |
|---|---|---|
| Protocolo | `ControlProtocol.h` | `ControlPacket` (10 B) + `PROTO_VERSION` — el contrato del cable |
| Puerto de enlace | `ILinkTransport.h` | interfaz del transporte |
| Adaptador ESP-NOW | `EspNowTransport.h/.cpp` | broadcast, canal, freshness (ESP32; excluido en native vía `NATIVE_BUILD`) |
| Ruteo | `ButtonRouter.h/.cpp`, `ButtonActions.h`, `Ports.h` | botones/encoder → efectos + nav |
| Fachada | `InputControlModule.h/.cpp` | end-to-end: decodifica y expone `lx/ly/rx/ry`, `buttons`, `isFresh()` |

**No incluye** (queda en cada robot): mezcla de motores / cinemática, e implementaciones de
`IEffectSink` / `IButtonStrategy` (qué *hace* cada botón en ese robot).

## Cómo se consume (PlatformIO `lib_deps`)

```ini
lib_deps =
  https://github.com/<owner>/pampa-input-control.git#v0.1.0
```

Cada repo **fija una versión** (tag semver) y actualiza cuando quiere → actualización
independiente, estilo paquete. La URL del `origin` se completa cuando exista el remoto.

## Compatibilidad y versionado

- **`PROTO_VERSION`** viaja en cada `ControlPacket`. Todo cambio del cable **bumpea
  `PROTO_VERSION` + major semver**; el receptor puede rechazar mismatches.
- Mando + V2 + V3 apuntando al **mismo tag** ⇒ idénticos en el cable, siempre.

## Testeabilidad

El protocolo y el router son **native-testable** (host). El adaptador ESP-NOW se excluye del
build native con `#ifndef NATIVE_BUILD`.

## Pendiente de extracción (próximos pasos, no hechos aún)

1. Consolidar el protocolo acá y **borrar los duplicados** en `PampaV3/lib/input_control/` y
   `PampaController/include/proto/control_packet.h`, repuntando includes.
2. Mover los tests native del módulo.
3. `platformio.ini` con env `native` para CI de la propia librería.
4. Tag `v1.0.0` y repuntar `lib_deps` de mando + V3; luego sumar V2.

## Protocolo de dispositivos (desde 2.7.0, rama `feature/device-protocol`)

Además del enlace mando ↔ robot, la librería define una **segunda familia de paquetes**
para lo que no es un robot: la consola de la sala, el receptor USB de la PC, el pedal y
las luces. Van **cifrados y autenticados**. Se agregó **sin modificar**
`ControlProtocol.h` ni `ConfigProtocol.h`: el mando y los robots siguen en sus tags y no
hay que recargarlos. El enlace mando ↔ robot sigue sin cifrar.

| Pieza | Archivo | Rol |
|---|---|---|
| Familia nueva | `DeviceProtocol.h` | formato del cable, `sealFrame` / `receiveFrame`, y los mensajes `LightCommand`, `Event` e `InputState` |
| Contador | `DeviceCounter.h` | `TxCounter` (emisor) y `ReplayGuard` (receptor): nonce único y descarte de copias y reenvíos |
| Cifrado | `crypto/ChaCha20Poly1305.h` | AEAD de la RFC 8439, sin dependencias, igual en ESP32, ESP8266 y la PC |
| Transporte ESP8266 | `Esp8266NowTransport.h/.cpp` | `ILinkTransport` sobre ESP-NOW con la API de ESP8266. `EspNowTransport` sigue siendo el de ESP32 y no cambió; cada uno se compila sólo en su plataforma |

### Formato en el aire

| Bytes | Campo | |
|---|---|---|
| 0 | `magic` | 0x4C. No es el primer byte de ningún paquete del enlace mando ↔ robot |
| 1 | `version` | 2. La 1 fue el borrador sin cifrar y no se acepta |
| 2 | `type` | `DevMsg` |
| 3 | `source` | `DeviceId` de quien emite |
| 4 | `target` | `DeviceId` destino, o 0 para todos |
| 5 a 8 | `epoch` | uint32 little-endian |
| 9 a 12 | `seq` | uint32 little-endian |
| 13 en adelante | carga | cifrada, hasta 35 bytes |
| últimos 16 | etiqueta | cubre la cabecera y la carga |

La cabecera va en claro pero autenticada. El paquete más largo mide 64 bytes, que es lo
que acepta `EspNowTransport`.

### Uso

Emisor: un solo `TxCounter` por dispositivo, creado con la época de este arranque.

```cpp
TxCounter tx(epoch);                       // epoch: leída de memoria permanente, +1, guardada
DevCounter c;
if (tx.next(c)) {
    uint8_t frame[DEV_MAX_FRAME];
    uint8_t n = sealLightCommand(KEY, (uint8_t)DeviceId::Console, (uint8_t)DeviceId::Lights,
                                 c, LightCommand{LightCmd::Power, 0, 0, 0, 2}, frame, sizeof(frame));
    for (int i = 0; i < 3; ++i) transport.send(frame, n);   // el MISMO paquete, repetido
}
```

Receptor: siempre `receiveFrame`, que verifica y recién después descarta copias.

```cpp
ReplayGuard guard;
DevHeader h; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t len;
if (receiveFrame(KEY, guard, data, n, h, payload, sizeof(payload), len) && isForDevice(h, MY_ID)) {
    LightCommand cmd;
    if (decodeLightCommand(h, payload, len, cmd)) { /* aplicar */ }
}
```

### La clave

32 bytes al azar, los mismos en todos los dispositivos de la sala. **No va en esta
librería ni en git**: cada firmware la incluye desde un archivo ignorado. Para generarla:

```
py -c "import secrets; print(', '.join(f'0x{b:02x}' for b in secrets.token_bytes(32)))"
```

### Reglas

- Cada dispositivo tiene un `DeviceId` único y un solo `TxCounter`. El nonce es
  `(source, epoch, seq)`: repetirlo con la misma clave rompe el cifrado.
- La época se guarda en memoria permanente y se incrementa una vez por arranque, antes
  de emitir. Si esa memoria se borra, hay que cambiar la clave de toda la sala.
- Los ids de dispositivo arrancan en 16; del 1 al 15 son de robots (`RobotId`).
- La sala usa `DEV_CHANNEL` (6), distinto del canal de los robots (1).
- Los valores de `DevMsg` y `LightCmd` viajan por el aire: no se renumeran.

### Verificación

`test/test_crypto` usa los vectores oficiales de la RFC 8439 (§2.3.2, §2.5.2, §2.8.2 y los
casos límite A.3 #5 a #10). `test/test_device` incluye un paquete calculado con otra
implementación (Python `cryptography`) y prueba que cambiar un bit en cualquier byte lo
invalida.

### Límites

- Se ve **quién** habla con **quién**, de qué tipo es el mensaje y cuándo. Sólo la carga
  va cifrada.
- La clave es una sola. Quien tenga en la mano cualquier dispositivo puede leerla del
  flash, que no está cifrado, y con eso leer y emitir todo.
- Un receptor recién reiniciado olvidó los contadores: hasta que le llegue un paquete
  legítimo de una fuente, acepta uno grabado antes de esa fuente.
- `ReplayGuard` recuerda 8 fuentes; con más, olvida a la que hace más que no habla.
- No protege de interferencia: alguien puede tapar la radio.
- La implementación del cifrado coincide con la norma en los vectores, pero no fue
  auditada por terceros.

Pendiente en esta rama: el paquete de estado de la PC hacia la consola, y guardar la
época en cada firmware que emite.
