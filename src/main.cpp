#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <debug.h>
#include <synth.h>

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

char receivedData[32]; // 受信データのためのバッファ
int dataPosition = 0;

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
int lastKeyPressed = 0; // 最後に押されたキーの情報を保存する変数

bool isFadeIn = false;
bool isFadeOut = false;

float midiNoteToFrequency(int midiNote) {
    return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

void receiveEvent(int bytes) {

    dataPosition = 0; // バッファ位置を初期化

    while(i2c.available()) {
        receivedData[dataPosition] = i2c.read();
        dataPosition++;

        if (dataPosition >= sizeof(receivedData) - 1) {
            // バッファの終端に達した場合、ループを終了
            break;
        }
    }
    receivedData[dataPosition] = '\0'; // 文字列の終端を追加

    int receivedInt = atoi(receivedData);

    if(lastKeyPressed == receivedInt-10000) {
        lastKeyPressed = 0; // 最後に押されたキーの情報をリセット
        isPlaying = false;
        isFadeOut = true; // フェードアウト後停止
        
    } else {
        // 受信された周波数が1万Hzを超える場合、処理を終了
        if (receivedInt > 10000) {
            return;
        }

        lastKeyPressed = receivedInt; // 最後に押されたキーの情報を更新
        wave.setFrequency(midiNoteToFrequency(receivedInt)); // 周波数を設定
        if(lastKeyPressed == 0) {
            isFadeIn = true; // 無音だった時のみフェードイン
        }
        isPlaying = true; // 音を再生
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

void loop() {
    if (isPlaying) {
        static size_t buffer_index = 0;

        digitalWrite(LED_BUILTIN, HIGH);
        if (buffer_index == BUFFER_SIZE) {
            wave.generate(buffer, BUFFER_SIZE);
            buffer_index = 0;
        }

        if (isFadeIn) {
            wave.applyFadeIn(buffer, BUFFER_SIZE, 10);
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
            wave.applyFadeOut(buffer, BUFFER_SIZE, 60);
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

void loop1() {
    while(1) {
        // todo
    }
}
