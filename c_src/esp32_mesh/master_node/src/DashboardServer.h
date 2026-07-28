#ifndef DASHBOARD_SERVER_H
#define DASHBOARD_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <DashboardPage.h>
#include <functional>

class DashboardServer {
public:
    static void start(std::function<String()> statusJsonCallback);
    static void handle();
    static bool isSatelliteOtaPending();
    static void clearSatelliteOtaPending();
    static uint8_t getOtaTargetSatId();

private:
    static bool m_otaPending;
    static uint8_t m_otaTargetSatId;
};

#endif // DASHBOARD_SERVER_H
