#include "ButtonRouter.h"

namespace input_control {

ButtonRouter::ButtonRouter(const ButtonBinding* global, uint8_t global_count, uint8_t nav_click_effect)
    : global_(global), global_count_(global_count), nav_click_effect_(nav_click_effect) {}

void ButtonRouter::reset() { prev_buttons_ = 0; last_seq_ = 0; have_seq_ = false; }

static bool matches(const ButtonBinding& b, uint16_t buttons, uint16_t edges) {
    const uint16_t mask = (uint16_t)(1u << b.button_bit);
    return b.trigger == ButtonTrigger::Level ? (buttons & mask) != 0
                                             : (edges   & mask) != 0;
}

uint8_t ButtonRouter::resolve(uint16_t buttons, const NavInput& nav, uint8_t seq, IButtonStrategy* active) {
    const uint16_t edges = (uint16_t)(buttons & ~prev_buttons_);
    const bool seq_new = !have_seq_ || seq != last_seq_;

    uint8_t effect = 0;

    // 1-2) Capa global (orden = prioridad; el host pone E-stop primero).
    for (uint8_t i = 0; i < global_count_ && effect == 0; ++i) {
        if (matches(global_[i], buttons, edges)) effect = global_[i].action_id;
    }

    // 3) Encoder click → efecto global (de-dup por seq).
    if (effect == 0 && seq_new && nav.click && nav_click_effect_ != 0) {
        effect = nav_click_effect_;
    }

    // 4) Botón con flanco no consumido → estrategia activa.
    if (effect == 0 && active) {
        uint8_t count = 0;
        const ButtonBinding* b = active->bindings(count);
        for (uint8_t i = 0; i < count && effect == 0; ++i) {
            if (matches(b[i], buttons, edges)) effect = active->onButtonAction(b[i].action_id);
        }
    }

    // 5) Rotación / long-press → canal lateral del menú (sin effect). Click es global (arriba).
    if (active && seq_new && (nav.delta != 0 || nav.long_press)) {
        active->onNav(nav);
    }

    prev_buttons_ = buttons;
    if (seq_new) { last_seq_ = seq; have_seq_ = true; }
    return effect;
}

} // namespace input_control
