#ifndef INPUT_CONTROL_CONFIG_STORE_H
#define INPUT_CONTROL_CONFIG_STORE_H
#include <cstdint>
#include <cstdio>
#include "ConfigProtocol.h"

namespace input_control {

// Backend de persistencia clave→int32. En ESP32 lo implementa NvsConfigBackend (Preferences,
// ver NvsConfigBackend.h); en tests native, un mock en RAM. ConfigStore no sabe de NVS ni
// de Arduino → lógica testeable en host.
struct IConfigBackend {
    // getInt: devuelve el valor guardado, o `def` si la clave no existe.
    virtual int32_t getInt(const char* key, int32_t def) = 0;
    virtual void    putInt(const char* key, int32_t value) = 0;
    virtual ~IConfigBackend() = default;
};

// Persiste (strategy_id,key)→int32 sobre un IConfigBackend. Header-only (métodos inline):
// pura C++ sin estado propio, y así linkea tanto en los tests native de la lib como en los
// consumidores. Sin cache elaborada (v1): getInt es barato y solo se llama en boot + request.
class ConfigStore {
public:
    explicit ConfigStore(IConfigBackend& backend) : backend_(backend) {}

    // Clave textual estable "s%03uk%03u" (8 chars ≤ 15 = límite de clave NVS). Pública p/tests.
    static void makeKey(char out[16], uint8_t strategy_id, uint8_t key) {
        std::snprintf(out, 16, "s%03uk%03u", (unsigned)strategy_id, (unsigned)key);
    }

    // NVS si existe la clave, sino `def` (el caller conoce el default de la key).
    int32_t get(uint8_t strategy_id, uint8_t key, int32_t def) const {
        char k[16]; makeKey(k, strategy_id, key);
        return backend_.getInt(k, def);
    }

    // Persiste SOLO si cambió (anti-wear de flash). El clamp lo hace el CALLER (conoce min/max).
    void set(uint8_t strategy_id, uint8_t key, int32_t value) {
        char k[16]; makeKey(k, strategy_id, key);
        // SENTINEL improbable (INT32_MIN) marca "clave inexistente" → fuerza el primer write.
        if (backend_.getInt(k, INT32_MIN) == value) return;
        backend_.putInt(k, value);
    }

    // Aplica un ConfigPacket entero: set() por item + callback `applier` por item.
    // Keys desconocidas se persisten igual (forward-compat); el applier decide ignorarlas.
    using Applier = void(*)(void* ctx, uint8_t strategy_id, uint8_t key, int32_t value);
    void applyPacket(const ConfigPacket& p, Applier applier, void* ctx) {
        uint8_t n = p.param_count > CONFIG_MAX_ITEMS ? CONFIG_MAX_ITEMS : p.param_count;
        for (uint8_t i = 0; i < n; ++i) {
            set(p.strategy_id, p.items[i].key, p.items[i].value);   // persiste (incl. desconocidas)
            if (applier) applier(ctx, p.strategy_id, p.items[i].key, p.items[i].value);
        }
    }

    // Arma un ConfigPacket (reply robot→mando, §4.4) leyendo del store los `keys` pedidos,
    // usando `defs[i]` cuando no hay valor guardado. n se clampa a CONFIG_MAX_ITEMS.
    // Devuelve param_count. Los items sobrantes quedan en 0 (paquete de tamaño fijo).
    uint8_t snapshotInto(uint8_t strategy_id, const uint8_t* keys, const int32_t* defs,
                         uint8_t n, ConfigPacket& out) const {
        if (n > CONFIG_MAX_ITEMS) n = CONFIG_MAX_ITEMS;
        out.msg_type    = MSG_CONFIG;
        out.strategy_id = strategy_id;
        out.param_count = n;
        for (uint8_t i = 0; i < n; ++i) {
            out.items[i].key   = keys[i];
            out.items[i].value = get(strategy_id, keys[i], defs[i]);
        }
        for (uint8_t i = n; i < CONFIG_MAX_ITEMS; ++i) { out.items[i].key = 0; out.items[i].value = 0; }
        return n;
    }

private:
    IConfigBackend& backend_;
};

} // namespace input_control
#endif
