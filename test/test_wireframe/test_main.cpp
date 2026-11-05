#include <unity.h>
#include <cstring>
#include <input_control/DeviceProtocol.h>

using namespace input_control;

static uint8_t kKey[DEV_KEY_LEN];

static const uint8_t RECEIVER = (uint8_t)DeviceId::Receiver;
static const uint8_t CONSOLE  = (uint8_t)DeviceId::Console;

void setUp() {
    for (uint8_t i = 0; i < DEV_KEY_LEN; ++i) kKey[i] = (uint8_t)(0x10 + i);
}
void tearDown() {}

// Sella, abre con receiveFrame y decodifica; devuelve false si algún paso falla.
static bool roundtrip(uint8_t inner, const uint8_t* data, uint8_t len, uint8_t& frame_len,
                      DevHeader& h, uint8_t& out_inner, uint8_t* out_data, uint8_t& out_len) {
    uint8_t frame[DEV_MAX_FRAME];
    frame_len = sealWireFrame(kKey, RECEIVER, CONSOLE, DevCounter{3, 41}, inner, data, len,
                              frame, sizeof(frame));
    if (frame_len == 0) return false;
    ReplayGuard guard;
    uint8_t payload[DEV_MAX_PAYLOAD];
    uint8_t n = 0;
    if (!receiveFrame(kKey, guard, frame, frame_len, h, payload, sizeof(payload), n)) return false;
    const uint8_t* d = nullptr;
    if (!decodeWireFrame(h, payload, n, out_inner, d, out_len)) return false;
    if (out_len) std::memcpy(out_data, d, out_len);
    return true;
}

void test_wire_frame_constants() {
    TEST_ASSERT_EQUAL_HEX8(0x50, (uint8_t)DevMsg::WireFrame);
    TEST_ASSERT_EQUAL_UINT8(34, DEV_WIRE_MAX_DATA);
}

void test_roundtrip_with_largest_data_fills_the_frame() {
    uint8_t data[DEV_WIRE_MAX_DATA];
    for (uint8_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(0xC0 + i);
    uint8_t frame_len = 0, inner = 0, out_len = 0;
    uint8_t out[DEV_MAX_PAYLOAD] = {};
    DevHeader h;
    TEST_ASSERT_TRUE(roundtrip(0x46, data, sizeof(data), frame_len, h, inner, out, out_len));
    TEST_ASSERT_EQUAL_UINT8(DEV_MAX_FRAME, frame_len);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)DevMsg::WireFrame, h.type);
    TEST_ASSERT_EQUAL_UINT8(RECEIVER, h.source);
    TEST_ASSERT_EQUAL_UINT8(CONSOLE, h.target);
    TEST_ASSERT_EQUAL_HEX8(0x46, inner);
    TEST_ASSERT_EQUAL_UINT8(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, sizeof(data));
}

void test_roundtrip_with_no_data_carries_only_the_inner_type() {
    uint8_t frame_len = 0, inner = 0, out_len = 99;
    uint8_t out[DEV_MAX_PAYLOAD] = {};
    DevHeader h;
    TEST_ASSERT_TRUE(roundtrip(0x30, nullptr, 0, frame_len, h, inner, out, out_len));
    TEST_ASSERT_EQUAL_UINT8(DEV_HEADER_LEN + 1 + DEV_TAG_LEN, frame_len);
    TEST_ASSERT_EQUAL_HEX8(0x30, inner);
    TEST_ASSERT_EQUAL_UINT8(0, out_len);
}

void test_seal_refuses_more_than_34_bytes_of_data() {
    uint8_t data[DEV_WIRE_MAX_DATA + 1] = {};
    uint8_t frame[DEV_MAX_FRAME + 8];
    TEST_ASSERT_EQUAL_UINT8(0, sealWireFrame(kKey, RECEIVER, CONSOLE, DevCounter{1, 1}, 0x46,
                                             data, sizeof(data), frame, sizeof(frame)));
}

void test_decode_rejects_other_type_and_empty_payload() {
    uint8_t inner = 0, len = 0;
    const uint8_t* d = nullptr;
    const uint8_t light[5] = {1, 255, 0, 0, 0};
    const DevHeader lh = {(uint8_t)DevMsg::LightCommand, CONSOLE, (uint8_t)DeviceId::Lights,
                          DevCounter{1, 1}};
    TEST_ASSERT_FALSE(decodeWireFrame(lh, light, sizeof(light), inner, d, len));
    const DevHeader wh = {(uint8_t)DevMsg::WireFrame, RECEIVER, CONSOLE, DevCounter{1, 1}};
    TEST_ASSERT_FALSE(decodeWireFrame(wh, light, 0, inner, d, len));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_wire_frame_constants);
    RUN_TEST(test_roundtrip_with_largest_data_fills_the_frame);
    RUN_TEST(test_roundtrip_with_no_data_carries_only_the_inner_type);
    RUN_TEST(test_seal_refuses_more_than_34_bytes_of_data);
    RUN_TEST(test_decode_rejects_other_type_and_empty_payload);
    return UNITY_END();
}
