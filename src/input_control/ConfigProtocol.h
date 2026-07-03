#ifndef INPUT_CONTROL_CONFIG_PROTOCOL_H
#define INPUT_CONTROL_CONFIG_PROTOCOL_H
#include <cstdint>

// Protocolo de configuración mando <-> robot. FUENTE ÚNICA en la lib compartida
// (antes vivía SOLO en PampaController/include/proto/config_packet.h). El mando y los
// robots (V2/V3) incluyen este MISMO header; el header viejo del mando queda como shim
// (mismo patrón que control_packet.h → ControlProtocol.h).
//
// Demux en el robot por (byte0 + TAMAÑO) — spec §3.3:
//   byte0=PROTO_VERSION(=2)  ControlPacket    (11B) — lo demuxea decodeControl (exige len==11)
//   byte0=MSG_CONFIG(=0x02)  ConfigPacket     (43B) — config rica mando→robot / reply robot→mando
//   byte0=MSG_CONFIG_REQ(3)  ConfigRequest    (3B)  — "dame tus valores vigentes" al ENTRAR a Config
//   byte0=TELEM_REQ_MAGIC(0xA5) TelemetryRequest(4B) — dump de caja negra
// ⚠️ PROTO_VERSION == MSG_CONFIG == 2 → ControlPacket y ConfigPacket COLISIONAN en byte0.
// Se separan por TAMAÑO: decodeControl solo acepta len==11, así que el ConfigPacket de 43B
// cae al handler raw. Entre los mensajes raw (config/config-req/telem), el byte0 sí distingue.
namespace input_control {

constexpr uint8_t MSG_CONFIG         = 0x02;   // ConfigPacket (config rica y reply/ACK)
constexpr uint8_t MSG_CONFIG_REQ     = 0x03;   // ConfigRequest (pedido de valores vigentes)
constexpr uint8_t CONFIG_SETTINGS_ID = 0xFF;   // strategy_id reservado = Ajustes globales
constexpr uint8_t CONFIG_MAX_ITEMS   = 8;

#pragma pack(push, 1)
struct ConfigItem { uint8_t key; int32_t value; };   // 5 bytes
// Config rica mando→robot; el MISMO layout se usa robot→mando como reply/ACK (§4.4).
struct ConfigPacket {
    uint8_t    msg_type;       // = MSG_CONFIG
    uint8_t    strategy_id;    // a qué brain aplica (0xFF = Ajustes)
    uint8_t    param_count;
    ConfigItem items[CONFIG_MAX_ITEMS];
};                                                    // = 3 + 8*5 = 43 bytes
// Pedido mando→robot al ENTRAR a Config: "reportá tus valores vigentes de este catálogo".
struct ConfigRequest {
    uint8_t msg_type;          // = MSG_CONFIG_REQ
    uint8_t target_robot_id;   // RobotId — los demás robots lo ignoran
    uint8_t strategy_id;       // qué catálogo (0xFF = Ajustes)
};                                                    // = 3 bytes
#pragma pack(pop)

static_assert(sizeof(ConfigItem)    == 5,  "ConfigItem debe ser 5 bytes (pack 1)");
static_assert(sizeof(ConfigPacket)  == 43, "ConfigPacket debe ser 43 bytes (pack 1)");
static_assert(sizeof(ConfigRequest) == 3,  "ConfigRequest debe ser 3 bytes (pack 1)");

// true si el ConfigRequest es para este robot (o broadcast). Espeja isForRobot(ControlPacket).
inline bool isForRobot(const ConfigRequest& r, uint8_t my_robot_id) {
    return r.target_robot_id == 0 /*ROBOT_ID_ALL*/ || r.target_robot_id == my_robot_id;
}

} // namespace input_control
#endif
