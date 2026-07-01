#ifndef INPUT_CONTROL_CONTROL_PROTOCOL_H
#define INPUT_CONTROL_CONTROL_PROTOCOL_H
#include <cstdint>
#include <cstring>
#include "ButtonActions.h"

namespace input_control {

constexpr uint8_t PROTO_VERSION = 1;

// Orden ESTABLE — el bit de cada botón es (1 << índice). Debe coincidir con el mando.
enum class Btn : uint8_t {
    Up, Down, Left, Right,
    Cross, Circle, Square, Triangle,
    Start, Select, Mode,
    COUNT
};

// nav_flags
constexpr uint8_t NAV_CLICK     = 0x01;
constexpr uint8_t NAV_LONGPRESS = 0x02;

// Paquete mando→robot, 50 Hz, 10 bytes packed. nav_flags/nav_delta antes eran
// menu_action/reserved (sin uso) → cambio backward-compatible.
typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  seq;
    int8_t   lx, ly;
    int8_t   rx, ry;
    uint16_t buttons;
    uint8_t  nav_flags;
    int8_t   nav_delta;
} ControlPacket;

inline bool decodeControl(const uint8_t* data, uint8_t len, ControlPacket& out) {
    if (len != sizeof(ControlPacket)) return false;
    if (data[0] != PROTO_VERSION)     return false;
    std::memcpy(&out, data, sizeof(out));
    return true;
}

inline NavInput extractNav(const ControlPacket& p) {
    NavInput n;
    n.delta      = p.nav_delta;
    n.click      = (p.nav_flags & NAV_CLICK)     != 0;
    n.long_press = (p.nav_flags & NAV_LONGPRESS) != 0;
    return n;
}

} // namespace input_control
#endif
