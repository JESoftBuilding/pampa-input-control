#ifndef INPUT_CONTROL_LED_LEDANIMATOR_H
#define INPUT_CONTROL_LED_LEDANIMATOR_H
#include <cstdint>
#include "LedModel.h"

// Convierte {color, patrón, overlay} + now_ms en el RGB instantáneo a mostrar.
// PURO (now_ms por parámetro, nunca millis() adentro — lección 13.4); las divisiones
// uint32 hacen los períodos wraparound-safe. Patrones digital-friendly: en placas sin
// PWM para el LED (V2: los 8 canales LEDC son de los motores) "Breathe" degrada a un
// blink lento suave sin cambio de semántica.
namespace input_control {
namespace led {

// Períodos de patrón (ms) — nombrados para que el patrón sea auditable, no mágico.
constexpr uint32_t BREATHE_HALF_MS   = 800;   // 800 on / 800 off (respirar digital)
constexpr uint32_t BLINK1HZ_HALF_MS  = 500;   // 1 Hz al 50%
constexpr uint32_t BLINKFAST_HALF_MS = 125;   // 4 Hz al 50%
constexpr uint32_t DOUBLEFLASH_PERIOD_MS = 1200;  // [flash 80][off 160][flash 80][pausa]
constexpr uint32_t DOUBLEFLASH_ON_MS     = 80;
constexpr uint32_t DOUBLEFLASH_GAP_MS    = 160;
constexpr uint32_t BATT_OVERLAY_PERIOD_MS = 3000; // 1 flash rojo cada 3 s (D6)
constexpr uint32_t BATT_OVERLAY_ON_MS     = 120;  // corto: inconfundible con e-stop sólido

inline bool patternOn(Pattern p, uint32_t now_ms) {
    switch (p) {
        case Pattern::Solid:     return true;
        case Pattern::Breathe:   return (now_ms / BREATHE_HALF_MS)   % 2u == 0u;
        case Pattern::Blink1Hz:  return (now_ms / BLINK1HZ_HALF_MS)  % 2u == 0u;
        case Pattern::BlinkFast: return (now_ms / BLINKFAST_HALF_MS) % 2u == 0u;
        case Pattern::DoubleFlash: {
            uint32_t t = now_ms % DOUBLEFLASH_PERIOD_MS;
            if (t < DOUBLEFLASH_ON_MS) return true;                            // flash 1
            t -= DOUBLEFLASH_ON_MS;
            if (t < DOUBLEFLASH_GAP_MS) return false;
            return (t - DOUBLEFLASH_GAP_MS) < DOUBLEFLASH_ON_MS;               // flash 2
        }
        case Pattern::Off:
        default:                 return false;
    }
}

// RGB instantáneo. El overlay de batería (flash rojo BATT_OVERLAY_ON_MS cada 3 s) PISA
// lo que hubiera — así "batería baja" se ve sin ocultar el estado base el 96% del tiempo.
inline Rgb sample(const LedOutput& o, uint32_t now_ms) {
    if (o.batt_overlay && (now_ms % BATT_OVERLAY_PERIOD_MS) < BATT_OVERLAY_ON_MS) return RED;
    return patternOn(o.pattern, now_ms) ? o.color : BLACK;
}

} // namespace led
} // namespace input_control
#endif
