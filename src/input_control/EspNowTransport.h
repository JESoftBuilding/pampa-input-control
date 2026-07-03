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

    // OJO: debe cubrir el frame MÁS GRANDE que recibe el robot. El ConfigPacket mide 43 B
    // (3 + 8*5) → con 32 se descartaba en onRecv (`len > kMaxFrame`) y la config NUNCA llegaba
    // (el WRITE se perdía → no guardaba). ControlPacket=11B y ConfigRequest=3B sí entraban.
    static constexpr uint8_t kMaxFrame = 64;   // ≥ sizeof(ConfigPacket)=43, con margen
    // Ring SPSC (productor = callback WiFi, consumidor = receive() en el loop). Un solo slot
    // NO alcanza: el mando emite ControlPacket a 50 Hz, y el ConfigPacket/ConfigRequest viaja
    // en la misma ráfaga → con un slot el control pisa la config antes de que el loop la lea
    // (config perdida → no se guarda). El ring absorbe la ráfaga; drena por-iteración del loop.
    static constexpr uint8_t kRing = 8;

    // Escritos por el callback (tarea WiFi). SPSC: el productor solo avanza head_, el consumidor
    // solo avanza tail_ → índices uint8 atómicos, sin lock. Publica head_ DESPUÉS de copiar.
    volatile uint8_t  rx_ring_[kRing][kMaxFrame];
    volatile uint8_t  rx_lens_[kRing];
    volatile uint8_t  head_ = 0;      // próxima posición a escribir (callback)
    volatile uint8_t  tail_ = 0;      // próxima posición a leer (receive)
    volatile uint32_t last_rx_ms_ = 0;

    // Buffer de consumo: escrito solo en receive() (tarea principal), no necesita volatile.
    uint8_t consume_buf_[kMaxFrame];

    bool peer_added_ = false;
};

} // namespace input_control
#endif // INPUT_CONTROL_ESPNOW_TRANSPORT_H
