#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <debug.h>

#define DEBUG_MODE 0 //0 or 1
#define SYNTH_ID 1 // 1 or 2

#if SYNTH_ID == 1
    #define SDA_PIN 0
    #define SCL_PIN 1
    #define I2C_ADDR 0x08
    TwoWire& I2C = Wire;
    Debug DEBUG(DEBUG_MODE, Serial2, 8, 9, 115200);

#elif SYNTH_ID == 2
    #define SDA_PIN 2
    #define SCL_PIN 3
    #define I2C_ADDR 0x09
    TwoWire& I2C = Wire1;
    Debug DEBUG(DEBUG_MODE, Serial1, 12, 13, 115200);

#endif

char receivedData[32]; // 受信データのためのバッファ
int dataPosition = 0;

#define PIN_I2S_DOUT 20
#define PIN_I2S_BCLK 21
#define PIN_I2S_LRCLK 22
#define BUFFER_SIZE (int32_t)(sampleRate * 0.2)  // 0.8 seconds buffer
I2S i2s(OUTPUT);
const int32_t sampleRate = 48000;
const float VOLUME_GAIN = 2.0f;
const size_t FADE_SAMPLES = 500;
int16_t buffer[BUFFER_SIZE];
uint32_t phase       = 0;
uint32_t phase_delta = 0;

void midiLoop();

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

void receiveEvent(int bytes) {

    dataPosition = 0; // バッファ位置を初期化

    while(I2C.available()) {
        receivedData[dataPosition] = I2C.read();
        dataPosition++;

        if (dataPosition >= sizeof(receivedData) - 1) {
            // バッファの終端に達した場合、ループを終了
            break;
        }
    }
    receivedData[dataPosition] = '\0'; // 文字列の終端を追加
}

void setup() {
    I2C.setSDA(SDA_PIN);
    I2C.setSCL(SCL_PIN);
    I2C.begin(I2C_ADDR);
    I2C.onReceive(receiveEvent);

    DEBUG.init();

    i2s.setBCLK(PIN_I2S_BCLK);
    i2s.setDATA(PIN_I2S_DOUT);
    i2s.setBitsPerSample(16);
    i2s.begin(sampleRate);
    
    pinMode(LED_BUILTIN, OUTPUT);

    multicore_launch_core1(midiLoop);
}

void loop() {
    if (dataPosition > 0) { // データが受信された場合

        DEBUG.print("Received from UART: ");
        DEBUG.println(receivedData);

        digitalWrite(LED_BUILTIN, HIGH);

        phase_delta = (float)atof(receivedData) * (float)(1ULL << 32) / sampleRate;
        generate_triangle(buffer, BUFFER_SIZE, &phase, phase_delta);
        applyFade(buffer, BUFFER_SIZE);

        for (size_t i = 0; i < BUFFER_SIZE; i++) {
            i2s.write(buffer[i]);  // L
            i2s.write(buffer[i]);  // R
        }

        delay(200);
        digitalWrite(LED_BUILTIN, LOW);

        dataPosition = 0; // 受信データ位置をリセット
    }
    delay(10);
}

void midiLoop() {
    while(1) {
        // todo
    }
}
