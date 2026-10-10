#ifndef INPUT_CONTROL_DEVICE_PROTOCOL_H
#define INPUT_CONTROL_DEVICE_PROTOCOL_H
#include <cstdint>
#include <cstring>
#include "DeviceCounter.h"
#include "crypto/ChaCha20Poly1305.h"

// Protocolo de DISPOSITIVOS: la familia de paquetes para todo lo que no es el enlace
// mando<->robot (consola de la sala, receptor USB de la PC, pedal, luces, lo que venga).
// Todos los paquetes van CIFRADOS y AUTENTICADOS.
//
// Formato en el aire (versión 2):
//
//   byte  0      magic    = DEV_MAGIC (0x4C)
//   byte  1      version  = DEV_VERSION (2)
//   byte  2      type     DevMsg
//   byte  3      source   DeviceId de quien emite
//   byte  4      target   DeviceId destino, o DEVICE_ID_ALL
//   bytes 5..8   epoch    uint32 little-endian  ┐ contador: ver DeviceCounter.h
//   bytes 9..12  seq      uint32 little-endian  ┘
//   bytes 13..   carga    cifrada con ChaCha20
//   últimos 16   etiqueta Poly1305 sobre cabecera + carga cifrada
//
// La cabecera viaja en claro (hace falta para rutear y para armar el nonce) pero está
// cubierta por la etiqueta: cambiarle un bit invalida el paquete.
//
// Qué garantiza, frente a alguien con una radio dentro del alcance y SIN la clave:
//   - no puede leer la carga;
//   - no puede fabricar ni modificar un paquete;
//   - no puede volver a emitir uno grabado (receiveFrame + ReplayGuard).
// Qué NO garantiza: ver "Límites" en el README. En corto: se ve quién habla con quién y
// cuándo; la clave es una sola para toda la sala y sale del flash de cualquier
// dispositivo; un receptor recién reiniciado acepta una vez un paquete viejo.
//
// Reglas para quien use esto:
//   - Cada dispositivo tiene un DeviceId ÚNICO y UN solo TxCounter para todo lo que emite.
//     El nonce es (source, epoch, seq): dos emisores con el mismo id, o un contador que
//     vuelve atrás, repiten nonce y rompen el cifrado.
//   - La clave NO vive en esta librería ni en git: cada firmware la trae de un archivo
//     ignorado. 32 bytes al azar, los mismos en todos los dispositivos de la sala.
//   - En recepción usar receiveFrame(), no openFrame() a secas: es la que descarta copias.
//
// Convivencia con el enlace mando<->robot (ControlProtocol.h / ConfigProtocol.h, que NO se
// modificaron): el primer byte 0x4C no es el de ningún paquete de ese enlace (0x01, 0x02,
// 0x03, 0xA5), así que cada lado descarta los del otro; y la sala usa otro canal.
// La versión 1 fue el borrador sin cifrar (el WirePacket de luces-rgb): no se acepta.
namespace input_control {

constexpr uint8_t DEV_MAGIC     = 0x4C;   // 'L' — heredado del WirePacket de luces-rgb
constexpr uint8_t DEV_VERSION   = 2;
constexpr uint8_t DEV_CHANNEL   = 6;      // canal WiFi de la sala (los robots usan el 1)
constexpr uint8_t DEVICE_ID_ALL = 0;      // target: todos los dispositivos obedecen

constexpr uint8_t DEV_KEY_LEN     = 32;
constexpr uint8_t DEV_HEADER_LEN  = 13;
constexpr uint8_t DEV_TAG_LEN     = 16;
// = EspNowTransport::kMaxFrame: el transporte descarta lo que supere este tamaño.
constexpr uint8_t DEV_MAX_FRAME   = 64;
constexpr uint8_t DEV_MAX_PAYLOAD = DEV_MAX_FRAME - DEV_HEADER_LEN - DEV_TAG_LEN;   // 35

static_assert(DEV_KEY_LEN == crypto::KEY_LEN, "la clave es la de ChaCha20");
static_assert(DEV_TAG_LEN == crypto::TAG_LEN, "la etiqueta es la de Poly1305");

// Ids de dispositivo. Comparten el espacio de numeración con RobotId (ControlProtocol.h):
// 0 = todos, 1..15 = robots, 16 en adelante = dispositivos.
enum class DeviceId : uint8_t {
    All      = DEVICE_ID_ALL,
    Receiver = 16,   // ESP enchufado a la PC: puente ESP-NOW <-> puerto serie
    Console  = 17,   // la consola de teclas, perilla y pantalla
    Pedal    = 18,
    Lights   = 19,   // controlador de la tira RGB (luces-rgb)
    // Emisor de pruebas de luces-rgb. Id propio y no el de la consola: el nonce incluye
    // el origen, y dos emisores con el mismo id y la misma clave pueden repetirlo.
    TestEmitter = 30,
};

// Tipo de mensaje. Los valores viajan por el aire: no se renumeran.
enum class DevMsg : uint8_t {
    LightCommand = 0x10,
    Event        = 0x20,
    InputState   = 0x30,
};

// Cabecera ya decodificada (no es el layout del cable: se serializa byte a byte).
struct DevHeader {
    uint8_t    type;      // DevMsg
    uint8_t    source;
    uint8_t    target;
    DevCounter counter;
};

// ── Cargas ───────────────────────────────────────────────────────────────────

// Comandos de luces. MISMOS valores que CommandType de luces-rgb (lib/core/Command.h).
enum class LightCmd : uint8_t {
    SetColor         = 1,   // r, g, b. Enciende.
    SetBrightness    = 2,   // value = brillo 0..255
    AdjustBrightness = 3,   // value = delta con signo (int8): pasos de un encoder
    Preset           = 4,   // value = índice de preset. Enciende.
    Power            = 5,   // value = 0 apagar, 1 encender, 2 alternar
};
struct LightCommand {       // 5 bytes en el cable
    LightCmd cmd;
    uint8_t  r, g, b;
    uint8_t  value;
};

// Qué le pasó a un control de un dispositivo simple (el pedal, un botón suelto).
enum class EventAction : uint8_t {
    Press   = 1,
    Release = 2,
};
struct Event {              // 2 bytes en el cable
    uint8_t     control;    // cuál control del dispositivo (un pedal doble tiene 1 y 2)
    EventAction action;
};

// Estado completo de la consola: mismos campos que ControlPacket (16 botones, 4 ejes,
// encoder), para las 12 teclas, los faders y la perilla. Es estado, no evento: perder un
// paquete no pierde una tecla, el siguiente trae el estado vigente.
struct InputState {         // 8 bytes en el cable
    uint16_t buttons;       // bit n = tecla n+1
    int8_t   lx, ly, rx, ry;
    uint8_t  nav_flags;     // NAV_CLICK / NAV_LONGPRESS de ControlProtocol.h
    int8_t   nav_delta;
};

// ── Cabecera ─────────────────────────────────────────────────────────────────
inline void writeHeader(const DevHeader& h, uint8_t out[DEV_HEADER_LEN]) {
    out[0] = DEV_MAGIC;
    out[1] = DEV_VERSION;
    out[2] = h.type;
    out[3] = h.source;
    out[4] = h.target;
    crypto::store32(out + 5, h.counter.epoch);
    crypto::store32(out + 9, h.counter.seq);
}

// Lee la cabecera SIN verificar la etiqueta. Sirve para descartar barato lo que no es de
// esta familia o no es para este dispositivo. NO actuar sobre lo que devuelve: hasta que
// openFrame/receiveFrame no den true, la cabecera puede ser falsa.
inline bool decodeHeader(const uint8_t* data, uint8_t len, DevHeader& out) {
    if (len < DEV_HEADER_LEN + DEV_TAG_LEN || len > DEV_MAX_FRAME) return false;
    if (data[0] != DEV_MAGIC || data[1] != DEV_VERSION) return false;
    out.type          = data[2];
    out.source        = data[3];
    out.target        = data[4];
    out.counter.epoch = crypto::load32(data + 5);
    out.counter.seq   = crypto::load32(data + 9);
    return true;
}

// true si el paquete es para este dispositivo (su id) o para todos. Espeja isForRobot.
inline bool isForDevice(const DevHeader& h, uint8_t my_device_id) {
    return h.target == DEVICE_ID_ALL || h.target == my_device_id;
}

// Nonce de 12 bytes = origen | 3 ceros | epoch | seq. Único mientras cada origen no repita
// un contador (ver las reglas de arriba).
inline void makeNonce(uint8_t source, const DevCounter& c, uint8_t nonce[crypto::NONCE_LEN]) {
    nonce[0] = source;
    nonce[1] = 0; nonce[2] = 0; nonce[3] = 0;
    crypto::store32(nonce + 4, c.epoch);
    crypto::store32(nonce + 8, c.seq);
}

// ── Sellar y abrir ───────────────────────────────────────────────────────────

// Arma el paquete cifrado en `out`. Devuelve su largo, o 0 si la carga no entra.
inline uint8_t sealFrame(const uint8_t key[DEV_KEY_LEN], DevMsg type, uint8_t source,
                         uint8_t target, const DevCounter& counter,
                         const uint8_t* payload, uint8_t payload_len,
                         uint8_t* out, uint8_t out_cap) {
    if (payload_len > DEV_MAX_PAYLOAD) return 0;
    const uint8_t total = (uint8_t)(DEV_HEADER_LEN + payload_len + DEV_TAG_LEN);
    if (out_cap < total) return 0;
    const DevHeader h = {static_cast<uint8_t>(type), source, target, counter};
    writeHeader(h, out);
    uint8_t nonce[crypto::NONCE_LEN];
    makeNonce(source, counter, nonce);
    crypto::aeadSeal(key, nonce, out, DEV_HEADER_LEN, payload, payload_len,
                     out + DEV_HEADER_LEN, out + DEV_HEADER_LEN + payload_len);
    return total;
}

// Verifica y descifra. true sólo si el paquete es auténtico; recién entonces escribe la
// cabecera, la carga y su largo. Si devuelve false no toca ninguna salida.
// OJO: no descarta copias ni reenvíos. En un receptor usar receiveFrame().
inline bool openFrame(const uint8_t key[DEV_KEY_LEN], const uint8_t* data, uint8_t len,
                      DevHeader& header, uint8_t* payload, uint8_t payload_cap,
                      uint8_t& payload_len) {
    DevHeader h;
    if (!decodeHeader(data, len, h)) return false;
    const uint8_t n = (uint8_t)(len - DEV_HEADER_LEN - DEV_TAG_LEN);
    if (n > payload_cap) return false;
    uint8_t nonce[crypto::NONCE_LEN];
    makeNonce(h.source, h.counter, nonce);
    if (!crypto::aeadOpen(key, nonce, data, DEV_HEADER_LEN, data + DEV_HEADER_LEN, n,
                          data + DEV_HEADER_LEN + n, payload)) return false;
    header      = h;
    payload_len = n;
    return true;
}

// Lo que usa un receptor: abre el paquete y, sólo si es auténtico, lo pasa por el
// ReplayGuard. true = paquete auténtico y nuevo. El orden importa: anotar el contador
// antes de verificar dejaría que un paquete falso bloquee a los legítimos.
inline bool receiveFrame(const uint8_t key[DEV_KEY_LEN], ReplayGuard& guard,
                         const uint8_t* data, uint8_t len, DevHeader& header,
                         uint8_t* payload, uint8_t payload_cap, uint8_t& payload_len) {
    DevHeader h;
    uint8_t   n = 0;
    uint8_t   tmp[DEV_MAX_PAYLOAD];
    if (!openFrame(key, data, len, h, tmp, sizeof(tmp), n)) return false;
    if (n > payload_cap) return false;
    if (!guard.accept(h.source, h.counter)) return false;
    header      = h;
    payload_len = n;
    if (n) std::memcpy(payload, tmp, n);
    return true;
}

// ── Mensajes tipados ─────────────────────────────────────────────────────────
// sealX arma el paquete; decodeX interpreta la carga YA abierta (por receiveFrame) y
// valida tipo, tamaño y rangos.

inline uint8_t sealLightCommand(const uint8_t key[DEV_KEY_LEN], uint8_t source, uint8_t target,
                                const DevCounter& counter, const LightCommand& m,
                                uint8_t* out, uint8_t out_cap) {
    const uint8_t raw[5] = {static_cast<uint8_t>(m.cmd), m.r, m.g, m.b, m.value};
    return sealFrame(key, DevMsg::LightCommand, source, target, counter, raw, sizeof(raw), out, out_cap);
}
inline bool decodeLightCommand(const DevHeader& h, const uint8_t* payload, uint8_t payload_len,
                               LightCommand& out) {
    if (h.type != static_cast<uint8_t>(DevMsg::LightCommand) || payload_len != 5) return false;
    if (payload[0] < static_cast<uint8_t>(LightCmd::SetColor) ||
        payload[0] > static_cast<uint8_t>(LightCmd::Power)) return false;
    out.cmd   = static_cast<LightCmd>(payload[0]);
    out.r     = payload[1];
    out.g     = payload[2];
    out.b     = payload[3];
    out.value = payload[4];
    return true;
}

inline uint8_t sealEvent(const uint8_t key[DEV_KEY_LEN], uint8_t source, uint8_t target,
                         const DevCounter& counter, const Event& m,
                         uint8_t* out, uint8_t out_cap) {
    const uint8_t raw[2] = {m.control, static_cast<uint8_t>(m.action)};
    return sealFrame(key, DevMsg::Event, source, target, counter, raw, sizeof(raw), out, out_cap);
}
inline bool decodeEvent(const DevHeader& h, const uint8_t* payload, uint8_t payload_len,
                        Event& out) {
    if (h.type != static_cast<uint8_t>(DevMsg::Event) || payload_len != 2) return false;
    if (payload[1] < static_cast<uint8_t>(EventAction::Press) ||
        payload[1] > static_cast<uint8_t>(EventAction::Release)) return false;
    out.control = payload[0];
    out.action  = static_cast<EventAction>(payload[1]);
    return true;
}

inline uint8_t sealInputState(const uint8_t key[DEV_KEY_LEN], uint8_t source, uint8_t target,
                              const DevCounter& counter, const InputState& m,
                              uint8_t* out, uint8_t out_cap) {
    const uint8_t raw[8] = {(uint8_t)(m.buttons & 0xFF), (uint8_t)(m.buttons >> 8),
                            (uint8_t)m.lx, (uint8_t)m.ly, (uint8_t)m.rx, (uint8_t)m.ry,
                            m.nav_flags, (uint8_t)m.nav_delta};
    return sealFrame(key, DevMsg::InputState, source, target, counter, raw, sizeof(raw), out, out_cap);
}
inline bool decodeInputState(const DevHeader& h, const uint8_t* payload, uint8_t payload_len,
                             InputState& out) {
    if (h.type != static_cast<uint8_t>(DevMsg::InputState) || payload_len != 8) return false;
    out.buttons   = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
    out.lx        = (int8_t)payload[2];
    out.ly        = (int8_t)payload[3];
    out.rx        = (int8_t)payload[4];
    out.ry        = (int8_t)payload[5];
    out.nav_flags = payload[6];
    out.nav_delta = (int8_t)payload[7];
    return true;
}

} // namespace input_control
#endif
