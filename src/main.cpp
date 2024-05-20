#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <synth.h>
#include <instruction_set.h>
#include <wokwi.h>

// SynthIDを選択
#define SYNTH_ID 1 // 1 or 2

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
uint8_t LRMode = LR_PAN_C;
bool isLed = false;

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
                wave.setShape(receivedData[4], 0x01);
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

        // 例: {INS_BEGIN, SYNTH_SET_ATTACK, DATA_BEGIN, 0x05, 0x00, 0x30, 0x00, 0x00, 0x00}
        case SYNTH_SET_ATTACK:
        case SYNTH_SET_DECAY:
        case SYNTH_SET_RELEASE:
            if(bytes < 9) return;
            {
                int16_t data = 0;
                data += receivedData[4] * 1000;
                data += receivedData[5];
                data += receivedData[6];
                data += receivedData[7];
                data += receivedData[8];
                if(receivedData[1] == SYNTH_SET_ATTACK) {
                    wave.setAttack(data);
                }
                else if(receivedData[1] == SYNTH_SET_DECAY) {
                    wave.setDecay(data);
                }
                else if(receivedData[1] == SYNTH_SET_RELEASE) {
                    wave.setRelease(data);
                }
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_SUSTAIN, DATA_BEGIN, 0x04, 0x30, 0x00, 0x00, 0x00}
        case SYNTH_SET_SUSTAIN:
            if(bytes < 8) return;
            {
                int16_t sustain = 0;
                sustain += receivedData[4];
                sustain += receivedData[5];
                sustain += receivedData[6];
                sustain += receivedData[7];
                wave.setSustain(sustain);
            }
            break;
    }
}

void setup() {
    i2c.setSDA(SDA_PIN);
    i2c.setSCL(SCL_PIN);
    i2c.begin(I2C_ADDR);
    i2c.setClock(1000000);
    i2c.onReceive(receiveEvent);

    // DebugPin
    Serial2.setTX(8);
    Serial2.setRX(9);
    Serial2.begin(1000000);

    i2s.setBCLK(PIN_I2S_BCLK);
    i2s.setDATA(PIN_I2S_DOUT);
    i2s.setBitsPerSample(SAMPLE_BITS);
    i2s.begin(SAMPLE_RATE);
    
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    while (1) {

        #if WOKWI_MODE == 1
            delay(10);
            if(isLed) {
                digitalWrite(LED_BUILTIN, HIGH);
            } else {
                digitalWrite(LED_BUILTIN, LOW);
            }
        #endif

        if (wave.getActiveNote() != 0) {
            static size_t buffer_index = 0;

            isLed = true;
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
            isLed = false;
        }
    }
}

#if WOKWI_MODE != 1
void loop1() {
    while (1) {
        if(isLed) {
            digitalWrite(LED_BUILTIN, HIGH);
        } else {
            digitalWrite(LED_BUILTIN, LOW);
        }
    }
}
#endif