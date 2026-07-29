#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

#include <Arduino.h>

class SerialConsole {
public:
    static void init(unsigned long baudRate = 115200);
    static bool checkCommand(String &outCmd);
};

#endif // SERIAL_CONSOLE_H
