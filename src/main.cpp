#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <debug.h>
#include <synth.h>
#include <instructionSet.h>

#define SYNTH_ID 1 // 1 or 2

// debug 関連
#define DEBUG_MODE 1 //0 or 1
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
WaveGenerator wave(48000);
int16_t buffer[BUFFER_SIZE];

#if SYNTH_ID == 1
    uint8_t LRMode = LR_PAN_L;
#elif SYNTH_ID == 2
    uint8_t LRMode = LR_PAN_R;
#endif

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
        // 例: {INS_BEGIN, SYNTH_NOTE_ON, DATA_BEGIN, 0x02, 0x53, 0xA5}
        case SYNTH_NOTE_ON:
            if(bytes < 6) return;
            {
                uint8_t note = receivedData[4];
                uint8_t velocity = receivedData[5];
                wave.noteOn(note, velocity);
            }
            break;

        // 例: {INS_BEGIN, SYNTH_NOTE_OFF, DATA_BEGIN, 0x02, 0x53, 0x00}
        case SYNTH_NOTE_OFF:
            if(bytes < 6) return;
            {
                uint8_t note = receivedData[4];
                wave.noteOff(note);
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_SHAPE, DATA_BEGIN, 0x01, 0x02}
        case SYNTH_SET_SHAPE:
            if(bytes < 5) return;
            {
                wave.setShape(receivedData[4]);
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SOUND_STOP}
        case SYNTH_SOUND_STOP:
            wave.noteReset();
            break;

        // 例: {INS_BEGIN, SYNTH_SET_PAN, DATA_BEGIN, 0x01, 0x02}
        case SYNTH_SET_PAN:
            if(bytes < 5) return;
            if(receivedData[4] == 0x00){
                LRMode = LR_PAN_C;
            }
            else if(receivedData[4] == 0x01){
                LRMode = LR_PAN_L;
            }
            else if(receivedData[4] == 0x02){
                LRMode = LR_PAN_R;
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_ATTACK, DATA_BEGIN, 0x01, 0x30}
        case SYNTH_SET_ATTACK:
            if(bytes < 5) return;
            wave.setAttack(255);
            break;

        // 例: {INS_BEGIN, SYNTH_SET_RELEASE, DATA_BEGIN, 0x01, 0x30}
        case SYNTH_SET_RELEASE:
            if(bytes < 5) return;
            wave.setRelease(60);
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
}

void loop() {
    if (wave.getActiveNote() != 0) {
        static size_t buffer_index = 0;

        digitalWrite(LED_BUILTIN, HIGH);
        if (buffer_index == BUFFER_SIZE) {
            wave.generate(buffer, BUFFER_SIZE);
            buffer_index = 0;
        }

        while (buffer_index < BUFFER_SIZE) {
            if(LRMode == LR_PAN_C){
                i2s.write(buffer[buffer_index]);  // L
                i2s.write(buffer[buffer_index]);  // R
            }
            else if(LRMode == LR_PAN_L){
                i2s.write(buffer[buffer_index]);  // L
                i2s.write(static_cast<int16_t>(0));  // R
            }
            else if(LRMode == LR_PAN_R){
                i2s.write(static_cast<int16_t>(0));  // L
                i2s.write(buffer[buffer_index]);  // R
            }
            buffer_index++;
        }

    } else {
        digitalWrite(LED_BUILTIN, LOW);
    }
}
