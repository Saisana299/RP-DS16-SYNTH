#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <synth.h>
#include <instruction_set.h>
#include <ring_buffer.h>

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
int16_t buffer_L[BUFFER_SIZE];
int16_t buffer_R[BUFFER_SIZE];

bool isLed = false;
uint32_t* delay_long;
uint32_t remain = 0;

uint16_t buff_i = 0;
int16_t cshape_buff[2048];

uint8_t response = 0x00;

void receiveEvent(int bytes) {
    // 1バイト以上のみ受け付ける
    if(bytes < 1) return;

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

    uint8_t instruction = receivedData[0];

    switch (instruction)
    {
        // 例: {SYNTH_NOTE_ON, <note>, <velocity>}
        case SYNTH_NOTE_ON:
            if(bytes < 3) return;
            {
                uint8_t note = receivedData[1];
                uint8_t velocity = receivedData[2];
                wave.noteOn(note, velocity);
            }
            break;

        // 例: {SYNTH_NOTE_OFF, <note>, <velocity>}
        case SYNTH_NOTE_OFF:
            if(bytes < 3) return;
            {
                uint8_t note = receivedData[1];
                wave.noteOff(note);
            }
            break;

        // 例: {SYNTH_SET_SHAPE, <id>, <osc>}
        case SYNTH_SET_SHAPE:
            if(bytes < 3) return;
            {
                wave.setShape(receivedData[1], receivedData[2]);
            }
            break;

        // 例: {SYNTH_SOUND_STOP}
        case SYNTH_SOUND_STOP:
            wave.noteReset();
            break;

        // 例: {SYNTH_SET_PAN, <pan>}
        case SYNTH_SET_PAN:
            if(bytes < 2) return;
            wave.setAmpPan(receivedData[1]);
            break;

        // 例: {SYNTH_SET_ATTACK, 0x00, 0x30, 0x00, 0x00, 0x00}
        case SYNTH_SET_ATTACK:
        case SYNTH_SET_DECAY:
        case SYNTH_SET_RELEASE:
            if(bytes < 6) return;
            {
                int16_t data = 0;
                data += receivedData[1] * 1000;
                data += receivedData[2];
                data += receivedData[3];
                data += receivedData[4];
                data += receivedData[5];
                if(instruction == SYNTH_SET_ATTACK) {
                    wave.setAttack(data);
                }
                else if(instruction == SYNTH_SET_DECAY) {
                    wave.setDecay(data);
                }
                else if(instruction == SYNTH_SET_RELEASE) {
                    wave.setRelease(data);
                }
            }
            break;

        // 例: {SYNTH_SET_SUSTAIN, 0x30, 0x00, 0x00, 0x00}
        case SYNTH_SET_SUSTAIN:
            if(bytes < 5) return;
            {
                int16_t sustain = 0;
                sustain += receivedData[1];
                sustain += receivedData[2];
                sustain += receivedData[3];
                sustain += receivedData[4];
                wave.setSustain(sustain);
            }
            break;

        // 例: {SYNTH_GET_USED}
        case SYNTH_GET_USED:
            response = wave.getActiveNote();
            break;

        // 例: {SYNTH_IS_NOTE, 0x64}
        case SYNTH_IS_NOTE:
            if(wave.isNote(receivedData[1])) response = 0x01;
            else response = 0x00;
            break;

        // 例: {SYNTH_SET_CSHAPE, 0x01, 0x02, WAVE_DATA...}
        case SYNTH_SET_CSHAPE:
            if(bytes < 27) return;
            {
                for(uint16_t i = 0; i < 27; i++) {
                    if(i < 3) continue;
                    if(buff_i == 2048) {
                        wave.setCustomShape(cshape_buff, receivedData[2]);
                        buff_i = 0;
                        break;
                    }
                    if(buff_i == 0) memset(cshape_buff, 0, 2048 * sizeof(int16_t));
                    cshape_buff[buff_i] = static_cast<int16_t>((receivedData[i+1] << 8) | receivedData[i]);
                    i++;
                    buff_i++;
                }
            }
            break;

        // 例: {SYNTH_SET_VOICE, 0x01, 0x01}
        case SYNTH_SET_VOICE:
            if(bytes < 3) return;
            {
                wave.setVoice(receivedData[1], receivedData[2]);
            }
            break;

        // 例: {SYNTH_SET_DETUNE, 0xA2, 0x01}
        case SYNTH_SET_DETUNE:
            if(bytes < 3) return;
            {
                wave.setDetune(receivedData[1], receivedData[2]);
            }
            break;

        // 例: {SYNTH_SET_SPREAD, 0xA2, 0x01}
        case SYNTH_SET_SPREAD:
            if(bytes < 3) return;
            {
                wave.setSpread(receivedData[1], receivedData[2]);
            }
            break;

        // 例: {SYNTH_SET_LPF, 0x01, 0x22...}
        // 例: {SYNTH_SET_LPF, 0x00}
        case SYNTH_SET_LPF:
            if(bytes < 2) return;
            {
                if(receivedData[1] == 0x01){
                    float freq, q;
                    uint8_t d_freq[] = {receivedData[2], receivedData[3], receivedData[4], receivedData[5]};
                    uint8_t d_q[] = {receivedData[6], receivedData[7], receivedData[8], receivedData[9]};
                    memcpy(&freq, d_freq, sizeof(float));
                    memcpy(&q, d_q, sizeof(float));
                    wave.setLowPassFilter(true, freq, q);
                } else {
                    wave.setLowPassFilter(false);
                }
            }
            break;

        // 例: {SYNTH_SET_HPF, 0x01, 0x22...}
        // 例: {SYNTH_SET_HPF, 0x00}
        case SYNTH_SET_HPF:
            if(bytes < 2) return;
            {
                if(receivedData[1] == 0x01){
                    float freq, q;
                    uint8_t d_freq[] = {receivedData[2], receivedData[3], receivedData[4], receivedData[5]};
                    uint8_t d_q[] = {receivedData[6], receivedData[7], receivedData[8], receivedData[9]};
                    memcpy(&freq, d_freq, sizeof(float));
                    memcpy(&q, d_q, sizeof(float));
                    wave.setHighPassFilter(true, freq, q);
                } else {
                    wave.setHighPassFilter(false);
                }
            }
            break;

        // 例: {SYNTH_SET_OSC_LVL, <OSC>, <HB_Level>, <LB_Level>}
        case SYNTH_SET_OSC_LVL:
            if(bytes < 4) return;
            {
                wave.setOscLevel(receivedData[1], (receivedData[2] << 8) | receivedData[3]);
            }
            break;

        // 例: {SYNTH_SET_OCT, <osc>, <octave>}
        case SYNTH_SET_OCT:
            if(bytes < 3) return;
            {
                wave.setOscOctave(receivedData[1], static_cast<int8_t>(receivedData[2]));
            }
            break;

        // 例: {SYNTH_SET_SEMI, <osc>, <semitone>}
        case SYNTH_SET_SEMI:
            if(bytes < 3) return;
            {
                wave.setOscSemitone(receivedData[1], static_cast<int8_t>(receivedData[2]));
            }
            break;

        // 例: {SYNTH_SET_CENT, <osc>, <cent>}
        case SYNTH_SET_CENT:
            if(bytes < 3) return;
            {
                wave.setOscCent(receivedData[1], static_cast<int8_t>(receivedData[2]));
            }
            break;

        // 例: {SYNTH_SET_LEVEL, <HB_level>, <LB_level>}
        case SYNTH_SET_LEVEL:
            if(bytes < 3) return;
            {
                wave.setAmpLevel((receivedData[1] << 8) | receivedData[2]);
            }
            break;

        // 例: {SYNTH_SET_DELAY, <true|false>, <HB_time>, <LB_time>, <HB_level>, <LB_level>, <HB_feedback>, <LB_feedback>}
        case SYNTH_SET_DELAY:
            if(bytes < 8) return;
            {
                if(receivedData[1] == 0x01) {
                    int16_t time = static_cast<int16_t>((receivedData[2] << 8) | receivedData[3]);
                    int16_t level = static_cast<int16_t>((receivedData[4] << 8) | receivedData[5]);
                    int16_t feedback = static_cast<int16_t>((receivedData[6] << 8) | receivedData[7]);
                    wave.setDelay(true, time, level, feedback);
                }
                else {
                    wave.setDelay(false);
                }
            }
            break;

        // 例: {SYNTH_SET_MOD, <mod>}
        case SYNTH_SET_MOD:
            if(bytes < 2) return;
            {
                wave.setMod(receivedData[1]);
            }
            break;
    }
}

void requestEvent() {
    i2c.write(response);
    response = 0x00;
}

void setup() {

    i2c.setSDA(SDA_PIN);
    i2c.setSCL(SCL_PIN);
    i2c.begin(I2C_ADDR);
    i2c.setClock(1000000);
    i2c.onReceive(receiveEvent);
    i2c.onRequest(requestEvent);

    // DebugPin
    Serial2.setTX(8);
    Serial2.setRX(9);
    Serial2.begin(115200);

    i2s.setBCLK(PIN_I2S_BCLK);
    i2s.setDATA(PIN_I2S_DOUT);
    i2s.setBitsPerSample(SAMPLE_BITS);
    i2s.begin(SAMPLE_RATE);

    pinMode(LED_BUILTIN, OUTPUT);

    delay_long = wave.getDelayLong();
}

void loop() {
    while (1) {
        if (wave.getActiveNote() != 0) {
            static size_t buffer_index = 0;

            isLed = true;
            remain = *delay_long;
            if (buffer_index == BUFFER_SIZE) {
                // /*debug*/ unsigned long startTime = micros();
                wave.generate(buffer_L, buffer_R, BUFFER_SIZE); // 目標: 5ミリ秒以内に完了する
                // /*debug*/ unsigned long endTime = micros();
                // /*debug*/ unsigned long duration = endTime - startTime;
                // /*debug*/ Serial2.print(":");
                // /*debug*/ Serial2.print(duration);
                // /*debug*/ Serial2.println("us");
                buffer_index = 0;
            }

            while (buffer_index < BUFFER_SIZE) {
                i2s.write(buffer_L[buffer_index]);  // L
                i2s.write(buffer_R[buffer_index]);  // R
                buffer_index++;
            }

        } else {
            // ディレイが残っている場合の処理
            if(wave.isDelayEnabled() && remain > 0) {
                int16_t remain_L = wave.delayProcess(0, 0x00);
                int16_t remain_R = wave.delayProcess(0, 0x01);
                i2s.write(remain_L); // L
                i2s.write(remain_R); // R
                remain--;
            } else {
                isLed = false;
            }
        }
    }
}

void loop1() {
    while(1) {
        if(isLed) {
            gpio_put(LED_BUILTIN, HIGH);
        } else {
            gpio_put(LED_BUILTIN, LOW);
        }
        wave.calculate();
    }
}