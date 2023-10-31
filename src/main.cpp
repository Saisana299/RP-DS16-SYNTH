#include <Arduino.h>

#define SYNTH_ID 2 // 1 or 2

#define BAUD_RATE 115200

#if SYNTH_ID == 1
    #define TX_PIN 0
    #define RX_PIN 1
    SerialUART& UART = Serial1;
    #define DEBUG_TX 8
    #define DEBUG_RX 9
    SerialUART& DEBUG = Serial2;

#elif SYNTH_ID == 2
    #define TX_PIN 4
    #define RX_PIN 5
    SerialUART& UART = Serial2;
    #define DEBUG_TX 12
    #define DEBUG_RX 13
    SerialUART& DEBUG = Serial1;

#endif

void setup() {
    UART.setTX(TX_PIN);
    UART.setRX(RX_PIN);
    UART.begin(BAUD_RATE);

    DEBUG.setTX(DEBUG_TX);
    DEBUG.setRX(DEBUG_RX);
    DEBUG.begin(BAUD_RATE);
    
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    if (UART.available()) {
        String data = UART.readStringUntil('\n');
        
        DEBUG.print("Received from UART: ");
        DEBUG.println(data);

        digitalWrite(LED_BUILTIN, HIGH);
        delay(500);
        digitalWrite(LED_BUILTIN, LOW);
    }
}
