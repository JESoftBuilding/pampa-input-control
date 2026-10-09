#include <unity.h>
#include <input_control/SeqFilter.h>

using namespace input_control;

void setUp() {}
void tearDown() {}

void test_first_packet_from_a_source_is_accepted() {
    SeqFilter f;
    TEST_ASSERT_TRUE(f.accept(18, 0, 1000));
}
void test_same_seq_again_inside_the_window_is_a_repeat() {
    SeqFilter f(500);
    TEST_ASSERT_TRUE(f.accept(18, 7, 1000));
    TEST_ASSERT_FALSE(f.accept(18, 7, 1020));
    TEST_ASSERT_FALSE(f.accept(18, 7, 1040));
}
void test_new_seq_from_the_same_source_is_accepted() {
    SeqFilter f(500);
    TEST_ASSERT_TRUE(f.accept(18, 7, 1000));
    TEST_ASSERT_TRUE(f.accept(18, 8, 1010));
}
// Un emisor que duerme (el pedal) puede arrancar siempre con el mismo seq: pasada la
// ventana, el mismo seq es un paquete nuevo y no una repetición.
void test_same_seq_after_the_window_is_a_new_packet() {
    SeqFilter f(500);
    TEST_ASSERT_TRUE(f.accept(18, 0, 1000));
    TEST_ASSERT_FALSE(f.accept(18, 0, 1499));
    TEST_ASSERT_TRUE(f.accept(18, 0, 1500));
}
// La ventana se mide desde el paquete ACEPTADO: las repeticiones no la estiran.
void test_repeats_do_not_extend_the_window() {
    SeqFilter f(500);
    TEST_ASSERT_TRUE(f.accept(18, 3, 1000));
    TEST_ASSERT_FALSE(f.accept(18, 3, 1400));
    TEST_ASSERT_TRUE(f.accept(18, 3, 1600));
}
void test_sources_are_independent() {
    SeqFilter f(500);
    TEST_ASSERT_TRUE(f.accept(17, 5, 1000));
    TEST_ASSERT_TRUE(f.accept(18, 5, 1001));
    TEST_ASSERT_FALSE(f.accept(17, 5, 1002));
    TEST_ASSERT_FALSE(f.accept(18, 5, 1003));
}
void test_millis_wraparound_is_handled() {
    SeqFilter f(500);
    TEST_ASSERT_TRUE(f.accept(18, 9, 0xFFFFFF00u));
    TEST_ASSERT_FALSE(f.accept(18, 9, 0x00000010u));   // pasaron 272 ms
    TEST_ASSERT_TRUE(f.accept(18, 9, 0x00000200u));    // pasaron 768 ms
}
// Con más emisores que lugares se olvida al que hace más tiempo que no habla.
void test_when_full_the_least_recent_source_is_forgotten() {
    SeqFilter f(60000);
    for (uint8_t s = 0; s < SeqFilter::kSources; ++s) {
        TEST_ASSERT_TRUE(f.accept((uint8_t)(100 + s), 1, 1000u + s));
    }
    TEST_ASSERT_TRUE(f.accept(200, 1, 2000));       // entra uno más: desaloja al 100
    TEST_ASSERT_FALSE(f.accept(101, 1, 2001));      // el 101 sigue recordado
    TEST_ASSERT_TRUE(f.accept(100, 1, 2002));       // el 100 fue olvidado: se acepta de nuevo
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_packet_from_a_source_is_accepted);
    RUN_TEST(test_same_seq_again_inside_the_window_is_a_repeat);
    RUN_TEST(test_new_seq_from_the_same_source_is_accepted);
    RUN_TEST(test_same_seq_after_the_window_is_a_new_packet);
    RUN_TEST(test_repeats_do_not_extend_the_window);
    RUN_TEST(test_sources_are_independent);
    RUN_TEST(test_millis_wraparound_is_handled);
    RUN_TEST(test_when_full_the_least_recent_source_is_forgotten);
    return UNITY_END();
}
