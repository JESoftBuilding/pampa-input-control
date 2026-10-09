#ifndef INPUT_CONTROL_DEVICE_PROTOCOL_H
#define INPUT_CONTROL_DEVICE_PROTOCOL_H
#include <cstdint>
#include <cstring>

// Protocolo de DISPOSITIVOS: la familia de paquetes para todo lo que no es el enlace
// mando<->robot (consola de la sala, receptor USB de la PC, pedal, luces, lo que venga).
//
// Por qué una familia aparte y no más paquetes "sueltos": los del enlace mando<->robot se
// distinguen por (tamaño + primer byte) y no tienen cabecera común (ver ConfigProtocol.h).
// Acá TODOS empiezan con la misma cabecera, y se distinguen por `type`. Agregar un paquete
// nuevo es agregar un valor a DevMsg: no hay que cuidar tamaños.
//
// Convivencia con lo que ya existe (no se modificó nada de ControlProtocol/ConfigProtocol):
//   - El primer byte es DEV_MAGIC (0x4C). Ningún paquete del enlace mando<->robot empieza
//     así (usan 0x01, 0x02, 0x03 y 0xA5), así que mando y robots descartan estos paquetes
//     y este decodificador descarta los de ellos. Lo fija test/test_device.
//   - La sala usa OTRO canal (DEV_CHANNEL) que los robots (canal 1): el tráfico de la sala
//     no renueva la "frescura" de transporte de un robot ni compite por su cola de RX.
//   - La consola NO usa esta familia para sus teclas: emite el ControlPacket de siempre
//     (16 botones, 4 ejes, encoder) con target_robot_id = DeviceId::Receiver.
namespace input_control {

constexpr uint8_t DEV_MAGIC     = 0x4C;   // 'L' — heredado del WirePacket de luces-rgb
constexpr uint8_t DEV_VERSION   = 1;
constexpr uint8_t DEV_CHANNEL   = 6;      // canal WiFi de la sala (los robots usan el 1)
constexpr uint8_t DEVICE_ID_ALL = 0;      // target: todos los dispositivos obedecen

// Ids de dispositivo. Comparten el espacio de numeración con RobotId (ControlProtocol.h):
// 0 = todos, 1..15 = robots, 16 en adelante = dispositivos. Así un ControlPacket dirigido
// a un dispositivo nunca coincide con el id de un robot.
enum class DeviceId : uint8_t {
    All      = DEVICE_ID_ALL,
    Receiver = 16,   // ESP enchufado a la PC: puente ESP-NOW <-> puerto serie
    Console  = 17,   // la consola de teclas, perilla y pantalla
    Pedal    = 18,
    Lights   = 19,   // controlador de la tira RGB (luces-rgb)
};

// Tipo de mensaje. Los valores viajan por el aire: no se renumeran.
enum class DevMsg : uint8_t {
    LightCommand = 0x10,
    Event        = 0x20,
};

// Comandos de luces. MISMOS valores que CommandType de luces-rgb (lib/core/Command.h).
enum class LightCmd : uint8_t {
    SetColor         = 1,   // r, g, b. Enciende.
    SetBrightness    = 2,   // value = brillo 0..255
    AdjustBrightness = 3,   // value = delta con signo (int8): pasos de un encoder
    Preset           = 4,   // value = índice de preset. Enciende.
    Power            = 5,   // value = 0 apagar, 1 encender, 2 alternar
};

// Qué le pasó a un control de un dispositivo simple (el pedal, un botón suelto).
enum class EventAction : uint8_t {
    Press   = 1,
    Release = 2,
};

#pragma pack(push, 1)
// Cabecera común. Sólo campos de un byte: no hay relleno ni orden de bytes que cuidar.
struct DevHeader {
    uint8_t magic;     // = DEV_MAGIC
    uint8_t version;   // = DEV_VERSION
    uint8_t type;      // DevMsg
    uint8_t source;    // DeviceId de quien emite (el transporte no conserva la MAC)
    uint8_t target;    // DeviceId destino, o DEVICE_ID_ALL
    uint8_t seq;       // lo incrementa el emisor; permite descartar repeticiones
};                                                       // 6 bytes
struct LightCommandPacket {
    DevHeader h;
    uint8_t   cmd;        // LightCmd
    uint8_t   r, g, b;
    uint8_t   value;
};                                                       // 11 bytes
struct EventPacket {
    DevHeader h;
    uint8_t   control;    // cuál control del dispositivo (un pedal doble tiene 1 y 2)
    uint8_t   action;     // EventAction
};                                                       // 8 bytes
#pragma pack(pop)

static_assert(sizeof(DevHeader)          == 6,  "DevHeader debe ser 6 bytes (pack 1)");
static_assert(sizeof(LightCommandPacket) == 11, "LightCommandPacket debe ser 11 bytes (pack 1)");
static_assert(sizeof(EventPacket)        == 8,  "EventPacket debe ser 8 bytes (pack 1)");

inline DevHeader makeHeader(DevMsg type, uint8_t source, uint8_t target, uint8_t seq) {
    return {DEV_MAGIC, DEV_VERSION, static_cast<uint8_t>(type), source, target, seq};
}

// true si los bytes empiezan con una cabecera de esta familia y de esta versión.
inline bool decodeHeader(const uint8_t* data, uint8_t len, DevHeader& out) {
    if (len < sizeof(DevHeader)) return false;
    if (data[0] != DEV_MAGIC || data[1] != DEV_VERSION) return false;
    std::memcpy(&out, data, sizeof(out));
    return true;
}

// true si el paquete es para este dispositivo (su id) o para todos. Espeja isForRobot.
inline bool isForDevice(const DevHeader& h, uint8_t my_device_id) {
    return h.target == DEVICE_ID_ALL || h.target == my_device_id;
}

inline LightCommandPacket makeLightCommand(uint8_t source, uint8_t target, uint8_t seq,
                                           LightCmd cmd, uint8_t r, uint8_t g, uint8_t b,
                                           uint8_t value) {
    return {makeHeader(DevMsg::LightCommand, source, target, seq),
            static_cast<uint8_t>(cmd), r, g, b, value};
}

inline bool decodeLightCommand(const uint8_t* data, uint8_t len, LightCommandPacket& out) {
    DevHeader h;
    if (len != sizeof(LightCommandPacket) || !decodeHeader(data, len, h)) return false;
    if (h.type != static_cast<uint8_t>(DevMsg::LightCommand)) return false;
    LightCommandPacket p;
    std::memcpy(&p, data, sizeof(p));
    if (p.cmd < static_cast<uint8_t>(LightCmd::SetColor) ||
        p.cmd > static_cast<uint8_t>(LightCmd::Power)) return false;
    out = p;
    return true;
}

inline EventPacket makeEvent(uint8_t source, uint8_t target, uint8_t seq,
                             uint8_t control, EventAction action) {
    return {makeHeader(DevMsg::Event, source, target, seq), control,
            static_cast<uint8_t>(action)};
}

inline bool decodeEvent(const uint8_t* data, uint8_t len, EventPacket& out) {
    DevHeader h;
    if (len != sizeof(EventPacket) || !decodeHeader(data, len, h)) return false;
    if (h.type != static_cast<uint8_t>(DevMsg::Event)) return false;
    EventPacket p;
    std::memcpy(&p, data, sizeof(p));
    if (p.action < static_cast<uint8_t>(EventAction::Press) ||
        p.action > static_cast<uint8_t>(EventAction::Release)) return false;
    out = p;
    return true;
}

} // namespace input_control
#endif
