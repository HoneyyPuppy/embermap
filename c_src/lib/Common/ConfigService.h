#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#include <Arduino.h>

class ConfigService {
public:
    static bool init();
    static bool loadConfig(String &ssid, String &pass, String &url);
    static bool saveConfig(const String &ssid, const String &pass, const String &url);
    static void startConfigPortal(const char* portalSsid);
    static void handlePortal();
    static bool isPortalActive() { return m_portalActive; }
    static void checkResetButton(uint8_t pin);
    static void startResetButtonTask(uint8_t pin);

private:
    static bool m_portalActive;
    static const String CONFIG_FILE;
};

#endif // CONFIG_SERVICE_H
