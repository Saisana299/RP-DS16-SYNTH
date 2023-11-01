#include <Arduino.h>

class Debug {
private:
    bool initialized = false;
    bool debug_mode;
    SerialUART& debug_serial;
    int TX_PIN, RX_PIN, BAUD_RATE;

public:
    Debug(bool isEnable, SerialUART& serial, int tx, int rx, int baud_rate): debug_serial(serial) {
        debug_mode = isEnable;
        TX_PIN = tx;
        RX_PIN = rx;
        BAUD_RATE = baud_rate;
    }

    void init() {
        if(debug_mode && !initialized){
            debug_serial.setTX(TX_PIN);
            debug_serial.setRX(RX_PIN);
            debug_serial.begin(BAUD_RATE);
        }
        initialized = true;
    }

    void print(const String& message) {
        if (debug_mode && initialized) {
            debug_serial.print(message);
        }
    }

    void println(const String& message) {
        if (debug_mode && initialized) {
            debug_serial.println(message);
        }
    }
};