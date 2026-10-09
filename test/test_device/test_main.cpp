#include <unity.h>
#include <cstring>
#include <input_control/ControlProtocol.h>
#include <input_control/ConfigProtocol.h>
#include <input_control/DeviceProtocol.h>

using namespace input_control;

void setUp() {}
void tearDown() {}

// ── Formato del cable ────────────────────────────────────────────────────────
void test_header_is_6_bytes() {
    TEST_ASSERT_EQUAL_UINT32(6, sizeof(DevHeader));
}
void test_light_command_is_11_bytes() {
    TEST_ASSERT_EQUAL_UINT32(11, sizeof(LightCommandPacket));
}
void test_event_is_8_bytes() {
    TEST_ASSERT_EQUAL_UINT32(8, sizeof(EventPacket));
}
void test_magic_and_version() {
    TEST_ASSERT_EQUAL_HEX8(0x4C, DEV_MAGIC);
    TEST_ASSERT_EQUAL_UINT8(1, DEV_VERSION);
}

// ── Cabecera ─────────────────────────────────────────────────────────────────
void test_make_header_fills_every_field() {
    const DevHeader h = makeHeader(DevMsg::Event, (uint8_t)DeviceId::Pedal,
                                   (uint8_t)DeviceId::Receiver, 42);
    TEST_ASSERT_EQUAL_HEX8(DEV_MAGIC, h.magic);
    TEST_ASSERT_EQUAL_UINT8(DEV_VERSION, h.version);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)DevMsg::Event, h.type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)DeviceId::Pedal, h.source);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)DeviceId::Receiver, h.target);
    TEST_ASSERT_EQUAL_UINT8(42, h.seq);
}
void test_decode_header_roundtrip() {
    const DevHeader in = makeHeader(DevMsg::LightCommand, 17, 19, 5);
    uint8_t buf[sizeof(DevHeader)];
    std::memcpy(buf, &in, sizeof(buf));
    DevHeader out{};
    TEST_ASSERT_TRUE(decodeHeader(buf, sizeof(buf), out));
    TEST_ASSERT_EQUAL_UINT8(17, out.source);
    TEST_ASSERT_EQUAL_UINT8(19, out.target);
    TEST_ASSERT_EQUAL_UINT8(5, out.seq);
}
void test_decode_header_rejects_short_frame() {
    const DevHeader in = makeHeader(DevMsg::Event, 18, 16, 1);
    uint8_t buf[sizeof(DevHeader)];
    std::memcpy(buf, &in, sizeof(buf));
    DevHeader out{};
    TEST_ASSERT_FALSE(decodeHeader(buf, sizeof(buf) - 1, out));
}
void test_decode_header_rejects_wrong_magic() {
    DevHeader in = makeHeader(DevMsg::Event, 18, 16, 1);
    in.magic = 0x02;   // primer byte de un ControlPacket
    uint8_t buf[sizeof(DevHeader)];
    std::memcpy(buf, &in, sizeof(buf));
    DevHeader out{};
    TEST_ASSERT_FALSE(decodeHeader(buf, sizeof(buf), out));
}
void test_decode_header_rejects_other_version() {
    DevHeader in = makeHeader(DevMsg::Event, 18, 16, 1);
    in.version = DEV_VERSION + 1;
    uint8_t buf[sizeof(DevHeader)];
    std::memcpy(buf, &in, sizeof(buf));
    DevHeader out{};
    TEST_ASSERT_FALSE(decodeHeader(buf, sizeof(buf), out));
}

// ── Destino ──────────────────────────────────────────────────────────────────
void test_is_for_device_match_mismatch_and_broadcast() {
    const DevHeader toLights = makeHeader(DevMsg::LightCommand, 17, (uint8_t)DeviceId::Lights, 0);
    TEST_ASSERT_TRUE(isForDevice(toLights, (uint8_t)DeviceId::Lights));
    TEST_ASSERT_FALSE(isForDevice(toLights, (uint8_t)DeviceId::Pedal));
    const DevHeader toAll = makeHeader(DevMsg::LightCommand, 17, DEVICE_ID_ALL, 0);
    TEST_ASSERT_TRUE(isForDevice(toAll, (uint8_t)DeviceId::Lights));
    TEST_ASSERT_TRUE(isForDevice(toAll, (uint8_t)DeviceId::Pedal));
}

// ── Comando de luces ─────────────────────────────────────────────────────────
void test_light_command_roundtrip() {
    const LightCommandPacket in = makeLightCommand(
        (uint8_t)DeviceId::Console, (uint8_t)DeviceId::Lights, 9,
        LightCmd::SetColor, 255, 128, 0, 0);
    uint8_t buf[sizeof(LightCommandPacket)];
    std::memcpy(buf, &in, sizeof(buf));
    LightCommandPacket out{};
    TEST_ASSERT_TRUE(decodeLightCommand(buf, sizeof(buf), out));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)LightCmd::SetColor, out.cmd);
    TEST_ASSERT_EQUAL_UINT8(255, out.r);
    TEST_ASSERT_EQUAL_UINT8(128, out.g);
    TEST_ASSERT_EQUAL_UINT8(0, out.b);
    TEST_ASSERT_EQUAL_UINT8(9, out.h.seq);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)DeviceId::Lights, out.h.target);
}
void test_light_command_rejects_wrong_size() {
    const LightCommandPacket in = makeLightCommand(17, 19, 0, LightCmd::Power, 0, 0, 0, 1);
    uint8_t buf[sizeof(LightCommandPacket) + 1] = {};
    std::memcpy(buf, &in, sizeof(in));
    LightCommandPacket out{};
    TEST_ASSERT_FALSE(decodeLightCommand(buf, sizeof(in) - 1, out));
    TEST_ASSERT_FALSE(decodeLightCommand(buf, sizeof(in) + 1, out));
}
void test_light_command_rejects_other_message_type() {
    LightCommandPacket in = makeLightCommand(17, 19, 0, LightCmd::Power, 0, 0, 0, 1);
    in.h.type = (uint8_t)DevMsg::Event;
    uint8_t buf[sizeof(LightCommandPacket)];
    std::memcpy(buf, &in, sizeof(buf));
    LightCommandPacket out{};
    TEST_ASSERT_FALSE(decodeLightCommand(buf, sizeof(buf), out));
}
void test_light_command_rejects_unknown_cmd() {
    LightCommandPacket in = makeLightCommand(17, 19, 0, LightCmd::Power, 0, 0, 0, 1);
    uint8_t buf[sizeof(LightCommandPacket)];
    in.cmd = 0;
    std::memcpy(buf, &in, sizeof(buf));
    LightCommandPacket out{};
    TEST_ASSERT_FALSE(decodeLightCommand(buf, sizeof(buf), out));
    in.cmd = (uint8_t)LightCmd::Power + 1;
    std::memcpy(buf, &in, sizeof(buf));
    TEST_ASSERT_FALSE(decodeLightCommand(buf, sizeof(buf), out));
}
// Los valores viajan por el aire y luces-rgb ya los usa: no se renumeran.
void test_light_cmd_values_are_stable() {
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)LightCmd::SetColor);
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)LightCmd::SetBrightness);
    TEST_ASSERT_EQUAL_UINT8(3, (uint8_t)LightCmd::AdjustBrightness);
    TEST_ASSERT_EQUAL_UINT8(4, (uint8_t)LightCmd::Preset);
    TEST_ASSERT_EQUAL_UINT8(5, (uint8_t)LightCmd::Power);
}

// ── Evento (pedal, botón suelto) ─────────────────────────────────────────────
void test_event_roundtrip() {
    const EventPacket in = makeEvent((uint8_t)DeviceId::Pedal, (uint8_t)DeviceId::Receiver,
                                     3, 1, EventAction::Press);
    uint8_t buf[sizeof(EventPacket)];
    std::memcpy(buf, &in, sizeof(buf));
    EventPacket out{};
    TEST_ASSERT_TRUE(decodeEvent(buf, sizeof(buf), out));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)DeviceId::Pedal, out.h.source);
    TEST_ASSERT_EQUAL_UINT8(1, out.control);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventAction::Press, out.action);
}
void test_event_rejects_unknown_action() {
    EventPacket in = makeEvent(18, 16, 0, 1, EventAction::Release);
    in.action = 0;
    uint8_t buf[sizeof(EventPacket)];
    std::memcpy(buf, &in, sizeof(buf));
    EventPacket out{};
    TEST_ASSERT_FALSE(decodeEvent(buf, sizeof(buf), out));
}
void test_event_rejects_light_command_frame() {
    const LightCommandPacket lc = makeLightCommand(17, 19, 0, LightCmd::Power, 0, 0, 0, 1);
    uint8_t buf[sizeof(LightCommandPacket)];
    std::memcpy(buf, &lc, sizeof(buf));
    EventPacket out{};
    TEST_ASSERT_FALSE(decodeEvent(buf, sizeof(buf), out));
}

// ── Convivencia con el enlace mando↔robot ────────────────────────────────────
// LightCommandPacket mide 11 bytes, igual que ControlPacket: un robot no debe tomarlo
// por control. Lo separa el primer byte.
void test_robot_control_decoder_ignores_light_command() {
    const LightCommandPacket lc = makeLightCommand(17, 19, 0, LightCmd::SetColor, 1, 2, 3, 0);
    TEST_ASSERT_EQUAL_UINT32(sizeof(ControlPacket), sizeof(LightCommandPacket));
    uint8_t buf[sizeof(LightCommandPacket)];
    std::memcpy(buf, &lc, sizeof(buf));
    ControlPacket out{};
    TEST_ASSERT_FALSE(decodeControl(buf, sizeof(buf), out));
}
void test_device_decoder_ignores_control_packet() {
    ControlPacket cp{};
    cp.version = PROTO_VERSION;
    uint8_t buf[sizeof(ControlPacket)];
    std::memcpy(buf, &cp, sizeof(buf));
    DevHeader h{};
    TEST_ASSERT_FALSE(decodeHeader(buf, sizeof(buf), h));
}
// Los ids de dispositivo no pisan los de robot (1..15): la consola emite ControlPacket
// con destino = receptor y ningún robot lo toma como propio.
void test_device_ids_do_not_overlap_robot_ids() {
    TEST_ASSERT_TRUE((uint8_t)DeviceId::Receiver >= 16);
    TEST_ASSERT_TRUE((uint8_t)DeviceId::Console  >= 16);
    TEST_ASSERT_TRUE((uint8_t)DeviceId::Pedal    >= 16);
    TEST_ASSERT_TRUE((uint8_t)DeviceId::Lights   >= 16);
    ControlPacket fromConsole{};
    fromConsole.version = PROTO_VERSION;
    fromConsole.target_robot_id = (uint8_t)DeviceId::Receiver;
    TEST_ASSERT_FALSE(isForRobot(fromConsole, (uint8_t)RobotId::PampaV3));
    TEST_ASSERT_FALSE(isForRobot(fromConsole, (uint8_t)RobotId::PampaV2));
    TEST_ASSERT_FALSE(isForRobot(fromConsole, (uint8_t)RobotId::PampaV2S3));
    TEST_ASSERT_TRUE(isForRobot(fromConsole, (uint8_t)DeviceId::Receiver));
}
void test_room_channel_differs_from_robot_channel() {
    TEST_ASSERT_NOT_EQUAL(1, DEV_CHANNEL);
}

// Registro de lo que ya viaja por el enlace mando<->robot: (tamanio, primer byte).
// Los tres ultimos no estan en esta libreria: viven en PampaController/include/proto/
// telemetry_packet.h y PampaV3/lib/proto. Si se agrega un paquete "suelto" nuevo, va aca.
struct LegacyFrame { uint8_t size; uint8_t byte0; const char* name; };
static const LegacyFrame kLegacy[] = {
    {sizeof(ControlPacket), PROTO_VERSION,  "ControlPacket"},
    {sizeof(ConfigPacket),  MSG_CONFIG,     "ConfigPacket"},
    {sizeof(ConfigRequest), MSG_CONFIG_REQ, "ConfigRequest"},
    {4,  0xA5, "TelemetryRequest"},
    {24, 0x01, "TelemetryPacket"},
    {28, 0x02, "BlackBoxDumpPacket"},
};
void test_dev_magic_is_not_the_first_byte_of_any_legacy_frame() {
    for (const LegacyFrame& f : kLegacy) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(f.byte0, DEV_MAGIC, f.name);
    }
}
void test_legacy_frames_do_not_collide_with_each_other() {
    const int n = (int)(sizeof(kLegacy) / sizeof(kLegacy[0]));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const bool same = kLegacy[i].size == kLegacy[j].size && kLegacy[i].byte0 == kLegacy[j].byte0;
            TEST_ASSERT_FALSE_MESSAGE(same, kLegacy[i].name);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_header_is_6_bytes);
    RUN_TEST(test_light_command_is_11_bytes);
    RUN_TEST(test_event_is_8_bytes);
    RUN_TEST(test_magic_and_version);
    RUN_TEST(test_make_header_fills_every_field);
    RUN_TEST(test_decode_header_roundtrip);
    RUN_TEST(test_decode_header_rejects_short_frame);
    RUN_TEST(test_decode_header_rejects_wrong_magic);
    RUN_TEST(test_decode_header_rejects_other_version);
    RUN_TEST(test_is_for_device_match_mismatch_and_broadcast);
    RUN_TEST(test_light_command_roundtrip);
    RUN_TEST(test_light_command_rejects_wrong_size);
    RUN_TEST(test_light_command_rejects_other_message_type);
    RUN_TEST(test_light_command_rejects_unknown_cmd);
    RUN_TEST(test_light_cmd_values_are_stable);
    RUN_TEST(test_event_roundtrip);
    RUN_TEST(test_event_rejects_unknown_action);
    RUN_TEST(test_event_rejects_light_command_frame);
    RUN_TEST(test_robot_control_decoder_ignores_light_command);
    RUN_TEST(test_device_decoder_ignores_control_packet);
    RUN_TEST(test_device_ids_do_not_overlap_robot_ids);
    RUN_TEST(test_room_channel_differs_from_robot_channel);
    RUN_TEST(test_dev_magic_is_not_the_first_byte_of_any_legacy_frame);
    RUN_TEST(test_legacy_frames_do_not_collide_with_each_other);
    return UNITY_END();
}
