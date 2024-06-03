#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>
#include <synth.h>
#include <instruction_set.h>

// SynthIDを選択
#define SYNTH_ID 2 // 2 or 2

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

uint16_t buff_i = 0;
int16_t cshape_buff[2048];

uint8_t response = 0x00;

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

        // 例: {INS_BEGIN, SYNTH_SET_SHAPE, DATA_BEGIN, 0x02, 0x02, 0x01}
        case SYNTH_SET_SHAPE:
            if(bytes < 6) return;
            {
                wave.setShape(receivedData[4], receivedData[5]);
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
                //
            }
            else if(receivedData[4] == 0x01){
                //
            }
            else if(receivedData[4] == 0x02){
                //
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

        // 例: {INS_BEGIN, SYNTH_GET_USED}
        case SYNTH_GET_USED:
            response = wave.getActiveNote();
            break;

        // 例: {INS_BEGIN, SYNTH_IS_NOTE, 0x64}
        case SYNTH_IS_NOTE:
            if(wave.isNote(receivedData[2])) response = 0x01;
            else response = 0x00;
            break;

        // 例: {INS_BEGIN, SYNTH_SET_CSHAPE, DATA_BEGIN, 0x01, 0x02, WAVE_DATA...}
        case SYNTH_SET_CSHAPE:
            if(bytes < 30) return;
            {
                for(uint16_t i = 0; i < 30; i++) {
                    if(i < 6) continue;
                    if(buff_i == 2048) {
                        wave.setCustomShape(cshape_buff, receivedData[5]);
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

        // 例: {INS_BEGIN, SYNTH_SET_VOICE, DATA_BEGIN, 0x02, 0x01, 0x01}
        case SYNTH_SET_VOICE:
            if(bytes < 6) return;
            {
                wave.setVoice(receivedData[4], receivedData[5]);
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_DETUNE, DATA_BEGIN, 0x02, 0xA2, 0x01}
        case SYNTH_SET_DETUNE:
            if(bytes < 6) return;
            {
                wave.setDetune(receivedData[4], receivedData[5]);
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_SPREAD, DATA_BEGIN, 0x02, 0xA2, 0x01}
        case SYNTH_SET_SPREAD:
            if(bytes < 6) return;
            {
                wave.setSpread(receivedData[4], receivedData[5]);
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_LPF, DATA_BEGIN, 0x09, 0x01, 0x22...}
        // 例: {INS_BEGIN, SYNTH_SET_LPF, DATA_BEGIN, 0x09, 0x00}
        case SYNTH_SET_LPF:
            if(bytes < 5) return;
            {
                if(receivedData[4] == 0x01){
                    float freq, q;
                    uint8_t d_freq[] = {receivedData[5], receivedData[6], receivedData[7], receivedData[8]};
                    uint8_t d_q[] = {receivedData[9], receivedData[10], receivedData[11], receivedData[12]};
                    memcpy(&freq, d_freq, sizeof(float));
                    memcpy(&q, d_q, sizeof(float));
                    wave.setLowPassFilter(true, freq, q);
                } else {
                    wave.setLowPassFilter(false);
                }
            }
            break;

        // 例: {INS_BEGIN, SYNTH_SET_HPF, DATA_BEGIN, 0x09, 0x01, 0x22...}
        // 例: {INS_BEGIN, SYNTH_SET_HPF, DATA_BEGIN, 0x09, 0x00}
        case SYNTH_SET_HPF:
            if(bytes < 5) return;
            {
                if(receivedData[4] == 0x01){
                    float freq, q;
                    uint8_t d_freq[] = {receivedData[5], receivedData[6], receivedData[7], receivedData[8]};
                    uint8_t d_q[] = {receivedData[9], receivedData[10], receivedData[11], receivedData[12]};
                    memcpy(&freq, d_freq, sizeof(float));
                    memcpy(&q, d_q, sizeof(float));
                    wave.setHighPassFilter(true, freq, q);
                } else {
                    wave.setHighPassFilter(false);
                }
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
}

void loop() {
    while (1) {
        if (wave.getActiveNote() != 0) {
            static size_t buffer_index = 0;

            isLed = true;
            if (buffer_index == BUFFER_SIZE) {
                wave.generate(buffer_L, buffer_R, BUFFER_SIZE);
                buffer_index = 0;
            }

            while (buffer_index < BUFFER_SIZE) {
                i2s.write(buffer_L[buffer_index]);  // L
                i2s.write(buffer_R[buffer_index]);  // R
                buffer_index++;
            }

        } else {
            isLed = false;
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