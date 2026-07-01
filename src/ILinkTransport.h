#ifndef INPUT_CONTROL_ILINK_TRANSPORT_H
#define INPUT_CONTROL_ILINK_TRANSPORT_H
#include <cstdint>

namespace input_control {

// Transporte del link. EspNowTransport es la implementación incluida; FakeTransport
// se usa en tests native. receive() devuelve true SOLO si hay un mensaje nuevo desde
// la última llamada, y deja data apuntando a un buffer interno válido hasta el próximo receive.
struct ILinkTransport {
    virtual void begin(uint8_t channel) = 0;
    virtual bool receive(const uint8_t*& data, uint8_t& len) = 0;
    virtual bool send(const uint8_t* data, uint8_t len) = 0;
    virtual bool isFresh(uint32_t maxAgeMs) const = 0;
    virtual uint32_t dataAgeMs() const = 0;
    virtual ~ILinkTransport() = default;
};

} // namespace input_control
#endif
