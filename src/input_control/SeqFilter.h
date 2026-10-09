#ifndef INPUT_CONTROL_SEQ_FILTER_H
#define INPUT_CONTROL_SEQ_FILTER_H
#include <cstdint>

namespace input_control {

// Descarta las repeticiones de un mismo paquete del protocolo de dispositivos.
//
// Por qué existe: ESP-NOW por difusión no confirma entrega, así que el emisor manda cada
// paquete varias veces. Para un comando como AdjustBrightness, aplicarlo dos veces es un
// error: el receptor tiene que quedarse con uno solo. Va en la librería para que cada
// receptor (luces, receptor de la PC) no escriba el suyo.
//
// Una repetición es: misma fuente y mismo seq que el último paquete ACEPTADO de esa
// fuente, dentro de la ventana. Pasada la ventana, el mismo seq es un paquete nuevo: un
// emisor que duerme y arranca siempre en seq 0 (el pedal) sigue funcionando.
class SeqFilter {
public:
    static constexpr uint8_t kSources = 8;   // emisores recordados a la vez

    explicit SeqFilter(uint32_t window_ms = 500) : window_ms_(window_ms) {}

    // true = procesar el paquete; false = es una repetición, ignorarlo.
    bool accept(uint8_t source, uint8_t seq, uint32_t now_ms) {
        Slot* slot = find(source);
        if (slot) {
            const uint32_t elapsed = now_ms - slot->at_ms;   // sin signo: tolera el desborde
            if (slot->seq == seq && elapsed < window_ms_) return false;
        } else {
            slot = leastRecent();
            slot->source = source;
            slot->used   = true;
        }
        slot->seq   = seq;
        slot->at_ms = now_ms;
        return true;
    }

private:
    struct Slot {
        uint8_t  source = 0;
        uint8_t  seq    = 0;
        uint32_t at_ms  = 0;
        bool     used   = false;
    };

    Slot* find(uint8_t source) {
        for (Slot& s : slots_) {
            if (s.used && s.source == source) return &s;
        }
        return nullptr;
    }

    // Un lugar libre si hay; si no, el del emisor que hace más tiempo que no se acepta.
    Slot* leastRecent() {
        Slot* oldest = &slots_[0];
        for (Slot& s : slots_) {
            if (!s.used) return &s;
            if (s.at_ms < oldest->at_ms) oldest = &s;
        }
        return oldest;
    }

    Slot     slots_[kSources];
    uint32_t window_ms_;
};

} // namespace input_control
#endif
