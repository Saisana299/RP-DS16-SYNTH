#include <Arduino.h>
#include <limits.h>
#include <shape.h>

class WaveGenerator {
private:
    struct Note {
        uint32_t phase;
        uint32_t phase_delta;
        bool active;
        uint8_t actnum;
        uint8_t note;
        float gain;
        int16_t fade_in_counter;
        int16_t fade_out_counter;
    };

    static const int MAX_NOTES = 4; // 6音目からおかしくなる
    Note notes[MAX_NOTES];
    float volume_gain = 1.0f;
    const int32_t sample_rate;
    uint8_t preset = 0x00;

    uint8_t bitShift(size_t tableSize) {
        uint8_t shift = 0;
        while (tableSize > 1) {
            tableSize >>= 1;
            shift++;
        }
        return 32 - shift;
    }

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
        if(isActiveNote(note)) return;
        if(note > 127) return;
        if(velocity > 127) return;
        if(velocity == 0) {
            noteOff(note);
            return;
        }

        int8_t noteIndex = getOldNote();
        if(noteIndex == -1) return;
        setFrequency(noteIndex, midiNoteToFrequency(note));
        if(notes[noteIndex].note == 0xff) {
            notes[noteIndex].phase = 0;
            notes[noteIndex].fade_in_counter = 0;
            notes[noteIndex].fade_out_counter = -1;
        }

        notes[noteIndex].note = note;
        notes[noteIndex].gain = (volume_gain / MAX_NOTES) * ((float)velocity / 127.0f);
        notes[noteIndex].actnum = getActiveNote();
        notes[noteIndex].active = true;
    }

    void noteOff(uint8_t note) {
        if(!isActiveNote(note)) return;

        int8_t noteIndex = getNoteIndex(note);
        if(noteIndex == -1) return;
        notes[noteIndex].fade_out_counter = 60;
        notes[noteIndex].fade_in_counter = -1;
        notes[noteIndex].actnum = 0;
        //notes[noteIndex].active = false;
        updateActNum(noteIndex);
    }

    void noteReset() {
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            notes[i].phase = 0;
            notes[i].phase_delta = 0;
            notes[i].active = false;
            notes[i].actnum = 0;
            notes[i].note = 0xff;
            notes[i].gain = 0.0f;
            notes[i].fade_in_counter = -1;
            notes[i].fade_out_counter = -1;
        }
    }

    void generate(int16_t *buffer, size_t size) {
        memset(buffer, 0, sizeof(int16_t) * size); // バッファをクリア

        for (uint8_t n = 0; n < MAX_NOTES; ++n) {
            if (notes[n].active) {
                size_t sampleSize;
                int16_t* waveform;

                switch(preset) {
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

                if (waveform != nullptr) {
                    for (size_t i = 0; i < size; i++) {
                        // フェードインとフェードアウトを適用
                        float fade_gain = 1.0f;
                        if (notes[n].fade_in_counter >= 0 && notes[n].fade_in_counter < 10) {
                            fade_gain = static_cast<float>(notes[n].fade_in_counter) / 10.0f;
                            notes[n].fade_in_counter++;
                        } else if (notes[n].fade_out_counter >= 0) {
                            fade_gain = static_cast<float>(notes[n].fade_out_counter) / 60.0f;
                            if (notes[n].fade_out_counter > 0) notes[n].fade_out_counter--;
                        }

                        int16_t value = waveform[(notes[n].phase >> bitShift(sampleSize)) % sampleSize];
                        buffer[i] += value * fade_gain * notes[n].gain;
                        notes[n].phase += notes[n].phase_delta;
                    }
                }

                if (notes[n].fade_out_counter == 0) {
                    notes[n].fade_out_counter = -1;
                    notes[n].active = false;
                    notes[n].note = 0xff;
                    notes[n].gain = 0.0f;
                }
            }
        }

        // 必要に応じてバッファの正規化
        for (size_t i = 0; i < size; i++) {
            buffer[i] = constrain(buffer[i], INT16_MIN, INT16_MAX);
        }
    }

    void setPreset(uint8_t id) {
        preset = id;
    }
};