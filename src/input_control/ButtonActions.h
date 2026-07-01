#ifndef INPUT_CONTROL_BUTTON_ACTIONS_H
#define INPUT_CONTROL_BUTTON_ACTIONS_H
#include <cstdint>

namespace input_control {

// Edge = dispara al apretar; Level = activo mientras se mantiene.
enum class ButtonTrigger : uint8_t { Edge, Level };

// Binding declarativo botón→acción. button_bit = índice de bit en el bitmask (0..15),
// NO un enum de dominio. label viaja al mando para la leyenda en pantalla.
struct ButtonBinding {
    uint8_t       button_bit;
    ButtonTrigger trigger;
    const char*   label;
    uint8_t       action_id;
};

// Encoder decodificado por ciclo.
struct NavInput {
    int8_t delta;       // detents acumulados con signo desde el último paquete
    bool   click;
    bool   long_press;
};

} // namespace input_control
#endif
