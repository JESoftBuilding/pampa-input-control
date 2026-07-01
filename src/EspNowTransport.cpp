// EspNowTransport.cpp — ILinkTransport sobre ESP-NOW.
//
// NOTA: Este archivo usa cabeceras Arduino/WiFi/esp_now que no existen en la
// plataforma native. Toda la implementación está envuelta en #ifndef NATIVE_BUILD
// para que `pio test -e native` siga compilando sin errores.
// El env:native define -DNATIVE_BUILD en build_flags de platformio.ini.

#ifndef NATIVE_BUILD

#include "EspNowTransport.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

namespace input_control {

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
EspNowTransport& EspNowTransport::instance() {
    static EspNowTransport t;
    return t;
}

// ---------------------------------------------------------------------------
// Callback ESP-NOW (corre en la tarea WiFi — tratar como ISR)
// Firma idéntica a EspNowControllerManager::onRecvStatic.
// ---------------------------------------------------------------------------
void EspNowTransport::onRecvStatic(const uint8_t* mac, const uint8_t* data, int len) {
    (void)mac;
    instance().onRecv(data, len);
}

void EspNowTransport::onRecv(const uint8_t* data, int len) {
    if (len <= 0 || len > kMaxFrame) return;
    // Copia manual: rx_buf_ es volatile, memcpy no acepta volatile.
    for (int i = 0; i < len; ++i) rx_buf_[i] = data[i];
    rx_len_    = static_cast<uint8_t>(len);
    last_rx_ms_ = millis();
    has_new_   = true;
}

// ---------------------------------------------------------------------------
// begin() — secuencia IDÉNTICA a EspNowControllerManager::begin().
// WiFi STA → disconnect → set channel → esp_now_init → register_recv_cb.
// ---------------------------------------------------------------------------
void EspNowTransport::begin(uint8_t channel) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        // Sin logger disponible en input_control; el fallo es silencioso aquí.
        // El llamador puede detectarlo vía !isFresh() tras el primer tick.
        return;
    }
    esp_now_register_recv_cb(&EspNowTransport::onRecvStatic);
}

// ---------------------------------------------------------------------------
// receive() — devuelve true UNA SOLA VEZ por frame.
// ---------------------------------------------------------------------------
bool EspNowTransport::receive(const uint8_t*& data, uint8_t& len) {
    if (!has_new_) return false;
    noInterrupts();
    uint8_t n = rx_len_;
    for (uint8_t i = 0; i < n; ++i) consume_buf_[i] = rx_buf_[i];
    has_new_ = false;
    interrupts();
    data = consume_buf_;
    len  = n;
    return true;
}

// ---------------------------------------------------------------------------
// send() — broadcast lazy. Secuencia IDÉNTICA a EspNowControllerManager::send().
// ---------------------------------------------------------------------------
bool EspNowTransport::send(const uint8_t* data, uint8_t len) {
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!peer_added_) {
        esp_now_peer_info_t peer{};
        std::memcpy(peer.peer_addr, bcast, 6);
        peer.channel = 0;       // 0 = usar el canal actual (el de begin())
        peer.encrypt = false;
        esp_now_add_peer(&peer); // ESP_ERR_ESPNOW_EXIST es benigno
        peer_added_ = true;
    }
    return esp_now_send(bcast, data, static_cast<size_t>(len)) == ESP_OK;
}

// ---------------------------------------------------------------------------
// isFresh() / dataAgeMs() — igual que EspNowControllerManager.
// ---------------------------------------------------------------------------
bool EspNowTransport::isFresh(uint32_t maxAgeMs) const {
    if (last_rx_ms_ == 0) return false;
    return (millis() - last_rx_ms_) < maxAgeMs;
}

uint32_t EspNowTransport::dataAgeMs() const {
    if (last_rx_ms_ == 0) return UINT32_MAX;
    return millis() - last_rx_ms_;
}

} // namespace input_control

#endif // NATIVE_BUILD
