#ifndef RESET_BUTTON_H
#define RESET_BUTTON_H

#include <Arduino.h>
#include <LittleFS.h>

class ResetButton {
public:
    static void check(uint8_t pin);
    static void startTask(uint8_t pin);
};

#endif // RESET_BUTTON_H
