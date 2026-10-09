#include <unity.h>
#include <input_control/DeviceCounter.h>

using namespace input_control;

void setUp() {}
void tearDown() {}

// ── Orden de contadores ──────────────────────────────────────────────────────
void test_newer_compares_epoch_first_then_seq() {
    TEST_ASSERT_TRUE(isNewer(DevCounter{1, 5}, DevCounter{1, 4}));
    TEST_ASSERT_FALSE(isNewer(DevCounter{1, 4}, DevCounter{1, 4}));
    TEST_ASSERT_FALSE(isNewer(DevCounter{1, 3}, DevCounter{1, 4}));
    TEST_ASSERT_TRUE(isNewer(DevCounter{2, 0}, DevCounter{1, 0xFFFFFFFFu}));
    TEST_ASSERT_FALSE(isNewer(DevCounter{1, 0xFFFFFFFFu}, DevCounter{2, 0}));
}

// ── Emisor ───────────────────────────────────────────────────────────────────
void test_tx_counter_starts_at_seq_zero_of_its_epoch() {
    TxCounter tx(7);
    DevCounter c{};
    TEST_ASSERT_TRUE(tx.next(c));
    TEST_ASSERT_EQUAL_UINT32(7, c.epoch);
    TEST_ASSERT_EQUAL_UINT32(0, c.seq);
}
void test_tx_counter_never_repeats() {
    TxCounter tx(3);
    DevCounter prev{}, cur{};
    TEST_ASSERT_TRUE(tx.next(prev));
    for (int i = 0; i < 1000; ++i) {
        TEST_ASSERT_TRUE(tx.next(cur));
        TEST_ASSERT_TRUE(isNewer(cur, prev));
        prev = cur;
    }
}
// Dar la vuelta repetiría un nonce con la misma clave: el contador se agota, no vuelve a 0.
void test_tx_counter_stops_instead_of_wrapping() {
    TxCounter tx(3, 0xFFFFFFFEu);
    DevCounter c{};
    TEST_ASSERT_TRUE(tx.next(c));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFEu, c.seq);
    TEST_ASSERT_TRUE(tx.next(c));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, c.seq);
    TEST_ASSERT_TRUE(tx.exhausted());
    DevCounter untouched{9, 9};
    TEST_ASSERT_FALSE(tx.next(untouched));
    TEST_ASSERT_EQUAL_UINT32(9, untouched.epoch);
    TEST_ASSERT_EQUAL_UINT32(9, untouched.seq);
}

// ── Receptor ─────────────────────────────────────────────────────────────────
void test_first_packet_from_a_source_is_accepted() {
    ReplayGuard g;
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 0}));
}
// El emisor repite cada paquete: las copias llevan el MISMO contador y se descartan.
void test_repeat_of_the_same_counter_is_rejected() {
    ReplayGuard g;
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 10}));
    TEST_ASSERT_FALSE(g.accept(18, DevCounter{1, 10}));
    TEST_ASSERT_FALSE(g.accept(18, DevCounter{1, 10}));
}
// Un paquete grabado y reenviado más tarde lleva un contador viejo.
void test_older_counter_is_rejected_as_replay() {
    ReplayGuard g;
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 10}));
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 11}));
    TEST_ASSERT_FALSE(g.accept(18, DevCounter{1, 10}));
    TEST_ASSERT_FALSE(g.accept(18, DevCounter{1, 3}));
}
void test_new_epoch_restarts_seq_and_is_accepted() {
    ReplayGuard g;
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 500}));
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{2, 0}));     // el emisor reinició
    TEST_ASSERT_FALSE(g.accept(18, DevCounter{1, 999}));  // la época vieja ya no vale
}
void test_sources_are_independent() {
    ReplayGuard g;
    TEST_ASSERT_TRUE(g.accept(17, DevCounter{1, 5}));
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 5}));
    TEST_ASSERT_FALSE(g.accept(17, DevCounter{1, 5}));
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 6}));
}
void test_rejected_packet_does_not_move_the_window() {
    ReplayGuard g;
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 10}));
    TEST_ASSERT_FALSE(g.accept(18, DevCounter{1, 2}));
    TEST_ASSERT_FALSE(g.accept(18, DevCounter{1, 10}));   // sigue recordando el 10, no el 2
    TEST_ASSERT_TRUE(g.accept(18, DevCounter{1, 11}));
}
// Con más emisores que lugares se olvida al que hace más que no se acepta.
void test_when_full_the_least_recently_accepted_source_is_forgotten() {
    ReplayGuard g;
    for (uint8_t s = 0; s < ReplayGuard::kSources; ++s) {
        TEST_ASSERT_TRUE(g.accept((uint8_t)(100 + s), DevCounter{1, 1}));
    }
    TEST_ASSERT_TRUE(g.accept(200, DevCounter{1, 1}));    // entra uno más: desaloja al 100
    TEST_ASSERT_FALSE(g.accept(101, DevCounter{1, 1}));   // el 101 sigue recordado
    TEST_ASSERT_TRUE(g.accept(100, DevCounter{1, 1}));    // el 100 fue olvidado
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_newer_compares_epoch_first_then_seq);
    RUN_TEST(test_tx_counter_starts_at_seq_zero_of_its_epoch);
    RUN_TEST(test_tx_counter_never_repeats);
    RUN_TEST(test_tx_counter_stops_instead_of_wrapping);
    RUN_TEST(test_first_packet_from_a_source_is_accepted);
    RUN_TEST(test_repeat_of_the_same_counter_is_rejected);
    RUN_TEST(test_older_counter_is_rejected_as_replay);
    RUN_TEST(test_new_epoch_restarts_seq_and_is_accepted);
    RUN_TEST(test_sources_are_independent);
    RUN_TEST(test_rejected_packet_does_not_move_the_window);
    RUN_TEST(test_when_full_the_least_recently_accepted_source_is_forgotten);
    return UNITY_END();
}
