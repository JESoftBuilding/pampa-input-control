#include <unity.h>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include <input_control/ControlProtocol.h>
#include <input_control/ConfigProtocol.h>
#include <input_control/DeviceProtocol.h>

using namespace input_control;

static uint8_t kKey[DEV_KEY_LEN];
static uint8_t kOtherKey[DEV_KEY_LEN];

static const uint8_t RECEIVER = (uint8_t)DeviceId::Receiver;
static const uint8_t CONSOLE  = (uint8_t)DeviceId::Console;
static const uint8_t PEDAL    = (uint8_t)DeviceId::Pedal;
static const uint8_t LIGHTS   = (uint8_t)DeviceId::Lights;

void setUp() {
    for (uint8_t i = 0; i < DEV_KEY_LEN; ++i) {
        kKey[i]      = (uint8_t)(0x10 + i);
        kOtherKey[i] = (uint8_t)(0xA0 + i);
    }
}
void tearDown() {}

static const LightCommand kOrange = {LightCmd::SetColor, 255, 128, 7, 0};

static uint8_t sealOrange(uint8_t* out, uint8_t cap, DevCounter c = DevCounter{1, 9},
                          uint8_t source = CONSOLE, const uint8_t* key = kKey) {
    return sealLightCommand(key, source, LIGHTS, c, kOrange, out, cap);
}

// ── Formato del cable ────────────────────────────────────────────────────────
void test_wire_constants() {
    TEST_ASSERT_EQUAL_HEX8(0x4C, DEV_MAGIC);
    TEST_ASSERT_EQUAL_UINT8(2, DEV_VERSION);
    TEST_ASSERT_EQUAL_UINT8(13, DEV_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT8(16, DEV_TAG_LEN);
    TEST_ASSERT_EQUAL_UINT8(64, DEV_MAX_FRAME);
    TEST_ASSERT_EQUAL_UINT8(35, DEV_MAX_PAYLOAD);
}
void test_frame_sizes() {
    uint8_t buf[DEV_MAX_FRAME];
    TEST_ASSERT_EQUAL_UINT8(13 + 5 + 16, sealOrange(buf, sizeof(buf)));
    const Event ev = {1, EventAction::Press};
    TEST_ASSERT_EQUAL_UINT8(13 + 2 + 16, sealEvent(kKey, PEDAL, RECEIVER, DevCounter{1, 0}, ev, buf, sizeof(buf)));
    const InputState in = {};
    TEST_ASSERT_EQUAL_UINT8(13 + 8 + 16, sealInputState(kKey, CONSOLE, RECEIVER, DevCounter{1, 0}, in, buf, sizeof(buf)));
}
// La cabecera viaja en claro, en este orden, con los números en little-endian.
void test_header_travels_in_clear_in_the_documented_order() {
    uint8_t buf[DEV_MAX_FRAME];
    sealOrange(buf, sizeof(buf), DevCounter{0x01020304u, 0x0A0B0C0Du});
    const uint8_t expected[13] = {0x4C, 2, 0x10, 17, 19, 0x04, 0x03, 0x02, 0x01, 0x0D, 0x0C, 0x0B, 0x0A};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 13);
}
void test_decode_header_reads_every_field() {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf), DevCounter{77, 1234});
    DevHeader h{};
    TEST_ASSERT_TRUE(decodeHeader(buf, n, h));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)DevMsg::LightCommand, h.type);
    TEST_ASSERT_EQUAL_UINT8(CONSOLE, h.source);
    TEST_ASSERT_EQUAL_UINT8(LIGHTS, h.target);
    TEST_ASSERT_EQUAL_UINT32(77, h.counter.epoch);
    TEST_ASSERT_EQUAL_UINT32(1234, h.counter.seq);
}
void test_decode_header_rejects_short_frame_wrong_magic_and_other_version() {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf));
    DevHeader h{};
    TEST_ASSERT_FALSE(decodeHeader(buf, DEV_HEADER_LEN + DEV_TAG_LEN - 1, h));   // ni lugar para la etiqueta
    buf[0] = 0x02;
    TEST_ASSERT_FALSE(decodeHeader(buf, n, h));
    buf[0] = DEV_MAGIC; buf[1] = 1;   // la versión 1 fue el borrador sin cifrar
    TEST_ASSERT_FALSE(decodeHeader(buf, n, h));
}
void test_is_for_device_match_mismatch_and_broadcast() {
    DevHeader h{};
    h.target = LIGHTS;
    TEST_ASSERT_TRUE(isForDevice(h, LIGHTS));
    TEST_ASSERT_FALSE(isForDevice(h, PEDAL));
    h.target = DEVICE_ID_ALL;
    TEST_ASSERT_TRUE(isForDevice(h, LIGHTS));
    TEST_ASSERT_TRUE(isForDevice(h, PEDAL));
}

// Paquete calculado con OTRA implementación (Python `cryptography` 50.0.1,
// ChaCha20Poly1305), con la misma clave, cabecera como datos asociados y el nonce
// origen|000|epoch|seq. Fija el formato del cable: si cambia un byte, falla.
void test_sealed_frame_matches_an_independent_implementation() {
    const uint8_t expected[34] = {
        0x4c, 0x02, 0x10, 0x11, 0x13, 0x04, 0x03, 0x02, 0x01, 0x0d, 0x0c, 0x0b, 0x0a,
        0xf7, 0xf1, 0x8e, 0x63, 0x52,
        0x2f, 0xc0, 0xf5, 0xdf, 0xad, 0x35, 0x2b, 0xb6, 0x37, 0xa6, 0x3b, 0xe3, 0x62, 0x0f, 0xff, 0x5f};
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf), DevCounter{0x01020304u, 0x0A0B0C0Du});
    TEST_ASSERT_EQUAL_UINT8(34, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 34);
}

// ── Sellar y abrir ───────────────────────────────────────────────────────────
void test_open_recovers_header_and_payload() {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf), DevCounter{3, 40});
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
    TEST_ASSERT_EQUAL_UINT8(5, plen);
    TEST_ASSERT_EQUAL_UINT8(CONSOLE, h.source);
    TEST_ASSERT_EQUAL_UINT32(40, h.counter.seq);
    LightCommand out{};
    TEST_ASSERT_TRUE(decodeLightCommand(h, payload, plen, out));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)LightCmd::SetColor, (uint8_t)out.cmd);
    TEST_ASSERT_EQUAL_UINT8(255, out.r);
    TEST_ASSERT_EQUAL_UINT8(128, out.g);
    TEST_ASSERT_EQUAL_UINT8(7, out.b);
}
// Lo que se manda no es el comando: quien escucha no ve el color.
void test_payload_does_not_travel_in_clear() {
    uint8_t buf[DEV_MAX_FRAME];
    sealOrange(buf, sizeof(buf));
    const uint8_t plain[5] = {(uint8_t)LightCmd::SetColor, 255, 128, 7, 0};
    TEST_ASSERT_NOT_EQUAL(0, std::memcmp(buf + DEV_HEADER_LEN, plain, 5));
}
// El mismo comando no produce nunca los mismos bytes: cambia con el contador y el origen.
void test_same_payload_encrypts_differently_per_counter_and_source() {
    uint8_t a[DEV_MAX_FRAME], b[DEV_MAX_FRAME], c[DEV_MAX_FRAME], d[DEV_MAX_FRAME];
    sealOrange(a, sizeof(a), DevCounter{1, 9});
    sealOrange(b, sizeof(b), DevCounter{1, 10});
    sealOrange(c, sizeof(c), DevCounter{2, 9});
    sealOrange(d, sizeof(d), DevCounter{1, 9}, PEDAL);
    TEST_ASSERT_NOT_EQUAL(0, std::memcmp(a + DEV_HEADER_LEN, b + DEV_HEADER_LEN, 5));
    TEST_ASSERT_NOT_EQUAL(0, std::memcmp(a + DEV_HEADER_LEN, c + DEV_HEADER_LEN, 5));
    TEST_ASSERT_NOT_EQUAL(0, std::memcmp(a + DEV_HEADER_LEN, d + DEV_HEADER_LEN, 5));
}
void test_open_rejects_another_key() {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf));
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_FALSE(openFrame(kOtherKey, buf, n, h, payload, sizeof(payload), plen));
}
// Cambiar UN bit en cualquier lugar (cabecera, carga cifrada o etiqueta) invalida el paquete.
void test_open_rejects_any_single_bit_change() {
    uint8_t good[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(good, sizeof(good));
    for (uint8_t i = 0; i < n; ++i) {
        for (uint8_t bit : {(uint8_t)0x01, (uint8_t)0x80}) {
            uint8_t bad[DEV_MAX_FRAME];
            std::memcpy(bad, good, n);
            bad[i] ^= bit;
            DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
            char msg[32];
            snprintf(msg, sizeof(msg), "byte %u bit 0x%02X", (unsigned)i, (unsigned)bit);
            TEST_ASSERT_FALSE_MESSAGE(openFrame(kKey, bad, n, h, payload, sizeof(payload), plen), msg);
        }
    }
}
void test_open_rejects_truncated_and_extended_frames() {
    uint8_t buf[DEV_MAX_FRAME] = {};
    const uint8_t n = sealOrange(buf, sizeof(buf));
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_FALSE(openFrame(kKey, buf, n - 1, h, payload, sizeof(payload), plen));
    TEST_ASSERT_FALSE(openFrame(kKey, buf, n + 1, h, payload, sizeof(payload), plen));
}
void test_open_failure_leaves_outputs_untouched() {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf));
    DevHeader h{}; h.source = 0xEE;
    uint8_t payload[DEV_MAX_PAYLOAD]; std::memset(payload, 0xEE, sizeof(payload));
    uint8_t plen = 0xEE;
    TEST_ASSERT_FALSE(openFrame(kOtherKey, buf, n, h, payload, sizeof(payload), plen));
    TEST_ASSERT_EQUAL_HEX8(0xEE, h.source);
    TEST_ASSERT_EQUAL_HEX8(0xEE, plen);
    for (uint8_t b : payload) TEST_ASSERT_EQUAL_HEX8(0xEE, b);
}
void test_open_rejects_when_payload_buffer_is_too_small() {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf));
    DevHeader h{}; uint8_t small[4]; uint8_t plen = 0;
    TEST_ASSERT_FALSE(openFrame(kKey, buf, n, h, small, sizeof(small), plen));
}

// ── Límites de sellado ───────────────────────────────────────────────────────
void test_largest_payload_makes_a_frame_that_fits_the_transport() {
    uint8_t payload[DEV_MAX_PAYLOAD] = {};
    uint8_t buf[DEV_MAX_FRAME];
    TEST_ASSERT_EQUAL_UINT8(DEV_MAX_FRAME, sealFrame(kKey, DevMsg::Event, PEDAL, RECEIVER, DevCounter{1, 1},
                                                    payload, DEV_MAX_PAYLOAD, buf, sizeof(buf)));
}
void test_seal_refuses_oversized_payload_and_small_output() {
    uint8_t payload[DEV_MAX_PAYLOAD + 1] = {};
    uint8_t big[100];
    TEST_ASSERT_EQUAL_UINT8(0, sealFrame(kKey, DevMsg::Event, PEDAL, RECEIVER, DevCounter{1, 1},
                                         payload, DEV_MAX_PAYLOAD + 1, big, sizeof(big)));
    uint8_t small[20];
    TEST_ASSERT_EQUAL_UINT8(0, sealOrange(small, sizeof(small)));
}

// ── Comando de luces ─────────────────────────────────────────────────────────
static uint8_t sealRawLight(uint8_t cmd_byte, uint8_t* out, uint8_t cap) {
    const uint8_t raw[5] = {cmd_byte, 1, 2, 3, 4};
    return sealFrame(kKey, DevMsg::LightCommand, CONSOLE, LIGHTS, DevCounter{1, 1}, raw, 5, out, cap);
}
void test_light_command_rejects_unknown_cmd() {
    for (uint8_t bad : {(uint8_t)0, (uint8_t)((uint8_t)LightCmd::Power + 1)}) {
        uint8_t buf[DEV_MAX_FRAME];
        const uint8_t n = sealRawLight(bad, buf, sizeof(buf));
        DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
        TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
        LightCommand out{};
        TEST_ASSERT_FALSE(decodeLightCommand(h, payload, plen, out));
    }
}
void test_light_command_rejects_other_message_type_and_wrong_size() {
    uint8_t buf[DEV_MAX_FRAME];
    const Event ev = {1, EventAction::Press};
    uint8_t n = sealEvent(kKey, PEDAL, RECEIVER, DevCounter{1, 1}, ev, buf, sizeof(buf));
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
    LightCommand out{};
    TEST_ASSERT_FALSE(decodeLightCommand(h, payload, plen, out));

    const uint8_t six[6] = {(uint8_t)LightCmd::Power, 0, 0, 0, 1, 0};
    n = sealFrame(kKey, DevMsg::LightCommand, CONSOLE, LIGHTS, DevCounter{1, 2}, six, 6, buf, sizeof(buf));
    TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
    TEST_ASSERT_FALSE(decodeLightCommand(h, payload, plen, out));
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
    uint8_t buf[DEV_MAX_FRAME];
    const Event in = {2, EventAction::Release};
    const uint8_t n = sealEvent(kKey, PEDAL, RECEIVER, DevCounter{5, 0}, in, buf, sizeof(buf));
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
    Event out{};
    TEST_ASSERT_TRUE(decodeEvent(h, payload, plen, out));
    TEST_ASSERT_EQUAL_UINT8(PEDAL, h.source);
    TEST_ASSERT_EQUAL_UINT8(2, out.control);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventAction::Release, (uint8_t)out.action);
}
void test_event_rejects_unknown_action() {
    const uint8_t raw[2] = {1, 0};
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealFrame(kKey, DevMsg::Event, PEDAL, RECEIVER, DevCounter{1, 1}, raw, 2, buf, sizeof(buf));
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
    Event out{};
    TEST_ASSERT_FALSE(decodeEvent(h, payload, plen, out));
}

// ── Estado de la consola (teclas, faders, perilla) ───────────────────────────
void test_input_state_roundtrip() {
    InputState in{};
    in.buttons = (uint16_t)((1u << 11) | (1u << 0) | 0x0A50);   // tecla 12, tecla 1 y otras
    in.lx = -128; in.ly = 127; in.rx = -1; in.ry = 0;
    in.nav_flags = NAV_CLICK | NAV_LONGPRESS;
    in.nav_delta = -3;
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealInputState(kKey, CONSOLE, RECEIVER, DevCounter{9, 77}, in, buf, sizeof(buf));
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
    InputState out{};
    TEST_ASSERT_TRUE(decodeInputState(h, payload, plen, out));
    TEST_ASSERT_EQUAL_HEX16(in.buttons, out.buttons);
    TEST_ASSERT_EQUAL_INT8(-128, out.lx);
    TEST_ASSERT_EQUAL_INT8(127, out.ly);
    TEST_ASSERT_EQUAL_INT8(-1, out.rx);
    TEST_ASSERT_EQUAL_INT8(0, out.ry);
    TEST_ASSERT_EQUAL_UINT8(NAV_CLICK | NAV_LONGPRESS, out.nav_flags);
    TEST_ASSERT_EQUAL_INT8(-3, out.nav_delta);
}
void test_input_state_rejects_other_message_type() {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf));
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    TEST_ASSERT_TRUE(openFrame(kKey, buf, n, h, payload, sizeof(payload), plen));
    InputState out{};
    TEST_ASSERT_FALSE(decodeInputState(h, payload, plen, out));
}

// Una carga del tamaño justo pero de OTRO tipo de mensaje no se interpreta: cada
// decodificador mira el tipo, no sólo el largo.
static bool openRaw(DevMsg type, const uint8_t* raw, uint8_t raw_len, DevHeader& h,
                    uint8_t* payload, uint8_t& plen) {
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealFrame(kKey, type, CONSOLE, RECEIVER, DevCounter{1, 1}, raw, raw_len, buf, sizeof(buf));
    return openFrame(kKey, buf, n, h, payload, DEV_MAX_PAYLOAD, plen);
}
void test_decoders_check_the_message_type_not_only_the_size() {
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;

    const uint8_t five[5] = {(uint8_t)LightCmd::SetColor, 1, 2, 3, 0};   // válida como comando de luces
    TEST_ASSERT_TRUE(openRaw(DevMsg::Event, five, 5, h, payload, plen));
    LightCommand lc{};
    TEST_ASSERT_FALSE(decodeLightCommand(h, payload, plen, lc));

    const uint8_t two[2] = {1, (uint8_t)EventAction::Press};             // válida como evento
    TEST_ASSERT_TRUE(openRaw(DevMsg::LightCommand, two, 2, h, payload, plen));
    Event ev{};
    TEST_ASSERT_FALSE(decodeEvent(h, payload, plen, ev));

    const uint8_t eight[8] = {0};                                        // válida como estado
    TEST_ASSERT_TRUE(openRaw(DevMsg::Event, eight, 8, h, payload, plen));
    InputState in{};
    TEST_ASSERT_FALSE(decodeInputState(h, payload, plen, in));
}

// ── Copias y reenvíos ────────────────────────────────────────────────────────
static bool receive(ReplayGuard& g, const uint8_t* buf, uint8_t n) {
    DevHeader h{}; uint8_t payload[DEV_MAX_PAYLOAD]; uint8_t plen = 0;
    return receiveFrame(kKey, g, buf, n, h, payload, sizeof(payload), plen);
}
// El emisor manda el mismo paquete varias veces: se procesa una sola.
void test_receive_accepts_a_frame_once() {
    ReplayGuard g;
    uint8_t buf[DEV_MAX_FRAME];
    const uint8_t n = sealOrange(buf, sizeof(buf), DevCounter{1, 5});
    TEST_ASSERT_TRUE(receive(g, buf, n));
    TEST_ASSERT_FALSE(receive(g, buf, n));
    TEST_ASSERT_FALSE(receive(g, buf, n));
}
// Un paquete válido grabado y vuelto a emitir más tarde no se acepta.
void test_receive_rejects_a_recorded_frame_sent_again_later() {
    ReplayGuard g;
    uint8_t first[DEV_MAX_FRAME], second[DEV_MAX_FRAME];
    const uint8_t n1 = sealOrange(first, sizeof(first), DevCounter{1, 5});
    const uint8_t n2 = sealOrange(second, sizeof(second), DevCounter{1, 6});
    TEST_ASSERT_TRUE(receive(g, first, n1));
    TEST_ASSERT_TRUE(receive(g, second, n2));
    TEST_ASSERT_FALSE(receive(g, first, n1));
}
// Un paquete falso con un contador altísimo no puede dejar afuera a los legítimos:
// el contador sólo se anota después de verificar la etiqueta.
void test_forged_frame_does_not_block_legitimate_ones() {
    ReplayGuard g;
    uint8_t forged[DEV_MAX_FRAME], real[DEV_MAX_FRAME];
    const uint8_t nf = sealOrange(forged, sizeof(forged), DevCounter{999999, 0}, CONSOLE, kOtherKey);
    const uint8_t nr = sealOrange(real, sizeof(real), DevCounter{1, 5});
    TEST_ASSERT_FALSE(receive(g, forged, nf));
    TEST_ASSERT_TRUE(receive(g, real, nr));
}

// ── Convivencia con el enlace mando↔robot ────────────────────────────────────
void test_robot_control_decoder_ignores_device_frames() {
    uint8_t buf[DEV_MAX_FRAME];
    ControlPacket out{};
    uint8_t n = sealOrange(buf, sizeof(buf));
    TEST_ASSERT_FALSE(decodeControl(buf, n, out));
    // Aun con el tamaño exacto de un ControlPacket, el primer byte lo separa.
    TEST_ASSERT_FALSE(decodeControl(buf, sizeof(ControlPacket), out));
}
void test_device_decoder_ignores_control_packet() {
    uint8_t buf[DEV_MAX_FRAME] = {};
    ControlPacket cp{};
    cp.version = PROTO_VERSION;
    std::memcpy(buf, &cp, sizeof(cp));
    DevHeader h{};
    TEST_ASSERT_FALSE(decodeHeader(buf, DEV_MAX_FRAME, h));
}
// Los ids de dispositivo no pisan los de robot (1..15).
void test_device_ids_do_not_overlap_robot_ids() {
    for (uint8_t id : {RECEIVER, CONSOLE, PEDAL, LIGHTS, (uint8_t)DeviceId::TestEmitter}) TEST_ASSERT_TRUE(id >= 16);
    TEST_ASSERT_TRUE((uint8_t)RobotId::PampaV3 < 16);
    TEST_ASSERT_TRUE((uint8_t)RobotId::PampaV2 < 16);
    TEST_ASSERT_TRUE((uint8_t)RobotId::PampaV2S3 < 16);
}
void test_room_channel_differs_from_robot_channel() {
    TEST_ASSERT_NOT_EQUAL(1, DEV_CHANNEL);
}
// Registro de lo que ya viaja por el enlace mando<->robot: (tamaño, primer byte).
// Los tres últimos no están en esta librería: viven en PampaController/include/proto/
// telemetry_packet.h y PampaV3/lib/proto. Si se agrega un paquete "suelto" nuevo, va acá.
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
    RUN_TEST(test_wire_constants);
    RUN_TEST(test_frame_sizes);
    RUN_TEST(test_header_travels_in_clear_in_the_documented_order);
    RUN_TEST(test_decode_header_reads_every_field);
    RUN_TEST(test_decode_header_rejects_short_frame_wrong_magic_and_other_version);
    RUN_TEST(test_is_for_device_match_mismatch_and_broadcast);
    RUN_TEST(test_sealed_frame_matches_an_independent_implementation);
    RUN_TEST(test_open_recovers_header_and_payload);
    RUN_TEST(test_payload_does_not_travel_in_clear);
    RUN_TEST(test_same_payload_encrypts_differently_per_counter_and_source);
    RUN_TEST(test_open_rejects_another_key);
    RUN_TEST(test_open_rejects_any_single_bit_change);
    RUN_TEST(test_open_rejects_truncated_and_extended_frames);
    RUN_TEST(test_open_failure_leaves_outputs_untouched);
    RUN_TEST(test_open_rejects_when_payload_buffer_is_too_small);
    RUN_TEST(test_largest_payload_makes_a_frame_that_fits_the_transport);
    RUN_TEST(test_seal_refuses_oversized_payload_and_small_output);
    RUN_TEST(test_light_command_rejects_unknown_cmd);
    RUN_TEST(test_light_command_rejects_other_message_type_and_wrong_size);
    RUN_TEST(test_light_cmd_values_are_stable);
    RUN_TEST(test_event_roundtrip);
    RUN_TEST(test_event_rejects_unknown_action);
    RUN_TEST(test_input_state_roundtrip);
    RUN_TEST(test_input_state_rejects_other_message_type);
    RUN_TEST(test_decoders_check_the_message_type_not_only_the_size);
    RUN_TEST(test_receive_accepts_a_frame_once);
    RUN_TEST(test_receive_rejects_a_recorded_frame_sent_again_later);
    RUN_TEST(test_forged_frame_does_not_block_legitimate_ones);
    RUN_TEST(test_robot_control_decoder_ignores_device_frames);
    RUN_TEST(test_device_decoder_ignores_control_packet);
    RUN_TEST(test_device_ids_do_not_overlap_robot_ids);
    RUN_TEST(test_room_channel_differs_from_robot_channel);
    RUN_TEST(test_dev_magic_is_not_the_first_byte_of_any_legacy_frame);
    RUN_TEST(test_legacy_frames_do_not_collide_with_each_other);
    return UNITY_END();
}
