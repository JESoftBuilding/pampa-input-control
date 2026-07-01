#include <unity.h>
#include <cstring>
#include <input_control/ControlProtocol.h>

using namespace input_control;

void setUp() {}
void tearDown() {}

// ── Formato del cable ────────────────────────────────────────────────────────
void test_packet_is_11_bytes() {
    TEST_ASSERT_EQUAL_UINT32(11, sizeof(ControlPacket));
}
void test_proto_version_is_2() {
    TEST_ASSERT_EQUAL_UINT8(2, PROTO_VERSION);
}

// ── decode ───────────────────────────────────────────────────────────────────
void test_decode_roundtrip() {
    ControlPacket in{};
    in.version         = PROTO_VERSION;
    in.target_robot_id = (uint8_t)RobotId::PampaV2;
    in.seq = 7; in.lx = 10; in.ly = -20; in.rx = 30; in.ry = -40;
    in.buttons   = (uint16_t)(1u << (uint8_t)Btn::Cross);
    in.nav_flags = NAV_CLICK;
    in.nav_delta = 3;

    uint8_t buf[sizeof(ControlPacket)];
    std::memcpy(buf, &in, sizeof(buf));

    ControlPacket out{};
    TEST_ASSERT_TRUE(decodeControl(buf, sizeof(buf), out));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RobotId::PampaV2, out.target_robot_id);
    TEST_ASSERT_EQUAL_INT8(10, out.lx);
    TEST_ASSERT_EQUAL_INT8(-40, out.ry);
    TEST_ASSERT_EQUAL_INT8(3, out.nav_delta);
    TEST_ASSERT_TRUE(out.buttons & (1u << (uint8_t)Btn::Cross));
}
void test_decode_rejects_v1_size() {
    // 10 bytes = tamaño del ControlPacket v1 → rechazado por len.
    uint8_t buf10[10] = {2,0,0,0,0,0,0,0,0,0};
    ControlPacket out{};
    TEST_ASSERT_FALSE(decodeControl(buf10, sizeof(buf10), out));
}
void test_decode_rejects_wrong_version() {
    uint8_t buf[sizeof(ControlPacket)] = {1};  // version=1 → rechazado
    ControlPacket out{};
    TEST_ASSERT_FALSE(decodeControl(buf, sizeof(buf), out));
}

// ── ruteo target_robot_id ────────────────────────────────────────────────────
void test_isForRobot_broadcast_passes_all() {
    ControlPacket p{}; p.target_robot_id = ROBOT_ID_ALL;
    TEST_ASSERT_TRUE(isForRobot(p, (uint8_t)RobotId::PampaV2));
    TEST_ASSERT_TRUE(isForRobot(p, (uint8_t)RobotId::PampaV3));
}
void test_isForRobot_match_and_mismatch() {
    ControlPacket p{}; p.target_robot_id = (uint8_t)RobotId::PampaV3;
    TEST_ASSERT_TRUE(isForRobot(p, (uint8_t)RobotId::PampaV3));
    TEST_ASSERT_FALSE(isForRobot(p, (uint8_t)RobotId::PampaV2));
}

// ── nav ──────────────────────────────────────────────────────────────────────
void test_extract_nav() {
    ControlPacket p{}; p.nav_flags = NAV_CLICK | NAV_LONGPRESS; p.nav_delta = -2;
    NavInput n = extractNav(p);
    TEST_ASSERT_TRUE(n.click);
    TEST_ASSERT_TRUE(n.long_press);
    TEST_ASSERT_EQUAL_INT8(-2, n.delta);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_packet_is_11_bytes);
    RUN_TEST(test_proto_version_is_2);
    RUN_TEST(test_decode_roundtrip);
    RUN_TEST(test_decode_rejects_v1_size);
    RUN_TEST(test_decode_rejects_wrong_version);
    RUN_TEST(test_isForRobot_broadcast_passes_all);
    RUN_TEST(test_isForRobot_match_and_mismatch);
    RUN_TEST(test_extract_nav);
    return UNITY_END();
}
