#ifndef INPUT_CONTROL_LED_LEDMODEL_H
#define INPUT_CONTROL_LED_LEDMODEL_H
#include <cstdint>

// Modelo PURO del LED de estado unificado de los robots Pampa (spec 2026-07-11).
// UNA tabla semántica = una fuente de verdad para TODOS los robots; cada placa solo
// implementa su driver (V2: RGB digital on/off que cuantiza; futuro: WS2812 a todo color).
//
// Concepto: color = estado del vínculo/seguridad · patrón = actividad del modo.
// Sin Arduino ni millis() adentro (testeable en host): el tiempo entra por parámetro
// en LedAnimator::sample(). El color de "seleccionado" es el ACENTO del robot (D2) —
// el mismo que muestra el mando en su header (V3 ámbar, V2-DVK teal, V2-S3 violeta).
namespace input_control {
namespace led {

struct Rgb { uint8_t r, g, b; };

// Paleta base (RGB888; el driver digital de V2 cuantiza por canal, umbral >=128).
constexpr Rgb BLACK  {   0,   0,   0 };
constexpr Rgb RED    { 255,   0,   0 };   // e-stop / overlay batería
constexpr Rgb BLUE   {   0,   0, 255 };   // sin mando en el aire
constexpr Rgb CYAN   {   0, 200, 200 };   // standby: mando presente, NO seleccionado
constexpr Rgb WHITE  { 255, 255, 255 };   // OTA / identify
constexpr Rgb VIOLET { 180,   0, 255 };   // modo test (peligro controlado)

enum class Pattern : uint8_t {
    Off,
    Solid,
    Breathe,      // pasivo/espera (digital: blink lento suave 800/800 ms)
    Blink1Hz,     // actividad continua (orbit)
    BlinkFast,    // alerta activa 4 Hz (OTA / identify / test)
    DoubleFlash,  // latido: 2 flashes + pausa (hold: "clavado en un rumbo")
};

// Actividad del modo del robot (la mapea cada firmware desde su brain/estrategia).
enum class Activity : uint8_t { Manual, Hold, Orbit, Test, ObstacleNear /*reservado V2*/ };

// Snapshot de estado que arma el firmware una vez por loop.
struct LedInputs {
    bool estop         = false;
    bool low_battery   = false;
    bool ota           = false;   // modo actualización/mantenimiento
    bool identify      = false;   // el mando me está resaltando en el selector (0xFC)
    bool link_fresh    = false;   // hay mando en el aire (frescura del TRANSPORTE)
    bool selected_fresh = false;  // me está controlando A MÍ (targetedAgeMs, D1)
    Activity activity  = Activity::Manual;
};

struct LedOutput {
    Rgb     color;
    Pattern pattern;
    bool    batt_overlay;   // D6: flash rojo corto cada ~3 s SUPERPUESTO al estado base
};

// Escalera de prioridad (spec §3) — gana el estado de arriba. La batería baja NO es un
// estado: es un overlay que el animator intercala sin tapar la semántica base.
inline LedOutput resolve(const LedInputs& s, Rgb accent) {
    LedOutput out{ BLACK, Pattern::Off, s.low_battery && !s.estop };
    if (s.estop)          { out.color = RED;   out.pattern = Pattern::Solid;     out.batt_overlay = false; return out; }
    if (s.ota)            { out.color = WHITE; out.pattern = Pattern::BlinkFast; return out; }
    if (s.identify)       { out.color = WHITE; out.pattern = Pattern::BlinkFast; return out; }
    if (!s.link_fresh)    { out.color = BLUE;  out.pattern = Pattern::Breathe;   return out; }
    if (!s.selected_fresh){ out.color = CYAN;  out.pattern = Pattern::Breathe;   return out; }
    switch (s.activity) {                       // seleccionado: acento + patrón por modo
        case Activity::Test:   out.color = VIOLET; out.pattern = Pattern::BlinkFast;   break;
        case Activity::Hold:   out.color = accent; out.pattern = Pattern::DoubleFlash; break;
        case Activity::Orbit:  out.color = accent; out.pattern = Pattern::Blink1Hz;    break;
        case Activity::ObstacleNear:                // reservado: frecuencia ∝ distancia (futuro)
        case Activity::Manual:
        default:               out.color = accent; out.pattern = Pattern::Solid;       break;
    }
    return out;
}

} // namespace led
} // namespace input_control
#endif
