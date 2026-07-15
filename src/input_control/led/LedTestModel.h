#ifndef INPUT_CONTROL_LED_LEDTESTMODEL_H
#define INPUT_CONTROL_LED_LEDTESTMODEL_H
#include <cstdint>
#include "LedModel.h"

// Secuencia canónica del test de conexión del LED RGB (spec 2026-07-14 D3/D5).
// UNA tabla para V2, V3 y el mando: el robot la aplica al driver, el mando la usa
// para el label/color esperado. Colores PUROS a propósito: cada paso enciende
// exactamente los canales que dice, para que un cruce G↔B o un canal muerto sea
// inambiguo a ojo. Termina en APAGADO (detecta canal clavado y deja el LED limpio).
namespace input_control {
namespace led {

struct LedTestStep { const char* label; Rgb color; };

constexpr LedTestStep LED_TEST_STEPS[] = {
    { "ROJO",    RED },                 // {255,0,0}
    { "VERDE",   Rgb{ 0, 255, 0 } },
    { "AZUL",    BLUE },                // {0,0,255}
    { "BLANCO",  WHITE },               // los 3 canales — canal muerto a la vista
    { "APAGADO", BLACK },               // ningún canal — canal clavado a la vista
};
constexpr uint8_t LED_TEST_STEP_COUNT =
    (uint8_t)(sizeof(LED_TEST_STEPS) / sizeof(LED_TEST_STEPS[0]));

inline uint8_t ledTestNextStep(uint8_t step) {
    return (uint8_t)((step + 1u) % LED_TEST_STEP_COUNT);
}

// Id del tool "LED RGB" en los catálogos del mando (kV2Tests/kV3Tests) y en el
// CONFIG_TEST_CTRL (0xFE). Es 7 A PROPÓSITO: viaja como byte0 del LedTestPayload y
// discrimina del TestPayload de los wheel tests, cuyo byte0 (test_mode/rueda) es <= 4.
constexpr uint8_t LED_TEST_TOOL_ID = 7;

} // namespace led
} // namespace input_control
#endif
