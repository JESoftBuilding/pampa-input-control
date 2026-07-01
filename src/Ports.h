#ifndef INPUT_CONTROL_PORTS_H
#define INPUT_CONTROL_PORTS_H
#include <cstdint>
#include "ButtonActions.h"

namespace input_control {

// El host ejecuta el efecto. effect_id es OPACO (0 = None); el módulo no sabe qué significa.
struct IEffectSink {
    virtual void execute(uint8_t effect_id) = 0;
    virtual ~IEffectSink() = default;
};

// Una estrategia declara sus botones y reacciona. onButtonAction devuelve un effect_id.
struct IButtonStrategy {
    virtual const ButtonBinding* bindings(uint8_t& count) const = 0;
    virtual uint8_t onButtonAction(uint8_t action_id) = 0;
    virtual void    onNav(const NavInput& nav) = 0;
    virtual ~IButtonStrategy() = default;
};

// Mensajes inbound que NO son ControlPacket (p.ej. request de dump de telemetría).
struct IRawInbound {
    virtual void onMessage(const uint8_t* data, uint8_t len) = 0;
    virtual ~IRawInbound() = default;
};

} // namespace input_control
#endif
