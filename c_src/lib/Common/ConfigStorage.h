#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <Arduino.h>
#include <LittleFS.h>

class ConfigStorage {
public:
    static bool init();
    static bool loadConfig(String &ssid, String &pass, String &url);
    static bool saveConfig(const String &ssid, const String &pass, const String &url);

    static const String CONFIG_FILE;
};

#endif // CONFIG_STORAGE_H
