#ifndef INPUT_CONTROL_ESPNOW_TRANSPORT_H
#define INPUT_CONTROL_ESPNOW_TRANSPORT_H
#include <cstdint>
#include "ILinkTransport.h"

namespace input_control {

// ILinkTransport sobre ESP-NOW (broadcast, dueño único de esp_now). Singleton interno
// porque el callback de esp_now es una función C estática.
//
// Replicado de EspNowControllerManager (lib/input): misma secuencia de init WiFi STA +
// esp_wifi_set_channel + esp_now_init + register_recv_cb, misma lógica de lazy-peer
// en send(). Diferencia: cachea los bytes del frame crudo (no un ControlPacket tipado),
// y receive() devuelve true una sola vez por frame (flag has_new_).
//
// Solo compilado en ESP32 — excluido de la plataforma native via #ifndef NATIVE_BUILD
// en el .cpp correspondiente.
class EspNowTransport : public ILinkTransport {
public:
    static EspNowTransport& instance();

    // Inicializa WiFi STA + ESP-NOW en el canal dado (mismo que el del mando).
    // Registra el callback de recepción. Idéntico a EspNowControllerManager::begin().
    void begin(uint8_t channel) override;

    // Devuelve true UNA SOLA VEZ por frame nuevo; copia al consume_buf_ interno.
    // data apunta a ese buffer hasta el próximo receive().
    bool receive(const uint8_t*& data, uint8_t& len) override;

    // TX por broadcast (lazy: agrega el peer la primera vez).
    bool send(const uint8_t* data, uint8_t len) override;

    bool isFresh(uint32_t maxAgeMs) const override;
    uint32_t dataAgeMs() const override;

private:
    EspNowTransport() = default;
    EspNowTransport(const EspNowTransport&) = delete;
    EspNowTransport& operator=(const EspNowTransport&) = delete;

    // Callback estático ESP-NOW → reenvía al singleton. Firma exacta del manager.
    static void onRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
    void onRecv(const uint8_t* data, int len);

    static constexpr uint8_t kMaxFrame = 32;

    // Escritos por el callback (tarea WiFi, similar a ISR) → volatile.
    volatile uint8_t  rx_buf_[kMaxFrame];
    volatile uint8_t  rx_len_    = 0;
    volatile uint32_t last_rx_ms_ = 0;
    volatile bool     has_new_   = false;

    // Buffer de consumo: escrito solo en receive() (tarea principal), no necesita volatile.
    uint8_t consume_buf_[kMaxFrame];

    bool peer_added_ = false;
};

} // namespace input_control
#endif // INPUT_CONTROL_ESPNOW_TRANSPORT_H
