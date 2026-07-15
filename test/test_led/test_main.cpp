#include <unity.h>
#include <input_control/led/LedModel.h>
#include <input_control/led/LedAnimator.h>
#include <input_control/led/LedTestModel.h>

// Tabla semántica del LED unificado (spec 2026-07-11 §3): escalera de prioridad,
// patrones por actividad y overlay de batería. UNA fuente de verdad para todos los robots.
using namespace input_control::led;

void setUp() {}
void tearDown() {}

namespace { constexpr Rgb kAccent{ 0, 200, 180 }; }   // teal (V2-DVK) como acento de prueba

static bool sameColor(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// Prio 1: e-stop tapa todo (incluso OTA/identify/batería) — rojo sólido.
void test_estop_wins_over_everything() {
    LedInputs s; s.estop = true; s.ota = true; s.identify = true; s.low_battery = true;
    s.link_fresh = true; s.selected_fresh = true;
    LedOutput o = resolve(s, kAccent);
    TEST_ASSERT_TRUE(sameColor(o.color, RED));
    TEST_ASSERT_TRUE(o.pattern == Pattern::Solid);
    TEST_ASSERT_FALSE(o.batt_overlay);          // en e-stop el rojo ya lo dice todo
}

// Prio 4/5: el fix del reporte — mando en el aire pero NO seleccionado = standby cyan,
// NUNCA el color de "controlado". Sin mando = azul respirando.
void test_link_vs_selected_are_different_states() {
    LedInputs s; s.link_fresh = false;
    LedOutput o = resolve(s, kAccent);
    TEST_ASSERT_TRUE(sameColor(o.color, BLUE));         // sin mando
    TEST_ASSERT_TRUE(o.pattern == Pattern::Breathe);

    s.link_fresh = true; s.selected_fresh = false;      // mando prendido, controla a OTRO
    o = resolve(s, kAccent);
    TEST_ASSERT_TRUE(sameColor(o.color, CYAN));         // standby — no acento, no verde
    TEST_ASSERT_TRUE(o.pattern == Pattern::Breathe);

    s.selected_fresh = true;                            // ahora sí me controla a mí
    o = resolve(s, kAccent);
    TEST_ASSERT_TRUE(sameColor(o.color, kAccent));      // acento del robot (D2)
    TEST_ASSERT_TRUE(o.pattern == Pattern::Solid);      // Manual
}

// Seleccionado: patrón por actividad (Hold latido, Orbit 1 Hz, Test violeta rápido).
void test_activity_patterns() {
    LedInputs s; s.link_fresh = true; s.selected_fresh = true;
    s.activity = Activity::Hold;
    TEST_ASSERT_TRUE(resolve(s, kAccent).pattern == Pattern::DoubleFlash);
    s.activity = Activity::Orbit;
    TEST_ASSERT_TRUE(resolve(s, kAccent).pattern == Pattern::Blink1Hz);
    s.activity = Activity::Test;
    LedOutput o = resolve(s, kAccent);
    TEST_ASSERT_TRUE(sameColor(o.color, VIOLET));
    TEST_ASSERT_TRUE(o.pattern == Pattern::BlinkFast);
}

// Prio 3: identify (0xFC) pisa el standby — blanco rápido (el "locate" del selector).
void test_identify_overrides_standby() {
    LedInputs s; s.link_fresh = true; s.selected_fresh = false; s.identify = true;
    LedOutput o = resolve(s, kAccent);
    TEST_ASSERT_TRUE(sameColor(o.color, WHITE));
    TEST_ASSERT_TRUE(o.pattern == Pattern::BlinkFast);
}

// D6: batería baja = OVERLAY (flash rojo 120 ms cada 3 s), no un estado que tape el base.
void test_battery_overlay_flashes_over_base() {
    LedInputs s; s.link_fresh = true; s.selected_fresh = true; s.low_battery = true;
    LedOutput o = resolve(s, kAccent);
    TEST_ASSERT_TRUE(o.batt_overlay);
    TEST_ASSERT_TRUE(sameColor(o.color, kAccent));            // el estado base sigue siendo acento
    TEST_ASSERT_TRUE(sameColor(sample(o, 0),    RED));        // dentro de la ventana del flash
    TEST_ASSERT_TRUE(sameColor(sample(o, 1000), kAccent));    // fuera: se ve el estado base
}

// Animator: DoubleFlash = 2 flashes + pausa larga dentro del período de 1200 ms.
void test_double_flash_shape() {
    LedOutput o{ kAccent, Pattern::DoubleFlash, false };
    TEST_ASSERT_TRUE(sameColor(sample(o, 40),   kAccent));    // flash 1 (0-80)
    TEST_ASSERT_TRUE(sameColor(sample(o, 120),  BLACK));      // gap (80-240)
    TEST_ASSERT_TRUE(sameColor(sample(o, 280),  kAccent));    // flash 2 (240-320)
    TEST_ASSERT_TRUE(sameColor(sample(o, 700),  BLACK));      // pausa larga
    TEST_ASSERT_TRUE(sameColor(sample(o, 1240), kAccent));    // período siguiente
}

// Secuencia canónica del test de conexión del LED RGB (spec 2026-07-14 D3/D5).
void test_led_test_secuencia_pura() {
    TEST_ASSERT_EQUAL_UINT8(5, LED_TEST_STEP_COUNT);
    // Paso 0 = ROJO puro, 1 = VERDE puro, 2 = AZUL puro (canales exactos → un cruce es inambiguo)
    TEST_ASSERT_EQUAL_UINT8(255, LED_TEST_STEPS[0].color.r);
    TEST_ASSERT_EQUAL_UINT8(0,   LED_TEST_STEPS[0].color.g);
    TEST_ASSERT_EQUAL_UINT8(0,   LED_TEST_STEPS[0].color.b);
    TEST_ASSERT_EQUAL_UINT8(0,   LED_TEST_STEPS[1].color.r);
    TEST_ASSERT_EQUAL_UINT8(255, LED_TEST_STEPS[1].color.g);
    TEST_ASSERT_EQUAL_UINT8(0,   LED_TEST_STEPS[1].color.b);
    TEST_ASSERT_EQUAL_UINT8(0,   LED_TEST_STEPS[2].color.r);
    TEST_ASSERT_EQUAL_UINT8(0,   LED_TEST_STEPS[2].color.g);
    TEST_ASSERT_EQUAL_UINT8(255, LED_TEST_STEPS[2].color.b);
}
void test_led_test_blanco_y_apagado() {
    // BLANCO = los 3 canales >=128 (detecta canal muerto); APAGADO = los 3 en 0 (canal clavado)
    TEST_ASSERT_TRUE(LED_TEST_STEPS[3].color.r >= 128);
    TEST_ASSERT_TRUE(LED_TEST_STEPS[3].color.g >= 128);
    TEST_ASSERT_TRUE(LED_TEST_STEPS[3].color.b >= 128);
    TEST_ASSERT_EQUAL_UINT8(0, LED_TEST_STEPS[4].color.r);
    TEST_ASSERT_EQUAL_UINT8(0, LED_TEST_STEPS[4].color.g);
    TEST_ASSERT_EQUAL_UINT8(0, LED_TEST_STEPS[4].color.b);
}
void test_led_test_avance_con_wrap() {
    TEST_ASSERT_EQUAL_UINT8(1, ledTestNextStep(0));
    TEST_ASSERT_EQUAL_UINT8(4, ledTestNextStep(3));
    TEST_ASSERT_EQUAL_UINT8(0, ledTestNextStep(4));   // wrap: APAGADO → ROJO
}

// byte0 del LedTestPayload = tool_id. Los wheel tests usan 0..4 en su byte0
// (test_mode V3 / rueda V2): el id DEBE quedar fuera de ese rango.
void test_led_test_tool_id_no_colisiona_con_wheel_tests() {
    TEST_ASSERT_EQUAL_UINT8(7, LED_TEST_TOOL_ID);
    TEST_ASSERT_TRUE(LED_TEST_TOOL_ID > 4);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_estop_wins_over_everything);
    RUN_TEST(test_link_vs_selected_are_different_states);
    RUN_TEST(test_activity_patterns);
    RUN_TEST(test_identify_overrides_standby);
    RUN_TEST(test_battery_overlay_flashes_over_base);
    RUN_TEST(test_double_flash_shape);
    RUN_TEST(test_led_test_secuencia_pura);
    RUN_TEST(test_led_test_blanco_y_apagado);
    RUN_TEST(test_led_test_avance_con_wrap);
    RUN_TEST(test_led_test_tool_id_no_colisiona_con_wheel_tests);
    return UNITY_END();
}
