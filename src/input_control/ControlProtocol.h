#ifndef INPUT_CONTROL_CONTROL_PROTOCOL_H
#define INPUT_CONTROL_CONTROL_PROTOCOL_H
#include <cstdint>
#include <cstring>
#include "ButtonActions.h"

namespace input_control {

// v2: se agregó target_robot_id (ruteo mando→robot). Rompe compatibilidad con v1
// (cambia tamaño y versión) → todos los extremos deben usar la misma versión de la lib.
constexpr uint8_t PROTO_VERSION = 2;

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

// Ruteo mando→robot. 0 = broadcast (todos los robots obedecen). El robot ignora los
// ControlPacket cuyo target_robot_id no sea ni el suyo ni ROBOT_ID_ALL (ver isForRobot).
constexpr uint8_t ROBOT_ID_ALL = 0;

// IDs canónicos de robot (nombre ↔ id) — FUENTE ÚNICA compartida por el mando y los robots.
// El mando estampa el id elegido; cada robot se configura con el suyo.
enum class RobotId : uint8_t {
    All       = ROBOT_ID_ALL,   // 0 — broadcast
    PampaV3   = 1,              // diferencial — coincide con catalog/Robots.h del mando + telemetría
    PampaV2   = 2,              // holonómico — placa DevKit (esp32doit-devkit-v1)
    PampaV2S3 = 3,             // holonómico — placa Waveshare S3-Zero; mismo firmware, id propio
                               // para que el mando direccione S3 y DevKit por separado.
};

// Paquete mando→robot, 50 Hz, 11 bytes packed.
typedef struct __attribute__((packed)) {
    uint8_t  version;          // PROTO_VERSION
    uint8_t  target_robot_id;  // a qué robot va (0 = broadcast/todos)
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

// Filtro de destino que aplica el robot tras decodificar: true si el paquete es para
// este robot (su id) o para todos (ROBOT_ID_ALL).
inline bool isForRobot(const ControlPacket& p, uint8_t my_robot_id) {
    return p.target_robot_id == ROBOT_ID_ALL || p.target_robot_id == my_robot_id;
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
