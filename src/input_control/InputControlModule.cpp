#include "InputControlModule.h"

namespace input_control {

InputControlModule::InputControlModule(ILinkTransport& transport, IEffectSink& sink, const ModuleConfig& cfg)
    : transport_(transport), sink_(sink),
      router_(cfg.global, cfg.global_count, cfg.nav_click_effect),
      robot_id_(cfg.robot_id) {}

void InputControlModule::begin(uint8_t channel) { transport_.begin(channel); }

bool InputControlModule::send(const uint8_t* data, uint8_t len) { return transport_.send(data, len); }

void InputControlModule::tick(uint32_t now_ms) {
    // Drenar TODO el ring por tick, no un solo frame. El transporte es una cola FIFO
    // (EspNowTransport): con `receive()` una vez por iteración del loop, si el mando
    // produce a 50 Hz y el consumidor drena a ≤50 Hz la cola se llena y el robot queda
    // leyendo comandos VIEJOS (lag ≈ profundidad_cola × período_loop). Para un link de
    // teleop el comando viejo no sirve: se procesan todos los frames pendientes (para no
    // perder edges de botón, que dependen del `seq`) y `last_` termina en el MÁS NUEVO.
    const uint8_t* data = nullptr; uint8_t len = 0;
    while (transport_.receive(data, len)) {
        ControlPacket pkt;
        if (decodeControl(data, len, pkt)) {
            if (!isForRobot(pkt, robot_id_)) continue;  // no es para este robot → ignorar
            last_ = pkt; seen_ = true;
            // Frescura dirigida (spec 2026-07-11 D1): solo control QUE ME APUNTA. Con el
            // tick() legacy (now_ms=0) no se registra — targetedAgeMs queda UINT32_MAX.
            if (now_ms != 0) { last_targeted_ms_ = now_ms; targeted_seen_ = true; }
            const NavInput nav = extractNav(pkt);
            const uint8_t effect = router_.resolve(pkt.buttons, nav, pkt.seq, active_);
            if (effect != 0) sink_.execute(effect);
        } else if (raw_) {
            raw_->onMessage(data, len);
        }
    }
}

} // namespace input_control
