#ifndef INPUT_CONTROL_INPUT_CONTROL_MODULE_H
#define INPUT_CONTROL_INPUT_CONTROL_MODULE_H
#include <cstdint>
#include "ButtonActions.h"
#include "Ports.h"
#include "ILinkTransport.h"
#include "ButtonRouter.h"
#include "ControlProtocol.h"

namespace input_control {

struct ModuleConfig {
    const ButtonBinding* global;        // tabla global de botones (host)
    uint8_t              global_count;
    uint8_t              nav_click_effect;  // effect_id al click del encoder (0 = ninguno)
    uint8_t              robot_id;          // id de ESTE robot; ignora paquetes con otro
                                            // target (ROBOT_ID_ALL siempre pasa)
};

// Fachada end-to-end: dueña del transporte, decodifica el protocolo, despacha y expone
// el input (sticks/freshness) para la estrategia/host. Reemplaza a EspNowControllerManager.
class InputControlModule {
public:
    InputControlModule(ILinkTransport& transport, IEffectSink& sink, const ModuleConfig& cfg);

    void begin(uint8_t channel);
    void setActiveStrategy(IButtonStrategy* s) { active_ = s; }
    void setRawInbound(IRawInbound* r)         { raw_ = r; }

    // Drena el inbound. now_ms alimenta targetedAgeMs() (pasar millis(); 0 = compat previa,
    // deja targetedAgeMs sin efecto). El default preserva la firma vieja de los consumidores.
    void tick(uint32_t now_ms = 0);
    bool send(const uint8_t* data, uint8_t len);  // TX para telemetría/log-dump del host

    // Input expuesto (lo lee la estrategia de teleop en vez del singleton viejo).
    int8_t lx() const { return last_.lx; }
    int8_t ly() const { return last_.ly; }
    int8_t rx() const { return last_.rx; }
    int8_t ry() const { return last_.ry; }
    uint16_t buttons() const { return last_.buttons; }
    uint8_t  lastSeq() const { return last_.seq; }
    bool     isConnected() const { return seen_; }
    bool     isFresh(uint32_t maxAgeMs = 300) const { return transport_.isFresh(maxAgeMs); }
    uint32_t dataAgeMs() const { return transport_.dataAgeMs(); }

    // ── Frescura DIRIGIDA (spec 2026-07-11 D1) ─────────────────────────────────────────
    // isFresh()/dataAgeMs() son del TRANSPORTE: se refrescan con CUALQUIER frame del canal
    // (control a otro robot, config, telemetría ajena — todo es broadcast). Esto mide solo
    // los ControlPacket que pasaron isForRobot() → "el mando me está controlando A MÍ".
    // Requiere tick(now_ms) con tiempo real (millis()); con tick() legacy queda sin datos.
    uint32_t targetedAgeMs(uint32_t now_ms) const {
        return targeted_seen_ ? (now_ms - last_targeted_ms_) : UINT32_MAX;
    }
    bool isTargetedFresh(uint32_t now_ms, uint32_t maxAgeMs = 300) const {
        return targetedAgeMs(now_ms) <= maxAgeMs;
    }

private:
    ILinkTransport& transport_;
    IEffectSink&    sink_;
    ButtonRouter    router_;
    IButtonStrategy* active_ = nullptr;
    IRawInbound*     raw_ = nullptr;
    ControlPacket    last_{};
    bool             seen_ = false;
    uint8_t          robot_id_ = ROBOT_ID_ALL;  // id de este robot (filtro de target)
    uint32_t         last_targeted_ms_ = 0;     // millis del último ControlPacket PARA MÍ
    bool             targeted_seen_ = false;    // hubo al menos uno (evita edad falsa en boot)
};

} // namespace input_control
#endif
