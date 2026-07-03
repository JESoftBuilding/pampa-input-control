#include <unity.h>
#include <cstring>
#include <map>
#include <string>
#include <input_control/ConfigProtocol.h>
#include <input_control/ConfigStore.h>
#include <input_control/ControlProtocol.h>   // PROTO_VERSION (demux byte0)

using namespace input_control;

// Backend en RAM para tests (mockea NVS). Cuenta escrituras → verifica anti-wear.
struct MockBackend : IConfigBackend {
    std::map<std::string, int32_t> kv;
    int writes = 0;
    int32_t getInt(const char* key, int32_t def) override {
        auto it = kv.find(key);
        return it == kv.end() ? def : it->second;
    }
    void putInt(const char* key, int32_t value) override { kv[key] = value; ++writes; }
};

void setUp() {} void tearDown() {}

// ── Layout del protocolo (espeja el que verifica el mando/robot) ──────────────
void test_layout_sizes() {
    TEST_ASSERT_EQUAL_UINT32(5,  sizeof(ConfigItem));
    TEST_ASSERT_EQUAL_UINT32(43, sizeof(ConfigPacket));
    TEST_ASSERT_EQUAL_UINT32(3,  sizeof(ConfigRequest));
}
// ── Demux del cable (spec §3.3) ───────────────────────────────────────────────
// OJO: PROTO_VERSION (byte0 de ControlPacket) == 2 == MSG_CONFIG → COLISIONAN en byte0.
// El robot los separa por TAMAÑO: decodeControl exige len==11, así que un ConfigPacket
// de 43B nunca se confunde con control. Entre los mensajes RAW (los que NO son control),
// el byte0 sí distingue: MSG_CONFIG(43B) / MSG_CONFIG_REQ(3B) / TelemetryRequest(4B).
void test_control_disambiguated_by_size_not_byte0() {
    TEST_ASSERT_EQUAL_UINT8(PROTO_VERSION, MSG_CONFIG);       // colisión de byte0 (documentada)
    TEST_ASSERT_TRUE(sizeof(ControlPacket) != sizeof(ConfigPacket));  // pero el tamaño difiere
}
void test_raw_messages_byte0_distinct() {
    const uint8_t TELEM = 0xA5;
    TEST_ASSERT_TRUE(MSG_CONFIG     != MSG_CONFIG_REQ);
    TEST_ASSERT_TRUE(MSG_CONFIG     != TELEM);
    TEST_ASSERT_TRUE(MSG_CONFIG_REQ != TELEM);
    // y sus tamaños también difieren → (byte0,size) identifica sin ambigüedad
    TEST_ASSERT_TRUE(sizeof(ConfigPacket) != sizeof(ConfigRequest));
}
void test_configrequest_routing() {
    ConfigRequest r{ MSG_CONFIG_REQ, (uint8_t)RobotId::PampaV3, 1 };
    TEST_ASSERT_TRUE(isForRobot(r, (uint8_t)RobotId::PampaV3));
    TEST_ASSERT_FALSE(isForRobot(r, (uint8_t)RobotId::PampaV2));
    r.target_robot_id = ROBOT_ID_ALL;   // broadcast
    TEST_ASSERT_TRUE(isForRobot(r, (uint8_t)RobotId::PampaV2));
}

// ── ConfigStore ──────────────────────────────────────────────────────────────
void test_makekey_format() {
    char k[16]; ConfigStore::makeKey(k, 1, 3);
    TEST_ASSERT_EQUAL_STRING("s001k003", k);
    ConfigStore::makeKey(k, 255, 255);
    TEST_ASSERT_EQUAL_STRING("s255k255", k);   // 8 chars ≤ 15
    TEST_ASSERT_TRUE(std::strlen(k) <= 15);
}
void test_get_returns_default_when_absent() {
    MockBackend b; ConfigStore s(b);
    TEST_ASSERT_EQUAL_INT32(180, s.get(1, 1, 180));   // no guardado → default del caller
}
void test_set_then_get() {
    MockBackend b; ConfigStore s(b);
    s.set(1, 1, 200);
    TEST_ASSERT_EQUAL_INT32(200, s.get(1, 1, 180));   // ignora el default, hay valor
}
void test_set_skips_write_when_unchanged() {
    MockBackend b; ConfigStore s(b);
    s.set(1, 1, 200);
    TEST_ASSERT_EQUAL_INT(1, b.writes);
    s.set(1, 1, 200);                                 // mismo valor → no reescribe (anti-wear)
    TEST_ASSERT_EQUAL_INT(1, b.writes);
    s.set(1, 1, 210);                                 // cambió → sí escribe
    TEST_ASSERT_EQUAL_INT(2, b.writes);
}
void test_keys_are_scoped_by_strategy() {
    MockBackend b; ConfigStore s(b);
    s.set(1, 1, 111);
    s.set(2, 1, 222);                                 // misma key, otra estrategia → clave distinta
    TEST_ASSERT_EQUAL_INT32(111, s.get(1, 1, 0));
    TEST_ASSERT_EQUAL_INT32(222, s.get(2, 1, 0));
}

// applyPacket: persiste + llama al applier por item; persiste keys desconocidas igual.
struct ApplyCapture { int calls = 0; uint8_t last_key = 0; int32_t last_val = 0; };
static void capture(void* ctx, uint8_t, uint8_t key, int32_t val) {
    auto* c = static_cast<ApplyCapture*>(ctx);
    c->calls++; c->last_key = key; c->last_val = val;
}
void test_apply_packet_sets_and_calls_applier() {
    MockBackend b; ConfigStore s(b);
    ConfigPacket p{}; p.msg_type = MSG_CONFIG; p.strategy_id = 1; p.param_count = 2;
    p.items[0] = { 1, 200 };
    p.items[1] = { 99, 7 };                           // key desconocida (forward-compat)
    ApplyCapture cap;
    s.applyPacket(p, capture, &cap);
    TEST_ASSERT_EQUAL_INT(2, cap.calls);
    TEST_ASSERT_EQUAL_INT32(200, s.get(1, 1, 0));     // persistió la conocida
    TEST_ASSERT_EQUAL_INT32(7,   s.get(1, 99, 0));    // y la desconocida también
}
void test_apply_packet_clamps_count() {
    MockBackend b; ConfigStore s(b);
    ConfigPacket p{}; p.strategy_id = 1; p.param_count = 200;   // count corrupto > MAX
    ApplyCapture cap;
    s.applyPacket(p, capture, &cap);
    TEST_ASSERT_EQUAL_INT(CONFIG_MAX_ITEMS, cap.calls);         // no desborda items[8]
}

// snapshotInto: reply robot→mando con valores vigentes o defaults.
void test_snapshot_into_reply() {
    MockBackend b; ConfigStore s(b);
    s.set(1, 1, 200);                                 // solo key1 guardada
    const uint8_t keys[] = { 1, 3 };
    const int32_t defs[] = { 180, 3 };
    ConfigPacket out{};
    uint8_t n = s.snapshotInto(1, keys, defs, 2, out);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_UINT8(MSG_CONFIG, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(1, out.strategy_id);
    TEST_ASSERT_EQUAL_UINT8(1, out.items[0].key); TEST_ASSERT_EQUAL_INT32(200, out.items[0].value);
    TEST_ASSERT_EQUAL_UINT8(3, out.items[1].key); TEST_ASSERT_EQUAL_INT32(3,   out.items[1].value);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_sizes);
    RUN_TEST(test_control_disambiguated_by_size_not_byte0);
    RUN_TEST(test_raw_messages_byte0_distinct);
    RUN_TEST(test_configrequest_routing);
    RUN_TEST(test_makekey_format);
    RUN_TEST(test_get_returns_default_when_absent);
    RUN_TEST(test_set_then_get);
    RUN_TEST(test_set_skips_write_when_unchanged);
    RUN_TEST(test_keys_are_scoped_by_strategy);
    RUN_TEST(test_apply_packet_sets_and_calls_applier);
    RUN_TEST(test_apply_packet_clamps_count);
    RUN_TEST(test_snapshot_into_reply);
    return UNITY_END();
}
