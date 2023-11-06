#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <debug.h>

#define DEBUG_MODE 0 //0 or 1
#define SYNTH_ID 1 // 1 or 2

#define SDA_PIN 0
#define SCL_PIN 1

TwoWire& I2C = Wire;
Debug DEBUG(DEBUG_MODE, Serial2, 8, 9, 115200);

#if SYNTH_ID == 1
    #define I2C_ADDR 0x08
    
#elif SYNTH_ID == 2
    #define I2C_ADDR 0x09

#endif

char receivedData[32]; // 受信データのためのバッファ
int dataPosition = 0;

#define PIN_I2S_DOUT 20
#define PIN_I2S_BCLK 21
#define PIN_I2S_LRCLK 22
#define BUFFER_SIZE 256

I2S i2s(OUTPUT);
const int32_t sampleRate = 48000;
const float VOLUME_GAIN = 2.0f;
const size_t FADE_SAMPLES = 500;
int16_t buffer[BUFFER_SIZE];
uint32_t phase       = 0;
uint32_t phase_delta = 0;

void midiLoop();

bool isPlaying = false;
int lastKeyPressed = 0; // 最後に押されたキーの情報を保存する変数

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

float midiNoteToFrequency(int midiNote) {
    return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
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

    int receivedInt = atoi(receivedData);

    if(lastKeyPressed == receivedInt-10000) {
        isPlaying = false; // 最後に押されたキーのみで音を停止
        lastKeyPressed = 0; // 最後に押されたキーの情報をリセット
    } else {
        // 受信された周波数が1万Hzを超える場合、処理を終了
        if (receivedInt > 10000) {
            return;
        }

        lastKeyPressed = receivedInt; // 最後に押されたキーの情報を更新
        phase_delta = midiNoteToFrequency(receivedInt) * (float)(1ULL << 32) / sampleRate;  // 周波数を計算
        isPlaying = true; // 音を再生
    }
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
    if (isPlaying) {
        static size_t buffer_index = 0;

        digitalWrite(LED_BUILTIN, HIGH);
        if (buffer_index == BUFFER_SIZE) {
            generate_triangle(buffer, BUFFER_SIZE, &phase, phase_delta);
            buffer_index = 0;
        }

        while (buffer_index < BUFFER_SIZE) {
            i2s.write(buffer[buffer_index]);  // L
            i2s.write(buffer[buffer_index]);  // R
            buffer_index++;
        }
    }else{
        digitalWrite(LED_BUILTIN, LOW);
    }
}

void midiLoop() {
    while(1) {
        // todo
    }
}
