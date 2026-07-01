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
