#include <limits.h>
#include <shape.h>

class WaveGenerator {
private:
    struct Note {
        uint32_t phase;
        uint32_t phase_delta;

        bool active;
        int8_t actnum;

        uint8_t note;
        float gain;
        float adsr_gain;
        float note_off_gain;

        // ADSRはノート毎に設定します
        float level_diff;
        float sustain;
        int32_t attack;
        int32_t decay;
        int32_t release;
        int32_t force_release;

        int32_t attack_cnt;
        int32_t decay_cnt;
        int32_t release_cnt;
        int32_t force_release_cnt;
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

    // 定数
    const int32_t SAMPLE_RATE;
    const size_t SAMPLE_SIZE = 2048;
    const uint8_t BIT_SHIFT = bitShift(SAMPLE_SIZE);

    // ADSRの定義
    float sustain_level = 1.0f;
    float level_diff = 0.0f;
    int32_t attack_sample = static_cast<int32_t>((1.0 * 0.001) * SAMPLE_RATE);
    int32_t decay_sample = static_cast<int32_t>((1000.0 * 0.001) * SAMPLE_RATE);
    int32_t release_sample = static_cast<int32_t>((1.0 * 0.001) * SAMPLE_RATE);

    // 強制リリース
    int32_t force_release_sample = static_cast<int32_t>((1.0 * 0.001) * SAMPLE_RATE);

    // 波形
    int16_t* osc1_wave = sine;
    int16_t* osc2_wave = nullptr;

    // ビットシフト
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
            notes[noteIndex].phase_delta = frequency * (float)(1ULL << 32) / SAMPLE_RATE;
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

    void updateActNumOff(int noteIndex) {
        if (noteIndex < 0 || noteIndex >= MAX_NOTES) {
            return;
        }
        if (!notes[noteIndex].active) {
            return;
        }
        for (uint8_t i = 0; i < MAX_NOTES; ++i) {
            if (notes[i].active && notes[i].actnum > notes[noteIndex].actnum) {
                notes[i].actnum--;
            }
        }
    }

    void updateActNumOn(int noteIndex) {
        if (noteIndex < 0 || noteIndex >= MAX_NOTES) {
            return;
        }
        if (!notes[noteIndex].active) {
            return;
        }
        if (notes[noteIndex].actnum != 3) return;

        for (uint8_t i = 0; i < MAX_NOTES; ++i) {
            if (i == noteIndex) continue;
            if (notes[i].active && notes[i].actnum <= notes[noteIndex].actnum) {
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
    WaveGenerator(int32_t rate): SAMPLE_RATE(rate) {
        noteReset();
        cache.processed = true;
        cache.actnum = 0;
        cache.note = 0;
        cache.velocity = 0;
    }

    uint8_t getActiveNote() {
        uint8_t active = 0;
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            if(notes[i].active == true) active++;
        }
        return active;
    }

    bool isNote(uint8_t note) {
        int8_t i = getNoteIndex(note);
        if(i == -1) return false;
        return true;
    }

    void noteOn(uint8_t note, uint8_t velocity, bool isCache = false) {
        if(note > 127) return;
        if(velocity > 127) return;
        if(velocity == 0) {
            noteOff(note);
            return;
        }
        if(isActiveNote(note)) {
            noteStop(note);
        }

        int8_t i = getOldNote();
        if(i == -1) return;

        if(notes[i].active) {
            if(isCache) return;

            // 強制停止専用release
            notes[i].note_off_gain = notes[i].adsr_gain;
            notes[i].force_release_cnt = force_release_sample;

            // Cacheに保存
            cache.processed = false;
            cache.note = note;
            cache.actnum = notes[i].actnum;
            cache.velocity = velocity;
            return;
        }

        setFrequency(i, midiNoteToFrequency(note));

        notes[i].attack_cnt = 0;

        if(notes[i].note == 0xff) {
            notes[i].phase = 0;
        }

        notes[i].level_diff = level_diff;
        notes[i].sustain = sustain_level;
        notes[i].attack = attack_sample;
        notes[i].decay = decay_sample;
        notes[i].release = release_sample;
        notes[i].force_release = force_release_sample;

        notes[i].release_cnt = -1;
        notes[i].force_release_cnt = -1;
        notes[i].note = note;
        notes[i].gain = (volume_gain / MAX_NOTES) * ((float)velocity / 127.0f);
        notes[i].actnum = getActiveNote();
        notes[i].active = true;
        if(isCache) updateActNumOn(i);
    }

    void noteOff(uint8_t note) {
        // cache にある場合は消す
        if(cache.note == note && !cache.processed) {
            cache.processed = true;
            return;
        }

        if(!isActiveNote(note)) return;

        int8_t i = getNoteIndex(note);
        if(i == -1) return;

        // リリースはnoteOff時のgainから行う
        notes[i].note_off_gain = notes[i].adsr_gain;
        notes[i].release_cnt = release_sample;

        notes[i].attack_cnt = -1;
        notes[i].decay_cnt = -1;
        notes[i].actnum = -1;
        updateActNumOff(i);
   }

    void noteStop(uint8_t note) {
        if(!isActiveNote(note)) return;

        int8_t i = getNoteIndex(note);
        if(i == -1) return;

        notes[i].active = false;
    }

    void noteReset() {
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            notes[i].phase = 0;
            notes[i].phase_delta = 0;
            notes[i].active = false;
            notes[i].actnum = -1;
            notes[i].note = 0xff;
            notes[i].gain = 0.0f;
            notes[i].adsr_gain = 0.0f;
            notes[i].note_off_gain = 0.0f;
            notes[i].attack_cnt = -1;
            notes[i].decay_cnt = -1;
            notes[i].release_cnt = -1;
            notes[i].force_release_cnt = -1;

            notes[i].level_diff = level_diff;
            notes[i].sustain = sustain_level;
            notes[i].attack = attack_sample;
            notes[i].decay = decay_sample;
            notes[i].release = release_sample;
        }
    }

    float applyEnvelope(uint8_t n) {

        // 基本レベル
        float adsr_gain = 0.0f;
        
        // アタック
        if (notes[n].attack_cnt >= 0 && notes[n].attack_cnt < notes[n].attack) {
            adsr_gain = static_cast<float>(notes[n].attack_cnt) / notes[n].attack;
            notes[n].attack_cnt++;
        }
        // 強制リリース
        else if (notes[n].force_release_cnt >= 0) {
            adsr_gain = notes[n].note_off_gain * (static_cast<float>(notes[n].force_release_cnt) / notes[n].force_release);
            if (notes[n].force_release_cnt > 0) notes[n].force_release_cnt--;
        }
        // リリース
        else if (notes[n].release_cnt >= 0) {
            adsr_gain = notes[n].note_off_gain * (static_cast<float>(notes[n].release_cnt) / notes[n].release);
            if (notes[n].release_cnt > 0) notes[n].release_cnt--;
        }
        // ディケイ
        else if (notes[n].decay_cnt >= 0) {
            adsr_gain = notes[n].sustain + (notes[n].level_diff * (static_cast<float>(notes[n].decay_cnt) / notes[n].decay));
            if (notes[n].decay_cnt > 0) notes[n].decay_cnt--;
        }
        // サステイン
        else {
            adsr_gain = notes[n].sustain;
        }
        
        notes[n].adsr_gain = adsr_gain;

        return adsr_gain;
    }

    void generate(int16_t *buffer, size_t size) {
        memset(buffer, 0, sizeof(int16_t) * size); // バッファをクリア

        for (uint8_t n = 0; n < MAX_NOTES; ++n) {
            if (!notes[n].active) continue;
            
            if (osc1_wave != nullptr) {
                for (size_t i = 0; i < size; i++) {

                    // OSC1適用
                    int16_t osc1 = osc1_wave[(notes[n].phase >> BIT_SHIFT) % SAMPLE_SIZE];
                    buffer[i] += osc1;

                    // OSC2適用
                    if(osc2_wave != nullptr) {
                        int16_t osc2 = osc2_wave[(notes[n].phase >> BIT_SHIFT) % SAMPLE_SIZE];
                        buffer[i] += osc2;
                    }

                    // ADSRゲイン適用
                    float adsr_gain = applyEnvelope(n);
                    buffer[i] *= adsr_gain;

                    // ノートゲイン適用
                    buffer[i] *= notes[n].gain;
                    
                    // ピッチ適用
                    notes[n].phase += notes[n].phase_delta;
                }
            }

            // アタック終了したらディケイへ
            if (notes[n].attack_cnt >= notes[n].attack) {
                notes[n].attack_cnt = -1;
                notes[n].decay_cnt = notes[n].decay;
            }

            // リリースが終了
            else if (notes[n].release_cnt == 0 || notes[n].force_release_cnt == 0) {
                notes[n].release_cnt = -1;
                notes[n].active = false;
                notes[n].note = 0xff;
                notes[n].gain = 0.0f;

                if(notes[n].actnum == cache.actnum && !cache.processed) {
                    cache.processed = true;
                    noteOn(cache.note, cache.velocity, true);
                }
            }

            // ディケイが終了
            else if (notes[n].decay_cnt == 0) {
                notes[n].decay_cnt = -1;
            }
        }

        // 必要に応じてバッファの正規化
        for (size_t i = 0; i < size; i++) {
            buffer[i] = constrain(buffer[i], INT16_MIN, INT16_MAX);
        }
    }

    void setShape(uint8_t id, uint8_t osc) {
        switch(id) {
            case 0x00:
                osc1_wave = sine;
                break;
            case 0x01:
                osc1_wave = triangle;
                break;
            case 0x02:
                osc1_wave = saw;
                break;
            case 0x03:
                osc1_wave = square;
                break;
        }
    }

    void setCustomShape(uint8_t osc, int16_t *samples) {
        osc1_wave = samples;
    }

    void setAttack(int16_t attack) {
        attack_sample = static_cast<int32_t>((attack * 0.001) * SAMPLE_RATE);
    }

    void setRelease(int16_t release) {
        release_sample = static_cast<int32_t>((release * 0.001) * SAMPLE_RATE);
    }

    void setDecay(int16_t decay) {
        decay_sample = static_cast<int32_t>((decay * 0.001) * SAMPLE_RATE);
    }

    void setSustain(int16_t sustain) {
        sustain_level = sustain * 0.001;
        level_diff = 1.0f - sustain_level;
    }
};