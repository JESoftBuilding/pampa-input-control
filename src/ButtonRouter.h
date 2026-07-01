#ifndef INPUT_CONTROL_BUTTON_ROUTER_H
#define INPUT_CONTROL_BUTTON_ROUTER_H
#include <cstdint>
#include "ButtonActions.h"
#include "Ports.h"

namespace input_control {

// Detecta flancos, de-dup por seq, y resuelve UN effect_id por ciclo:
//   global (tabla en orden, Level/Edge) → encoder click → estrategia → onNav (lateral).
class ButtonRouter {
public:
    ButtonRouter(const ButtonBinding* global, uint8_t global_count, uint8_t nav_click_effect);
    uint8_t resolve(uint16_t buttons, const NavInput& nav, uint8_t seq, IButtonStrategy* active);
    void reset();

private:
    const ButtonBinding* global_;
    uint8_t  global_count_;
    uint8_t  nav_click_effect_;
    uint16_t prev_buttons_ = 0;
    uint8_t  last_seq_ = 0;
    bool     have_seq_ = false;
};

} // namespace input_control
#endif
