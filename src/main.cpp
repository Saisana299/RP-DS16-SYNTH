#include <Arduino.h>
#include <I2S.h>
#include <debug.h>

#define DEBUG_MODE 1 //0 or 1

#define SYNTH_ID 1 // 1 or 2
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

#define PIN_I2S_DOUT 20
#define PIN_I2S_BCLK 21
#define PIN_I2S_LRCLK 22
#define BUFFER_SIZE (int32_t)(sampleRate * 0.8)  // 0.8 seconds buffer
I2S i2s(OUTPUT);
const int32_t sampleRate = 48000;
const float VOLUME_GAIN = 2.0f;
const size_t FADE_SAMPLES = 500;
int16_t buffer[BUFFER_SIZE];
uint32_t phase       = 0;
uint32_t phase_delta = 0;

int16_t triangle(uint32_t phase) {
  phase += (1 << 30);
  int16_t value = ((phase < (uint32_t)(1 << 31)) ? (phase >> 16) : ((1 << 16) - (phase >> 16) - 1)) - 16383;
  return constrain(value * VOLUME_GAIN, -32768, 32767);
}

void generate_triangle(int16_t *buffer, size_t size, uint32_t *phase, uint32_t phase_delta) {
  for (size_t i = 0; i < size; i++) {
    buffer[i] = triangle(*phase);
    *phase += phase_delta;
  }
}

void applyFade(int16_t *buffer, size_t size) {
    for (size_t i = 0; i < FADE_SAMPLES && i < size; ++i) {
        float fadeFactor = (float)i / FADE_SAMPLES;
        buffer[i] *= fadeFactor;
        buffer[size - 1 - i] *= fadeFactor;
    }
}

void setup() {
    UART.setTX(TX_PIN);
    UART.setRX(RX_PIN);
    UART.begin(BAUD_RATE);

    DEBUG.init();

    i2s.setBCLK(PIN_I2S_BCLK);
    i2s.setDATA(PIN_I2S_DOUT);
    i2s.setBitsPerSample(16);
    i2s.begin(sampleRate);
    
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    if (UART.available()) {
        String data = UART.readStringUntil('\n');
        
        DEBUG.print("Received from UART: ");
        DEBUG.println(data);

        digitalWrite(LED_BUILTIN, HIGH);

        phase_delta = data.toFloat() * (float)(1ULL << 32) / sampleRate;
        generate_triangle(buffer, BUFFER_SIZE, &phase, phase_delta);
        applyFade(buffer, BUFFER_SIZE);

        for (size_t i = 0; i < BUFFER_SIZE; i++) {
            i2s.write(buffer[i]);  // L
            i2s.write(buffer[i]);  // R
        }

        delay(800);
        digitalWrite(LED_BUILTIN, LOW);
    }
}
