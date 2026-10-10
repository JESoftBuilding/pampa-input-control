#ifndef INPUT_CONTROL_ESP8266_NOW_TRANSPORT_H
#define INPUT_CONTROL_ESP8266_NOW_TRANSPORT_H
#include <cstdint>
#include "ILinkTransport.h"

namespace input_control {

// ILinkTransport sobre ESP-NOW para ESP8266. Es el equivalente de EspNowTransport, que
// usa la API de ESP32 (esp_now.h / esp_wifi.h) y no compila en ESP8266 (espnow.h, con
// roles y otra firma de callback). Mismo comportamiento: difusión, un canal, cola de
// kRing tramas de hasta kMaxFrame bytes. En el aire los dos se entienden.
//
// Pensado para los dispositivos de la sala (DeviceProtocol.h): no lo usa ningún robot.
//
// Solo compilado en ESP8266 — el .cpp está envuelto en #if defined(ARDUINO_ARCH_ESP8266).
// Singleton interno porque el callback de esp_now es una función C.
class Esp8266NowTransport : public ILinkTransport {
public:
    static Esp8266NowTransport& instance();

    void begin(uint8_t channel) override;
    bool receive(const uint8_t*& data, uint8_t& len) override;
    bool send(const uint8_t* data, uint8_t len) override;
    bool isFresh(uint32_t maxAgeMs) const override;
    uint32_t dataAgeMs() const override;

    // false si esp_now_init falló en begin().
    bool ready() const { return ready_; }

private:
    Esp8266NowTransport() = default;
    Esp8266NowTransport(const Esp8266NowTransport&) = delete;
    Esp8266NowTransport& operator=(const Esp8266NowTransport&) = delete;

    void onRecv(const uint8_t* data, uint8_t len);

    static constexpr uint8_t kMaxFrame = 64;   // = EspNowTransport::kMaxFrame = DEV_MAX_FRAME
    static constexpr uint8_t kRing = 8;

    // Ring SPSC igual al de EspNowTransport: el callback sólo avanza head_ y receive()
    // sólo avanza tail_.
    volatile uint8_t  rx_ring_[kRing][kMaxFrame];
    volatile uint8_t  rx_lens_[kRing];
    volatile uint8_t  head_ = 0;
    volatile uint8_t  tail_ = 0;
    volatile uint32_t last_rx_ms_ = 0;

    uint8_t consume_buf_[kMaxFrame];
    bool    ready_ = false;
    bool    peer_added_ = false;
};

} // namespace input_control
#endif // INPUT_CONTROL_ESP8266_NOW_TRANSPORT_H
