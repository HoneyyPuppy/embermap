#include "SensorService.h"
#include <common_config.h>

// Pins
#define ONE_WIRE_BUS 4
#define MQ2_PIN 34
#define BUZZER_PIN 25
#define NEOPIXEL_PIN 26
#define NUMPIXELS 1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 SensorService::display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OneWire SensorService::oneWire(ONE_WIRE_BUS);
DallasTemperature SensorService::sensors(&SensorService::oneWire);
Adafruit_NeoPixel SensorService::pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
bool SensorService::oledConnected = false;

const float SensorService::TEMP_THRESHOLD = 50.0;
const int SensorService::GAS_THRESHOLD = 300;

void SensorService::init(const char* nodeName) {
#if HAS_OLED
    Wire.begin(); // Khởi tạo bus I2C trước
    if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        oledConnected = true;
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,0);
        display.printf("Init %s...\n", nodeName);
        display.display();
    } else {
        Serial.println(F("[OLED Warning] OLED display not found at 0x3C. Disabling display outputs."));
        oledConnected = false;
    }
#endif

#if HAS_NEOPIXEL
    pixels.begin();
    pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue for startup
    pixels.show();
#endif

#if HAS_TEMP_SENSOR
    sensors.begin();
#endif

#if HAS_BUZZER
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
#endif
}

void SensorService::read(float &temp, int &gas, bool &emergency) {
#if HAS_TEMP_SENSOR
    sensors.requestTemperatures();
    temp = sensors.getTempCByIndex(0);
    if (temp == DEVICE_DISCONNECTED_C) {
        temp = 27.2; // Fallback if hardware not connected
    }
#else
    temp = 27.2; // Giả lập nhiệt độ khi không sử dụng cảm biến thật
#endif

#if HAS_GAS_SENSOR
    gas = analogRead(MQ2_PIN);
#else
    gas = 120;   // Giả lập nồng độ gas khi không sử dụng cảm biến thật
#endif
    emergency = (temp >= TEMP_THRESHOLD || gas >= GAS_THRESHOLD);
}

void SensorService::updateAlarm(bool emergency, bool hasRoute, float repulsivePotential) {
#if HAS_BUZZER
    if (emergency) {
        digitalWrite(BUZZER_PIN, HIGH);
    } else {
        digitalWrite(BUZZER_PIN, LOW);
    }
#endif

#if HAS_NEOPIXEL
    if (emergency) {
        pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red flashing
    } else {
        if (hasRoute) {
            if (repulsivePotential > 0.0) {
                pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Yellow warning (APF)
            } else {
                pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Green: OK
            }
        } else {
            pixels.setPixelColor(0, pixels.Color(255, 165, 0)); // Orange: Lost route / offline
        }
    }
    pixels.show();
#endif
}

void SensorService::displayMaster(const char* protocolName, float temp, int gas, bool wifiConnected, const String &ipAddress) {
#if HAS_OLED
    if (!oledConnected) return;
    display.clearDisplay();
    display.setCursor(0,0);
    display.printf("MASTER (%s)\n", protocolName);
    display.printf("Temp: %.1f C\n", temp);
    display.printf("Gas: %d\n", gas);
    if (wifiConnected) {
        display.println("WiFi: OK");
        display.println(ipAddress);
    } else {
        display.println("WiFi: DISCONNECTED");
    }
    display.display();
#endif
}

void SensorService::displaySatellite(const char* protocolName, int nodeId, float temp, int gas, bool hasRoute, float routingInfo, const uint8_t *nextHopMac) {
#if HAS_OLED
    if (!oledConnected) return;
    display.clearDisplay();
    display.setCursor(0,0);
    display.printf("NODE %d (%s)\n", nodeId, protocolName);
    display.printf("Temp: %.1f C\n", temp);
    display.printf("Gas: %d\n", gas);
    if (hasRoute) {
        if (strcmp(protocolName, "APF") == 0) {
            display.printf("My U: %.0f\n", routingInfo);
        } else if (strcmp(protocolName, "RPL") == 0) {
            display.printf("Rank: %.0f\n", routingInfo);
        } else {
            display.printf("Hop Cost: %.1f\n", routingInfo);
        }
        display.printf("Next: %02X:%02X...\n", nextHopMac[0], nextHopMac[1]);
    } else {
        display.println("Status: MAT TUYEN");
    }
    display.display();
#endif
}
