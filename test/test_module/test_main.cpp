#include <unity.h>
#include <cstring>
#include <deque>
#include <vector>
#include <input_control/InputControlModule.h>
#include <input_control/ControlProtocol.h>

using namespace input_control;

// Transporte fake: cola FIFO en memoria (imita a EspNowTransport, que entrega el frame
// MÁS VIEJO primero). receive() saca de a uno del frente; data queda apuntando a un buffer
// interno válido hasta el próximo receive().
struct FakeTransport : ILinkTransport {
    std::deque<std::vector<uint8_t>> q;
    std::vector<uint8_t>             hold;   // mantiene vivo el último frame entregado

    void push(const ControlPacket& p) {
        std::vector<uint8_t> f(sizeof(ControlPacket));
        std::memcpy(f.data(), &p, sizeof(ControlPacket));
        q.push_back(std::move(f));
    }

    void begin(uint8_t) override {}
    bool receive(const uint8_t*& data, uint8_t& len) override {
        if (q.empty()) return false;
        hold = q.front(); q.pop_front();
        data = hold.data();
        len  = static_cast<uint8_t>(hold.size());
        return true;
    }
    bool send(const uint8_t*, uint8_t) override { return true; }
    bool isFresh(uint32_t) const override { return !q.empty(); }
    uint32_t dataAgeMs() const override { return 0; }
};

struct NullSink : IEffectSink { void execute(uint8_t) override {} };

static FakeTransport* g_tx;
static NullSink*      g_sink;

void setUp() {
    g_tx   = new FakeTransport();
    g_sink = new NullSink();
}
void tearDown() {
    delete g_tx;   g_tx = nullptr;
    delete g_sink; g_sink = nullptr;
}

static ControlPacket mk(int8_t lx, uint8_t seq, uint8_t target = (uint8_t)RobotId::PampaV2) {
    ControlPacket p{};
    p.version = PROTO_VERSION;
    p.target_robot_id = target;
    p.seq = seq;
    p.lx = lx;
    return p;
}

// El corazón del fix: con VARIOS frames encolados, un solo tick() debe drenar TODO y dejar
// last_ en el MÁS NUEVO (antes se quedaba con el más viejo → lag proporcional a la cola).
void test_tick_drains_to_latest() {
    ModuleConfig cfg{ nullptr, 0, 0, (uint8_t)RobotId::PampaV2 };
    InputControlModule mod(*g_tx, *g_sink, cfg);

    g_tx->push(mk(10, 1));
    g_tx->push(mk(20, 2));
    g_tx->push(mk(30, 3));   // el más nuevo

    mod.tick();

    TEST_ASSERT_EQUAL_INT8(30, mod.lx());          // quedó en el último, no en el primero (10)
    TEST_ASSERT_EQUAL_UINT8(3, mod.lastSeq());
    TEST_ASSERT_TRUE(mod.isConnected());
}

// Soltar el stick: el último frame trae lx=0 → tras drenar, lx()==0 (no arrastra el previo).
void test_release_reflects_immediately() {
    ModuleConfig cfg{ nullptr, 0, 0, (uint8_t)RobotId::PampaV2 };
    InputControlModule mod(*g_tx, *g_sink, cfg);

    g_tx->push(mk(100, 1));
    g_tx->push(mk(100, 2));
    g_tx->push(mk(0,   3));   // soltó
    mod.tick();
    TEST_ASSERT_EQUAL_INT8(0, mod.lx());
}

// Un frame dirigido a OTRO robot en el medio no debe pisar last_ ni cortar el drenado.
void test_foreign_frame_skipped_but_drains() {
    ModuleConfig cfg{ nullptr, 0, 0, (uint8_t)RobotId::PampaV2 };
    InputControlModule mod(*g_tx, *g_sink, cfg);

    g_tx->push(mk(10, 1, (uint8_t)RobotId::PampaV2));
    g_tx->push(mk(99, 2, (uint8_t)RobotId::PampaV3));   // ajeno → se ignora
    g_tx->push(mk(40, 3, (uint8_t)RobotId::PampaV2));   // propio, más nuevo
    mod.tick();
    TEST_ASSERT_EQUAL_INT8(40, mod.lx());
    TEST_ASSERT_EQUAL_UINT8(3, mod.lastSeq());
}

// Sin frames: tick() no rompe y no marca conexión.
void test_empty_tick_noop() {
    ModuleConfig cfg{ nullptr, 0, 0, (uint8_t)RobotId::PampaV2 };
    InputControlModule mod(*g_tx, *g_sink, cfg);
    mod.tick();
    TEST_ASSERT_FALSE(mod.isConnected());
    TEST_ASSERT_EQUAL_INT8(0, mod.lx());
}

// ── Frescura dirigida (spec 2026-07-11 D1) ───────────────────────────────────────────
// Solo los ControlPacket QUE ME APUNTAN refrescan targetedAgeMs; los ajenos no. Esta es la
// señal "seleccionado" del LED (el transporte se refresca con cualquier frame del canal).
void test_targeted_age_tracks_only_my_frames() {
    ModuleConfig cfg{ nullptr, 0, 0, (uint8_t)RobotId::PampaV2 };
    InputControlModule mod(*g_tx, *g_sink, cfg);

    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, mod.targetedAgeMs(5000));   // nunca me apuntaron

    g_tx->push(mk(10, 1, (uint8_t)RobotId::PampaV2));   // para mí
    mod.tick(1000);
    TEST_ASSERT_EQUAL_UINT32(0, mod.targetedAgeMs(1000));
    TEST_ASSERT_TRUE(mod.isTargetedFresh(1200));                     // 200 ms de edad

    g_tx->push(mk(99, 2, (uint8_t)RobotId::PampaV3));   // ajeno: NO refresca
    mod.tick(2000);
    TEST_ASSERT_EQUAL_UINT32(1000, mod.targetedAgeMs(2000));         // sigue anclado en t=1000
    TEST_ASSERT_FALSE(mod.isTargetedFresh(2000));                    // 1000 ms > 300 default
}

// Broadcast (target 0) cuenta como dirigido a mí (isForRobot lo deja pasar).
void test_targeted_age_accepts_broadcast() {
    ModuleConfig cfg{ nullptr, 0, 0, (uint8_t)RobotId::PampaV2 };
    InputControlModule mod(*g_tx, *g_sink, cfg);
    g_tx->push(mk(10, 1, 0));                            // ROBOT_ID_ALL
    mod.tick(500);
    TEST_ASSERT_TRUE(mod.isTargetedFresh(600));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tick_drains_to_latest);
    RUN_TEST(test_release_reflects_immediately);
    RUN_TEST(test_foreign_frame_skipped_but_drains);
    RUN_TEST(test_empty_tick_noop);
    RUN_TEST(test_targeted_age_tracks_only_my_frames);
    RUN_TEST(test_targeted_age_accepts_broadcast);
    return UNITY_END();
}
