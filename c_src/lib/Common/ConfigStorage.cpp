#include "ConfigStorage.h"

const String ConfigStorage::CONFIG_FILE = "/config.txt";

bool ConfigStorage::init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Error mounting file system. Formatted instead.");
        return false;
    }
    return true;
}

bool ConfigStorage::loadConfig(String &ssid, String &pass, String &url) {
    if (!LittleFS.exists(CONFIG_FILE)) {
        return false;
    }
    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        return false;
    }
    ssid = file.readStringUntil('\n'); ssid.trim();
    pass = file.readStringUntil('\n'); pass.trim();
    url = file.readStringUntil('\n');   url.trim();
    file.close();
    return (ssid.length() > 0);
}

bool ConfigStorage::saveConfig(const String &ssid, const String &pass, const String &url) {
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[Config] Failed to open config file for writing");
        return false;
    }
    file.println(ssid);
    file.println(pass);
    file.println(url);
    file.close();
    Serial.println("[Config] Saved new configuration successfully.");
    return true;
}
