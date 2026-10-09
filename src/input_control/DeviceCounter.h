#ifndef INPUT_CONTROL_DEVICE_COUNTER_H
#define INPUT_CONTROL_DEVICE_COUNTER_H
#include <cstdint>

namespace input_control {

// Contador de 64 bits que viaja en cada paquete del protocolo de dispositivos.
//
// Cumple dos funciones a la vez:
//   1. Es parte del nonce del cifrado. Con una misma clave un nonce NO se puede repetir,
//      así que un emisor jamás puede mandar dos paquetes con el mismo (epoch, seq).
//   2. Ordena los paquetes: el receptor sólo acepta contadores mayores al último aceptado
//      de esa fuente. Eso descarta las copias (el emisor repite cada paquete porque la
//      difusión no confirma entrega) y los reenvíos de paquetes grabados.
//
// `epoch` es lo que hace que funcione a través de reinicios: el firmware lo guarda en
// memoria permanente y lo incrementa UNA vez en cada arranque, antes de mandar nada.
// `seq` arranca en 0 en cada época. Un dispositivo que duerme (el pedal) puede subir la
// época en cada despertar: con 32 bits alcanza para miles de millones.
struct DevCounter {
    uint32_t epoch;
    uint32_t seq;
};

// true si `a` es estrictamente posterior a `b`.
inline bool isNewer(const DevCounter& a, const DevCounter& b) {
    return a.epoch > b.epoch || (a.epoch == b.epoch && a.seq > b.seq);
}

// Lado emisor: entrega contadores que nunca se repiten dentro de una época.
class TxCounter {
public:
    // `epoch` = el valor ya incrementado y guardado en memoria permanente en este arranque.
    explicit TxCounter(uint32_t epoch, uint32_t first_seq = 0)
        : epoch_(epoch), next_seq_(first_seq) {}

    // false = la época se agotó: hay que subir la época (guardarla) y crear otro TxCounter.
    // No da la vuelta a 0: eso repetiría un nonce. Si devuelve false, `out` no se toca.
    bool next(DevCounter& out) {
        if (exhausted_) return false;
        out.epoch = epoch_;
        out.seq   = next_seq_;
        if (next_seq_ == 0xFFFFFFFFu) exhausted_ = true;
        else ++next_seq_;
        return true;
    }

    bool exhausted() const { return exhausted_; }

private:
    uint32_t epoch_;
    uint32_t next_seq_;
    bool     exhausted_ = false;
};

// Lado receptor: recuerda el último contador aceptado de cada fuente.
//
// IMPORTANTE: llamar a accept() sólo DESPUÉS de verificar la etiqueta del paquete. Si se
// llamara antes, un paquete falso con un contador altísimo dejaría afuera a los legítimos.
// receiveFrame() de DeviceProtocol.h hace las dos cosas en el orden correcto.
class ReplayGuard {
public:
    static constexpr uint8_t kSources = 8;   // fuentes recordadas a la vez

    // true = paquete nuevo, procesarlo. false = copia o reenvío, ignorarlo.
    bool accept(uint8_t source, const DevCounter& c) {
        Slot* slot = find(source);
        if (slot) {
            if (!isNewer(c, slot->last)) return false;
        } else {
            slot = leastRecentlyAccepted();
            slot->source = source;
            slot->used   = true;
        }
        slot->last = c;
        slot->tick = ++tick_;
        return true;
    }

private:
    struct Slot {
        uint8_t    source = 0;
        bool       used   = false;
        DevCounter last   = {0, 0};
        uint32_t   tick   = 0;      // orden en que se aceptó por última vez
    };

    Slot* find(uint8_t source) {
        for (Slot& s : slots_) {
            if (s.used && s.source == source) return &s;
        }
        return nullptr;
    }

    // Un lugar libre si hay; si no, el de la fuente que hace más que no se acepta.
    Slot* leastRecentlyAccepted() {
        Slot* oldest = &slots_[0];
        for (Slot& s : slots_) {
            if (!s.used) return &s;
            if (s.tick < oldest->tick) oldest = &s;
        }
        return oldest;
    }

    Slot     slots_[kSources];
    uint32_t tick_ = 0;
};

} // namespace input_control
#endif
