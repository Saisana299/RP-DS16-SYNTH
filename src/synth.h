#include <Arduino.h>
#include <limits.h>
#include <shape.h>

// todo
// 二音を高速で連続発声させるとattackのサンプルが再生されたままになる現象が起こる(releaseは再生されない)
// 爆音にならない強制リリース処理

class WaveGenerator {
private:
    struct Note {
        uint32_t phase;
        uint32_t phase_delta;
        bool active;
        uint8_t actnum;
        uint8_t note;
        float gain;
        float adsr_gain;
        int32_t attack_counter;
        int32_t release_counter;
    };

    struct NoteCache {
        bool processed;
        uint8_t actnum;
        uint8_t note;
        uint8_t velocity;
    };

    static const int MAX_NOTES = 4;
    Note notes[MAX_NOTES];
    NoteCache cache;

    float volume_gain = 1.0f;
    const int32_t sample_rate;

    // ADSRの定義
    int32_t attack_sample = static_cast<int32_t>((1.0 / 1000.0) * sample_rate);
    int32_t decay_sample = static_cast<int32_t>((0.0 / 1000.0) * sample_rate);
    int16_t sustain_level = 1000;
    int32_t release_sample = static_cast<int32_t>((1.0 / 1000.0) * sample_rate);

    // 基本波形とサンプル定義
    uint8_t shape = 0x00;
    size_t sampleSize = sizeof(sine) / sizeof(sine[0]);
    int16_t* waveform = sine;

    // ビットシフト
    uint8_t bitShift(size_t tableSize) {
        uint8_t shift = 0;
        while (tableSize > 1) {
            tableSize >>= 1;
            shift++;
        }
        return 32 - shift;
    }

    // 初期化
    uint8_t bit_shift = bitShift(sampleSize);

    void setFrequency(int noteIndex, float frequency) {
        if (noteIndex >= 0 && noteIndex < MAX_NOTES) {
            notes[noteIndex].phase_delta = frequency * (float)(1ULL << 32) / sample_rate;
        }
    }

    float midiNoteToFrequency(uint8_t midiNote) {
        return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
    }

    int8_t getOldNote() {
        int8_t index = -1;
        uint8_t min = 0xff;
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            if(getActiveNote() == MAX_NOTES) {
                if(notes[i].actnum < min){
                    min = notes[i].actnum;
                    index = i;
                }
            } else if(notes[i].active == false) {
                index = i;
            }
        }
        return index;
    }

    int8_t getNoteIndex(uint8_t note) {
        int8_t index = -1;
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            if(notes[i].note == note) index = i;
        }
        return index;
    }

    void updateActNum(int noteIndex) {
        if (noteIndex < 0 || noteIndex >= MAX_NOTES) {
            return;
        }
        if (!notes[noteIndex].active) {
            return;
        }
        for (uint8_t i = 0; i < MAX_NOTES; ++i) {
            // ノートがアクティブであり、かつそのactnumが
            // 更新されたノートのactnumより大きい場合、デクリメントする
            if (notes[i].active && notes[i].actnum > notes[noteIndex].actnum) {
                notes[i].actnum--;
            }
        }
    }


    bool isActiveNote(uint8_t note) {
        bool active = false;
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            if(notes[i].note == note && notes[i].active == true){
                active = true;
            }
        }
        return active;
    }

public:
    WaveGenerator(int32_t rate): sample_rate(rate) {
        noteReset();
    }

    uint8_t getActiveNote() {
        uint8_t active = 0;
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            if(notes[i].active == true) active++;
        }
        return active;
    }

    void noteOn(uint8_t note, uint8_t velocity) {
        if(note > 127) return;
        if(velocity > 127) return;
        if(velocity == 0) {
            noteOff(note);
            return;
        }
        if(isActiveNote(note)) {
            noteStop(note);
        }

        int8_t noteIndex = getOldNote();
        if(noteIndex == -1) return;

        if(notes[noteIndex].active) {

            // 強制停止専用release
            float currentAdsrGain;
            if (notes[noteIndex].attack_counter >= 0 && notes[noteIndex].attack_counter < attack_sample) {
                currentAdsrGain = notes[noteIndex].adsr_gain;
            } else {
                currentAdsrGain = 1.0f;
            }
            notes[noteIndex].release_counter = static_cast<int32_t>(currentAdsrGain * release_sample);

            // Cacheに保存
            cache.processed = false;
            cache.note = note;
            cache.actnum = notes[noteIndex].actnum;
            cache.velocity = velocity;
            return;
        }

        setFrequency(noteIndex, midiNoteToFrequency(note));

        // リリース中のアタックは現在の位置から行う
        float currentAdsrGain;
        if(notes[noteIndex].release_counter >= 0) {
            currentAdsrGain = notes[noteIndex].adsr_gain;
        } else {
            currentAdsrGain = 0.0f;
        }
        notes[noteIndex].attack_counter = static_cast<int32_t>(currentAdsrGain * attack_sample);

        if(notes[noteIndex].note == 0xff) {
            notes[noteIndex].phase = 0;
        }

        notes[noteIndex].release_counter = -1;
        notes[noteIndex].note = note;
        notes[noteIndex].gain = (volume_gain / MAX_NOTES) * ((float)velocity / 127.0f);
        notes[noteIndex].actnum = getActiveNote();
        notes[noteIndex].active = true;
    }

    void noteOff(uint8_t note) {
        // cache にある場合は消す
        if(cache.note == note && !cache.processed) {
            cache.processed = true;
            return;
        }

        if(!isActiveNote(note)) return;

        int8_t noteIndex = getNoteIndex(note);
        if(noteIndex == -1) return;

        // アタック中のリリースはnoteOff時のgainから行う
        float currentAdsrGain;
        if (notes[noteIndex].attack_counter >= 0 && notes[noteIndex].attack_counter < attack_sample) {
            currentAdsrGain = notes[noteIndex].adsr_gain;
        } else {
            currentAdsrGain = 1.0f;
        }
        // リリースカウンターを現在のADSRゲインに基づいて調整
        notes[noteIndex].release_counter = static_cast<int32_t>(currentAdsrGain * release_sample);

        notes[noteIndex].attack_counter = -1;
        notes[noteIndex].actnum = 0;
        updateActNum(noteIndex);
    }

    void noteStop(uint8_t note) {
        if(!isActiveNote(note)) return;

        int8_t noteIndex = getNoteIndex(note);
        if(noteIndex == -1) return;

        notes[noteIndex].active = false;
    }

    void noteReset() {
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            notes[i].phase = 0;
            notes[i].phase_delta = 0;
            notes[i].active = false;
            notes[i].actnum = 0;
            notes[i].note = 0xff;
            notes[i].gain = 0.0f;
            notes[i].adsr_gain = 0.0f;
            notes[i].attack_counter = -1;
            notes[i].release_counter = -1;
        }
    }

    void generate(int16_t *buffer, size_t size) {
        memset(buffer, 0, sizeof(int16_t) * size); // バッファをクリア

        for (uint8_t n = 0; n < MAX_NOTES; ++n) {
            if (notes[n].active) {
                if (waveform != nullptr) {
                    for (size_t i = 0; i < size; i++) {
                        // ADSRを適用
                        float adsr_gain = 1.0f;
                        
                        // アタック
                        if (notes[n].attack_counter >= 0 && notes[n].attack_counter < attack_sample) {
                            adsr_gain = static_cast<float>(notes[n].attack_counter) / attack_sample;
                            notes[n].attack_counter++;
                        }
                        // リリース
                        else if (notes[n].release_counter >= 0) {
                            adsr_gain = static_cast<float>(notes[n].release_counter) / release_sample;
                            if (notes[n].release_counter > 0) notes[n].release_counter--;
                        }
                        
                        notes[n].adsr_gain = adsr_gain;

                        int16_t value = waveform[(notes[n].phase >> bit_shift) % sampleSize];
                        buffer[i] += value * adsr_gain * notes[n].gain;
                        notes[n].phase += notes[n].phase_delta;
                    }
                }

                if (notes[n].release_counter == 0) {
                    notes[n].release_counter = -1;
                    notes[n].active = false;
                    notes[n].note = 0xff;
                    notes[n].gain = 0.0f;

                    if(notes[n].actnum == cache.actnum && !cache.processed) {
                        cache.processed = true;
                        noteOn(cache.note, cache.velocity);
                    }
                }
            }
        }

        // 必要に応じてバッファの正規化
        for (size_t i = 0; i < size; i++) {
            buffer[i] = constrain(buffer[i], INT16_MIN, INT16_MAX);
        }
    }

    void setShape(uint8_t id) {
        shape = id;

        switch(shape) {
            case 0x00:
                sampleSize = sizeof(sine) / sizeof(sine[0]);
                waveform = sine;
                break;
            case 0x01:
                sampleSize = sizeof(triangle) / sizeof(triangle[0]);
                waveform = triangle;
                break;
            case 0x02:
                sampleSize = sizeof(saw) / sizeof(saw[0]);
                waveform = saw;
                break;
            case 0x03:
                sampleSize = sizeof(square) / sizeof(square[0]);
                waveform = square;
                break;
        }

        bit_shift = bitShift(sampleSize);
    }

    void setAttack(int16_t attack) {
        attack_sample = static_cast<int32_t>((attack / 1000.0) * sample_rate);
    }

    void setRelease(int16_t release) {
        release_sample = static_cast<int32_t>((release / 1000.0) * sample_rate);
    }
};