#include <Arduino.h>
#include <debug.h>

#define DEBUG_MODE 1 //0 or 1

#define SYNTH_ID 2 // 1 or 2
#define BAUD_RATE 115200

#if SYNTH_ID == 1
    #define TX_PIN 0
    #define RX_PIN 1
    SerialUART& UART = Serial1;
    Debug DEBUG(DEBUG_MODE, Serial2, 8, 9, BAUD_RATE);

#elif SYNTH_ID == 2
    #define TX_PIN 4
    #define RX_PIN 5
    SerialUART& UART = Serial2;
    Debug DEBUG(DEBUG_MODE, Serial1, 12, 13, BAUD_RATE);

#endif

void setup() {
    UART.setTX(TX_PIN);
    UART.setRX(RX_PIN);
    UART.begin(BAUD_RATE);

    DEBUG.init();
    
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
