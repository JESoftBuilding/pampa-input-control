#include "InputControlModule.h"

namespace input_control {

InputControlModule::InputControlModule(ILinkTransport& transport, IEffectSink& sink, const ModuleConfig& cfg)
    : transport_(transport), sink_(sink),
      router_(cfg.global, cfg.global_count, cfg.nav_click_effect) {}

void InputControlModule::begin(uint8_t channel) { transport_.begin(channel); }

bool InputControlModule::send(const uint8_t* data, uint8_t len) { return transport_.send(data, len); }

void InputControlModule::tick() {
    const uint8_t* data = nullptr; uint8_t len = 0;
    if (!transport_.receive(data, len)) return;

    ControlPacket pkt;
    if (decodeControl(data, len, pkt)) {
        last_ = pkt; seen_ = true;
        const NavInput nav = extractNav(pkt);
        const uint8_t effect = router_.resolve(pkt.buttons, nav, pkt.seq, active_);
        if (effect != 0) sink_.execute(effect);
    } else if (raw_) {
        raw_->onMessage(data, len);
    }
}

} // namespace input_control
