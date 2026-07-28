#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <common_config.h>
#include "ConfigStorage.h"
#include <PortalPage.h>
#include <SuccessPage.h>

class CaptivePortal {
public:
    static void start(const char* portalSsid);
    static void handle();
    static bool isActive();

private:
    static bool m_active;
};

#endif // CAPTIVE_PORTAL_H
