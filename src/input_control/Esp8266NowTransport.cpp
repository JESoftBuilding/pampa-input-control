// Esp8266NowTransport.cpp — ILinkTransport sobre ESP-NOW para ESP8266.
//
// Usa cabeceras del core de ESP8266 que no existen en ESP32 ni en native: toda la
// implementación está envuelta en #if defined(ARDUINO_ARCH_ESP8266).

#if defined(ARDUINO_ARCH_ESP8266)

#include "Esp8266NowTransport.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

namespace input_control {

namespace {
uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}

Esp8266NowTransport& Esp8266NowTransport::instance() {
    static Esp8266NowTransport t;
    return t;
}

void Esp8266NowTransport::begin(uint8_t channel) {
    // Sin esto el ESP8266 se reconecta solo a la última red guardada en el flash y el
    // canal lo pasa a fijar ese router.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    // Con el módem durmiendo se pierden tramas.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    wifi_set_channel(channel);

    if (esp_now_init() != 0) return;
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb([](uint8_t*, uint8_t* data, uint8_t len) {
        instance().onRecv(data, len);
    });
    ready_ = true;
}

void Esp8266NowTransport::onRecv(const uint8_t* data, uint8_t len) {
    if (len == 0 || len > kMaxFrame) return;
    const uint8_t h    = head_;
    const uint8_t next = (uint8_t)((h + 1) % kRing);
    if (next == tail_) return;          // ring lleno → descartar
    for (uint8_t i = 0; i < len; ++i) rx_ring_[h][i] = data[i];
    rx_lens_[h] = len;
    head_       = next;                 // publica el frame DESPUÉS de copiarlo
    last_rx_ms_ = millis();
}

bool Esp8266NowTransport::receive(const uint8_t*& data, uint8_t& len) {
    const uint8_t t = tail_;
    if (t == head_) return false;
    const uint8_t n = rx_lens_[t];
    for (uint8_t i = 0; i < n; ++i) consume_buf_[i] = rx_ring_[t][i];
    tail_ = (uint8_t)((t + 1) % kRing);
    data = consume_buf_;
    len  = n;
    return true;
}

bool Esp8266NowTransport::send(const uint8_t* data, uint8_t len) {
    if (!ready_) return false;
    if (!peer_added_) {
        esp_now_add_peer(kBroadcast, ESP_NOW_ROLE_COMBO, 0, nullptr, 0);
        peer_added_ = true;
    }
    return esp_now_send(kBroadcast, const_cast<uint8_t*>(data), len) == 0;
}

bool Esp8266NowTransport::isFresh(uint32_t maxAgeMs) const {
    if (last_rx_ms_ == 0) return false;
    return (millis() - last_rx_ms_) < maxAgeMs;
}

uint32_t Esp8266NowTransport::dataAgeMs() const {
    if (last_rx_ms_ == 0) return UINT32_MAX;
    return millis() - last_rx_ms_;
}

} // namespace input_control

#endif // ARDUINO_ARCH_ESP8266
