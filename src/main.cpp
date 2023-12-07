#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <debug.h>
#include <synth.h>
#include <instructionSet.h>

#define SYNTH_ID 1 // 1 or 2

// debug 関連
#define DEBUG_MODE 0 //0 or 1
Debug debug(DEBUG_MODE, Serial2, 8, 9, 115200);

// CTRL 関連
#if SYNTH_ID == 1
    #define I2C_ADDR 0x08
    
#elif SYNTH_ID == 2
    #define I2C_ADDR 0x09

#endif

TwoWire& i2c = Wire;
#define SDA_PIN 0
#define SCL_PIN 1

// DAC 関連
#define PIN_I2S_DOUT 20
#define PIN_I2S_BCLK 21
#define PIN_I2S_LRCLK 22
#define BUFFER_SIZE 256

I2S i2s(OUTPUT);
#define SAMPLE_BITS 16
#define SAMPLE_RATE 48000

// その他
WaveGenerator wave;
int16_t buffer[BUFFER_SIZE];

void loop1();

bool isPlaying = false;
uint8_t lastKeyPressed = 0xff; // 最後に押されたキーの情報を保存する変数

bool isFadeIn = false;
bool isFadeOut = false;

float midiNoteToFrequency(uint8_t midiNote) {
    return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

void receiveEvent(int bytes) {
    // 2バイト以上のみ受け付ける
    if(bytes < 2) return;

    int i = 0;
    uint8_t receivedData[bytes];
    while (i2c.available()) {
        uint8_t received = i2c.read();
        receivedData[i] = received;
        i++;
        if (i >= bytes) {
            break;
        }
    }

    uint8_t instruction = 0x00; // コード種別
    if(receivedData[0] == INS_BEGIN) {
        instruction = receivedData[1];
    }

    switch (instruction)
    {
        // 例: {INS_BEGIN, SYNTH_NOTE_ON, DATA_BEGIN, 0x01, 0x53}
        case SYNTH_NOTE_ON:
            if(bytes < 5) return;
            {
                uint8_t note = receivedData[4];
                lastKeyPressed = note; // 最後に押されたキーの情報を更新
                wave.setFrequency(midiNoteToFrequency(note)); // 周波数を設定
                if(lastKeyPressed == 0xff) {
                    isFadeIn = true; // 無音だった時のみフェードイン
                }
                isPlaying = true; // 音を再生
            }
            break;

        // 例: {INS_BEGIN, SYNTH_NOTE_OFF, DATA_BEGIN, 0x01, 0x53}
        case SYNTH_NOTE_OFF:
            if(bytes < 5) return;
            {
                uint8_t note = receivedData[4];
                if(lastKeyPressed == note) {
                    lastKeyPressed = 0xff; // 最後に押されたキーの情報をリセット
                    isPlaying = false;
                    isFadeOut = true; // フェードアウト後停止
                }
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_PRESET, DATA_BEGIN, 0x01, 0x02}
        case SYNTH_SET_PRESET:
            if(bytes < 5) return;
            {
                wave.setPreset(receivedData[4]);
            }
            break;
    }
}

void setup() {
    i2c.setSDA(SDA_PIN);
    i2c.setSCL(SCL_PIN);
    i2c.begin(I2C_ADDR);
    i2c.onReceive(receiveEvent);

    debug.init();

    i2s.setBCLK(PIN_I2S_BCLK);
    i2s.setDATA(PIN_I2S_DOUT);
    i2s.setBitsPerSample(SAMPLE_BITS);
    i2s.begin(SAMPLE_RATE);
    
    pinMode(LED_BUILTIN, OUTPUT);

    multicore_launch_core1(loop1);
}

void loop() {} // 使用しない

void loop1() {
    while(1) {
        if (isPlaying) {
            static size_t buffer_index = 0;

            digitalWrite(LED_BUILTIN, HIGH);
            if (buffer_index == BUFFER_SIZE) {
                wave.generate(buffer, BUFFER_SIZE);
                buffer_index = 0;
            }

            if (isFadeIn) {
                wave.applyFadeIn(buffer, BUFFER_SIZE);
                isFadeIn = false;
            }

            while (buffer_index < BUFFER_SIZE) {
                i2s.write(buffer[buffer_index]);  // L
                i2s.write(buffer[buffer_index]);  // R
                buffer_index++;
            }

        } else if(isFadeOut) {
            static size_t buffer_index = 0;

            digitalWrite(LED_BUILTIN, HIGH);
            if (buffer_index == BUFFER_SIZE) {
                wave.generate(buffer, BUFFER_SIZE);
                buffer_index = 0;
            }

            if (isFadeOut) {
                wave.applyFadeOut(buffer, BUFFER_SIZE);
            }

            while (buffer_index < BUFFER_SIZE) {
                i2s.write(buffer[buffer_index]);  // L
                i2s.write(buffer[buffer_index]);  // R
                buffer_index++;
            }

            isFadeOut = false;
            wave.resetPhase();

        } else {
            digitalWrite(LED_BUILTIN, LOW);
        }
    }
}
