#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#include <Arduino.h>
#include <WebServer.h>
#include <functional>

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

    // Phương thức chạy WebServer ở chế độ hoạt động bình thường
    static void startNormalWebServer(std::function<String()> statusJsonCallback);
    static bool isSatelliteOtaPending() { return m_satelliteOtaPending; }
    static void clearSatelliteOtaPending() { m_satelliteOtaPending = false; }
    static uint8_t getOtaTargetSatId() { return m_otaTargetSatId; }
    static void handleNormalWebServer();
    static WebServer& getWebServer();

private:
    static bool m_portalActive;
    static bool m_satelliteOtaPending;
    static uint8_t m_otaTargetSatId;
    static const String CONFIG_FILE;
};

#endif // CONFIG_SERVICE_H
