#include <limits.h>
#include <shape.h>

class WaveGenerator {
private:

    // 定数
    static const int MAX_NOTES = 4;
    static const int MAX_VOICE = 16;
    static const size_t SAMPLE_SIZE = 2048;
    const int32_t SAMPLE_RATE;
    const uint8_t BIT_SHIFT = bitShift(SAMPLE_SIZE);
    const double WHOLETONE = pow(2.0, 2.0 / 12.0) - 1.0;

    struct Note {
        uint32_t osc1_phase[MAX_VOICE];
        uint32_t osc2_phase[MAX_VOICE];
        uint32_t osc1_phase_delta[MAX_VOICE];
        uint32_t osc2_phase_delta[MAX_VOICE];

        bool active;
        int8_t actnum;

        uint8_t note;
        int32_t gain;
        int32_t adsr_gain;
        int32_t note_off_gain;

        // ADSRはノート毎に設定します
        int32_t level_diff;
        int32_t sustain;
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
        uint8_t note;
        uint8_t velocity;
    };

    Note notes[MAX_NOTES];
    NoteCache cache[MAX_NOTES];

    int16_t volume_gain = 1000; // 1.0 = 100

    // ADSRの定義
    int16_t sustain_level = 1000; // 1.0 = 1000
    int16_t level_diff = 0; // 1.0 = 1000
    int32_t attack_sample = (1 * SAMPLE_RATE) / 1000;
    int32_t decay_sample = (1000 * SAMPLE_RATE) / 1000;
    int32_t release_sample = (1 * SAMPLE_RATE) / 1000;

    // 強制リリース
    int32_t force_release_sample = (1 * SAMPLE_RATE) / 1000;

    // 波形
    int16_t* osc1_wave = sine;
    int16_t* osc2_wave = nullptr;
    int16_t osc1_cwave[SAMPLE_SIZE];
    int16_t osc2_cwave[SAMPLE_SIZE];

    // OSCパラメータ
    uint8_t osc1_voice = 7; // MAX16
    uint8_t osc2_voice = 1;
    float  osc1_detune = 0.2f;
    float  osc2_detune = 0.2f;

    // ビットシフト
    uint8_t bitShift(size_t tableSize) {
        uint8_t shift = 0;
        while (tableSize > 1) {
            tableSize >>= 1;
            shift++;
        }
        return 32 - shift;
    }

    double lerp(double a, double b, double t) {
        return a + t * (b - a);
    }

    void setFrequency(int noteIndex, float frequency) {
        if (noteIndex >= 0 && noteIndex < MAX_NOTES) {
            // osc1処理
            if(osc1_voice == 1) {
                notes[noteIndex].osc1_phase_delta[0] = frequency * (float)(1ULL << 32) / SAMPLE_RATE;
            }
            else {
                for(uint8_t d = 0; d < osc1_voice; d++) {
                    const auto pos = lerp(-1.0, 1.0, 1.0 * d / (osc1_voice - 1));
                    float detuneFactor = static_cast<float>(1.0 + WHOLETONE * osc1_detune * pos);
                    notes[noteIndex].osc1_phase_delta[d] = (frequency * detuneFactor) * (float)(1ULL << 32) / SAMPLE_RATE;
                }
            }
            // osc2処理
            if(osc2_voice == 1) {
                notes[noteIndex].osc2_phase_delta[0] = frequency * (float)(1ULL << 32) / SAMPLE_RATE;
            }
            else {
                for(uint8_t d = 0; d < osc2_voice; d++) {
                    const auto pos = lerp(-1.0, 1.0, 1.0 * d / (osc2_voice - 1));
                    float detuneFactor = static_cast<float>(1.0 + WHOLETONE * osc2_detune * pos);
                    notes[noteIndex].osc2_phase_delta[d] = (frequency * detuneFactor) * (float)(1ULL << 32) / SAMPLE_RATE;
                }
            }
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
                if(notes[i].actnum > -1) notes[i].actnum--;
            }
        }
    }

    void updateActNumOff(int noteIndex) {
        if (noteIndex < 0 || noteIndex >= MAX_NOTES) {
            return;
        }
        for (uint8_t i = 0; i < MAX_NOTES; ++i) {
            if (notes[i].actnum > notes[noteIndex].actnum) {
                if(notes[i].actnum > -1) notes[i].actnum--;
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

    void resetPhase(int8_t noteIndex) {
        for(uint8_t i = 0; i < MAX_VOICE; i++) {
            notes[noteIndex].osc1_phase[i] = rand();
            notes[noteIndex].osc2_phase[i] = rand();
        }
    }

    void resetPhaseDelta(int8_t noteIndex) {
        for(uint8_t i = 0; i < MAX_VOICE; i++) {
            notes[noteIndex].osc1_phase_delta[i] = 0;
            notes[noteIndex].osc2_phase_delta[i] = 0;
        }
    }

public:
    WaveGenerator(int32_t rate): SAMPLE_RATE(rate) {
        noteReset();
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            cache[i].processed = true;
            cache[i].note = 0;
            cache[i].velocity = 0;
        }
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

    void noteOn(uint8_t note, uint8_t velocity, bool isCache = false, uint8_t cacheIndex = 0) {
        if(note > 127) return;
        if(velocity > 127) return;
        if(velocity == 0) {
            noteOff(note);
            return;
        }

        int8_t i = getOldNote();
        if(isActiveNote(note)) {
            i = getNoteIndex(note);
        }
        if(isCache) i = cacheIndex;
        if(i == -1) return;

        if(notes[i].active && !isCache) {

            // 強制停止専用release
            notes[i].note_off_gain = notes[i].adsr_gain;
            notes[i].force_release_cnt = force_release_sample;
            notes[i].attack_cnt = -1;
            notes[i].decay_cnt = -1;

            // Cacheに保存
            cache[i].processed = false;
            cache[i].note = note;
            cache[i].velocity = velocity;

            return;
        }

        setFrequency(i, midiNoteToFrequency(note));

        notes[i].attack_cnt = 0;

        if(notes[i].note == 0xff) {
            resetPhase(i);
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
        notes[i].gain = ((volume_gain / MAX_NOTES) * ((velocity * 1000) / 127)) / 1000;

        if(isCache) updateActNumOn(i);

        // actnumの更新
        int8_t newActNum = -1;
        for(uint8_t j = 0; j < MAX_NOTES; j++) {
            if(notes[j].actnum > newActNum) newActNum = notes[j].actnum;
        }
        newActNum++;
        if(newActNum >= 4) newActNum = 3;
        else if(newActNum == -1) newActNum = 0;
        notes[i].actnum = newActNum;

        notes[i].active = true;
    }

    void noteOff(uint8_t note) {
        // cache にある場合は消す
        for(uint8_t n = 0; n < MAX_NOTES; n++) {
            if(cache[n].note == note && !cache[n].processed) {
                cache[n].processed = true;
            }
        }

        if(!isActiveNote(note)) return;

        int8_t i = getNoteIndex(note);
        if(i == -1) return;

        // リリースはnoteOff時のgainから行う
        notes[i].note_off_gain = notes[i].adsr_gain;
        notes[i].release_cnt = release_sample;

        notes[i].attack_cnt = -1;
        notes[i].decay_cnt = -1;
   }

    void noteReset() {
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            resetPhase(i);
            resetPhaseDelta(i);
            notes[i].active = false;
            notes[i].actnum = -1;
            notes[i].note = 0xff;
            notes[i].gain = 0;
            notes[i].adsr_gain = 0;
            notes[i].note_off_gain = 0;
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

    void generate(int16_t *buffer, size_t size) {

        // バッファ初期化
        memset(buffer, 0, size * sizeof(int16_t));

        for (uint8_t n = 0; n < MAX_NOTES; ++n) {
            if (!notes[n].active) continue;

            // 変数の事前キャッシュ
            int16_t* osc1_wave_ptr = osc1_wave;
            int16_t* osc2_wave_ptr = osc2_wave;
            uint8_t osc1_v = osc1_voice;
            uint8_t osc2_v = osc2_voice;
            uint32_t* osc1_phase = notes[n].osc1_phase;
            uint32_t* osc2_phase = notes[n].osc2_phase;
            uint32_t* osc1_phase_delta = notes[n].osc1_phase_delta;
            uint32_t* osc2_phase_delta = notes[n].osc2_phase_delta;
            
            if (osc1_wave_ptr != nullptr) {
                for (size_t i = 0; i < size; i++) {

                    // 基本レベル
                    int32_t adsr_gain = 0;
                    
                    // アタック
                    if (notes[n].attack_cnt >= 0 && notes[n].attack_cnt < notes[n].attack) {
                        adsr_gain = (notes[n].attack_cnt * 1000) / notes[n].attack;
                        notes[n].attack_cnt++;
                    }
                    // 強制リリース
                    else if (notes[n].force_release_cnt >= 0) {
                        adsr_gain = (notes[n].note_off_gain * notes[n].force_release_cnt) / notes[n].force_release;
                        if (notes[n].force_release_cnt > 0) notes[n].force_release_cnt--;
                    }
                    // リリース
                    else if (notes[n].release_cnt >= 0) {
                        adsr_gain = (notes[n].note_off_gain * notes[n].release_cnt) / notes[n].release;
                        if (notes[n].release_cnt > 0) notes[n].release_cnt--;
                    }
                    // ディケイ
                    else if (notes[n].decay_cnt >= 0) {
                        adsr_gain = notes[n].sustain + (notes[n].level_diff * notes[n].decay_cnt) / notes[n].decay;
                        if (notes[n].decay_cnt > 0) notes[n].decay_cnt--;
                    }
                    // サステイン
                    else {
                        adsr_gain = notes[n].sustain;
                    }
                    
                    notes[n].adsr_gain = adsr_gain;

                    int16_t VCO = 0;

                    // OSC1の処理
                    if(osc1_v == 1) {
                        VCO += osc1_wave_ptr[(osc1_phase[0] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                    }
                    else {
                        for(uint8_t d = 0; d < osc1_v; d++) {
                            VCO += (osc1_wave_ptr[(osc1_phase[d] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)]) / osc1_v;
                        }
                    }
                    // OSC2の処理
                    if(osc2_wave_ptr != nullptr) {
                        if(osc2_v == 1) {
                            VCO += osc2_wave_ptr[(osc2_phase[0] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                        }
                        else {
                            for(uint8_t d = 0; d < osc2_v; d++) {
                                VCO += (osc2_wave_ptr[(osc2_phase[d] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)]) / osc2_v;
                            }
                        }
                    }

                    // ボリューム処理
                    buffer[i] += (VCO * (float)adsr_gain * notes[n].gain) / 1000000;
                    //todo: float

                    // OSC1 次の位相へ
                    if(osc1_v == 1) {
                        osc1_phase[0] += osc1_phase_delta[0];
                    }
                    else {
                        for(uint8_t d = 0; d < osc1_v; d++) {
                            osc1_phase[d] += osc1_phase_delta[d];
                        }
                    }
                    // OSC2 次の位相へ
                    if(osc2_v == 1) {
                        osc2_phase[0] += osc2_phase_delta[0];
                    }
                    else {
                        for(uint8_t d = 0; d < osc2_v; d++) {
                            osc2_phase[d] += osc2_phase_delta[d];
                        }
                    }
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
                notes[n].gain = 0;

                updateActNumOff(n); // 更新してから-1にする
                notes[n].actnum = -1;

                if(!cache[n].processed) {
                    cache[n].processed = true;
                    noteOn(cache[n].note, cache[n].velocity, true, n);
                }
            }

            // ディケイが終了
            else if (notes[n].decay_cnt == 0) {
                notes[n].decay_cnt = -1;
            }
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

    void setAttack(int16_t attack) {
        attack_sample = (attack * SAMPLE_RATE) / 1000;
    }

    void setRelease(int16_t release) {
        release_sample = (release * SAMPLE_RATE) / 1000;
    }

    void setDecay(int16_t decay) {
        decay_sample = (decay * SAMPLE_RATE) / 1000;
    }

    void setSustain(int16_t sustain) {
        sustain_level = (sustain * 1000) / 1000;
        level_diff = 1000 - sustain_level;
    }

    void setCustomShape(int16_t *wave, uint8_t osc) {
        if(osc == 1) {
            memcpy(osc1_cwave, wave, 2048 * sizeof(int16_t));
            osc1_wave = osc1_cwave;
        }
        else if(osc == 2) {
            memcpy(osc2_cwave, wave, 2048 * sizeof(int16_t));
            osc2_wave = osc2_cwave;
        }
    }
};