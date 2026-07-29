#include "SerialConsole.h"

void SerialConsole::init(unsigned long baudRate) {
    // Chỉ khởi động Serial nếu nó chưa được khởi động trước đó
    if (!Serial) {
        Serial.begin(baudRate);
    }
    Serial.setTimeout(50); // Đặt timeout ngắn 50ms giúp phản hồi tức thì không chờ đợi
}

bool SerialConsole::checkCommand(String &outCmd) {
    if (Serial.available() > 0) {
        outCmd = Serial.readString();
        outCmd.trim();
        return outCmd.length() > 0;
    }
    return false;
}
