#include <Arduino.h>

class Debug {
private:
    bool debug_mode;
    SerialUART& debug_serial;

public:
    Debug(bool isEnable, SerialUART& serial, int tx, int rx, int baud_rate): debug_serial(serial) {
        debug_mode = isEnable;

        if(debug_mode){
            debug_serial.setTX(tx);
            debug_serial.setRX(rx);
            debug_serial.begin(baud_rate);
        }
    }

    void print(const String& message) {
        if (debug_mode) {
            debug_serial.print(message);
        }
    }

    void println(const String& message) {
        if (debug_mode) {
            debug_serial.println(message);
        }
    }
};