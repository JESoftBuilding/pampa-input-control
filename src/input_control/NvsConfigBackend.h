#ifndef INPUT_CONTROL_NVS_CONFIG_BACKEND_H
#define INPUT_CONTROL_NVS_CONFIG_BACKEND_H

// Backend NVS de ConfigStore sobre Arduino Preferences (ESP32). Header-only y NUNCA incluido
// por los tests native → no arrastra <Preferences.h> al host. Guardado igual por si algún
// TU lo incluye desde código compartido compilado en native.
#ifndef NATIVE_BUILD

#include "ConfigStore.h"
#include <Preferences.h>

namespace input_control {

// IConfigBackend respaldado por una partición NVS (namespace configurable). El robot usa
// "cfg"; el mando usa "mcfg" para sus params scope-mando (spec §5.2). Un backend por store.
class NvsConfigBackend : public IConfigBackend {
public:
    explicit NvsConfigBackend(const char* ns) : ns_(ns) {}

    // Abre la partición en modo RW. Llamar una vez en setup() antes del primer get/set.
    void begin() { prefs_.begin(ns_, /*readOnly=*/false); }

    int32_t getInt(const char* key, int32_t def) override { return prefs_.getInt(key, def); }
    void    putInt(const char* key, int32_t value) override { prefs_.putInt(key, value); }

private:
    Preferences prefs_;
    const char* ns_;
};

} // namespace input_control

#endif // NATIVE_BUILD
#endif
