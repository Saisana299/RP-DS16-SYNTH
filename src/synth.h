#include <limits.h>
#include <shape.h>

#define CALC_IDLE  0x00
#define CALC_ADSR  0x01
#define CALC_PHASE 0x02
#define CALC_SET_F 0x03

class WaveGenerator {
private:

    // 定数
    static const int MAX_NOTES = 4;
    static const int MAX_VOICE = 8;
    static const size_t SAMPLE_SIZE = 2048;
    const int32_t SAMPLE_RATE;
    const uint8_t BIT_SHIFT = bitShift(SAMPLE_SIZE);
    const double HALFTONE = pow(2.0, 1.0 / 12.0) - 1.0;
    const uint16_t DIVIDE_FIXED[7] = {200, 300, 380, 460, 540, 620, 710};
    const int16_t SIN_TABLE[101] = {
        0, 514, 1029, 1543, 2057, 2570, 3083, 3595, 4106, 4616,
        5125, 5633, 6139, 6644, 7147, 7649, 8148, 8646, 9141, 9634,
        10125, 10613, 11099, 11582, 12062, 12539, 13013, 13484, 13951, 14415,
        14875, 15332, 15785, 16234, 16679, 17120, 17557, 17989, 18417, 18841,
        19259, 19673, 20083, 20487, 20886, 21280, 21669, 22052, 22430, 22802,
        23169, 23530, 23886, 24235, 24578, 24916, 25247, 25572, 25891, 26203,
        26509, 26808, 27100, 27386, 27666, 27938, 28203, 28462, 28713, 28958,
        29195, 29425, 29648, 29863, 30072, 30272, 30465, 30651, 30829, 31000,
        31163, 31318, 31465, 31605, 31737, 31861, 31977, 32086, 32186, 32279,
        32363, 32440, 32508, 32569, 32621, 32665, 32702, 32730, 32750, 32762, 32767
    };
    const int16_t COS_TABLE[101] = {
        32767, 32762, 32750, 32730, 32702, 32665, 32621, 32569, 32508, 32440,
        32363, 32279, 32186, 32086, 31977, 31861, 31737, 31605, 31465, 31318,
        31163, 31000, 30829, 30651, 30465, 30272, 30072, 29863, 29648, 29425,
        29195, 28958, 28713, 28462, 28203, 27938, 27666, 27386, 27100, 26808,
        26509, 26203, 25891, 25572, 25247, 24916, 24578, 24235, 23886, 23530,
        23169, 22802, 22430, 22052, 21669, 21280, 20886, 20487, 20083, 19673,
        19259, 18841, 18417, 17989, 17557, 17120, 16679, 16234, 15785, 15332,
        14875, 14415, 13951, 13484, 13013, 12539, 12062, 11582, 11099, 10613,
        10125, 9634, 9141, 8646, 8148, 7649, 7147, 6644, 6139, 5633,
        5125, 4616, 4106, 3595, 3083, 2570, 2057, 1543, 1029, 514, 0
    };

    // コア1制御用
    volatile uint8_t calc_mode = CALC_IDLE;
    volatile uint8_t calc_n = 0x00;
    volatile int8_t calc_i = 0x00;
    volatile uint8_t calc_note = 0x00;

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

    volatile Note notes[MAX_NOTES]; // core1でも使用
    NoteCache cache[MAX_NOTES];

    int16_t volume_gain = 1000; // 1.0 = 100

    uint8_t pan = 50; // 0=L, 50=C, 100=R

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
    volatile uint8_t osc1_voice = 1; // 総ボイス数8まで
    volatile uint8_t osc2_voice = 1;
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
                    float detuneFactor = static_cast<float>(1.0 + HALFTONE * osc1_detune * pos);
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
                    float detuneFactor = static_cast<float>(1.0 + HALFTONE * osc2_detune * pos);
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
        notes[noteIndex].osc1_phase[0] = 0;
        notes[noteIndex].osc2_phase[0] = 0;
        for(uint8_t i = 1; i < MAX_VOICE; i++) {
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

        // core1でフェーズ計算
        /*core1*/ calc_i = i;
        /*core1*/ calc_note = note;
        /*core1*/ calc_mode = CALC_SET_F;

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

        // core1を待つ
        while(calc_mode == CALC_PHASE);

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

    void generate(int16_t *buffer_L, int16_t *buffer_R, size_t size) {

        // バッファ初期化
        memset(buffer_L, 0, size * sizeof(int16_t));
        memset(buffer_R, 0, size * sizeof(int16_t));

        for (uint8_t n = 0; n < MAX_NOTES; ++n) {
            if (!notes[n].active) continue;

            // 変数の事前キャッシュ
            int16_t* osc1_wave_ptr = osc1_wave;
            int16_t* osc2_wave_ptr = osc2_wave;
            volatile uint8_t osc1_v = osc1_voice;
            volatile uint8_t osc2_v = osc2_voice;
            volatile uint32_t* osc1_phase = notes[n].osc1_phase;
            volatile uint32_t* osc2_phase = notes[n].osc2_phase;
            
            if (osc1_wave_ptr != nullptr) {
                for (size_t i = 0; i < size; i++) {

                    // core1でadsrの計算
                    /*core1*/ calc_n = n;
                    /*core1*/ calc_mode = CALC_ADSR;

                    int16_t VCO = 0;

                    // OSC1の処理 + core1で同時にADSR計算
                    if(osc1_v == 1) {
                        VCO += osc1_wave_ptr[(osc1_phase[0] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                    }
                    else {
                        for(uint8_t d = 0; d < osc1_v; d++) {
                            VCO += ((osc1_wave_ptr[(osc1_phase[d] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)])*100) / DIVIDE_FIXED[osc1_v - 2];
                        }
                    }
                    // OSC2の処理 + core1で同時にADSR計算
                    if(osc2_wave_ptr != nullptr) {
                        if(osc2_v == 1) {
                            VCO += osc2_wave_ptr[(osc2_phase[0] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                        }
                        else {
                            for(uint8_t d = 0; d < osc2_v; d++) {
                                VCO += ((osc2_wave_ptr[(osc2_phase[d] >> BIT_SHIFT) & (SAMPLE_SIZE - 1)])*100) / DIVIDE_FIXED[osc2_v - 2];
                            }
                        }
                    }

                    // core1を待つ
                    while(calc_mode == CALC_ADSR);

                    // core1で次のフェーズの計算
                    /*core1*/ calc_n = n;
                    /*core1*/ calc_mode = CALC_PHASE;

                    // ボリューム処理 + core1で同時にフェーズ計算
                    int16_t sample = (((VCO * notes[n].adsr_gain) / 1000) * notes[n].gain) / 1000;

                    // パン処理
                    buffer_L[i] += (sample * COS_TABLE[pan]) / INT16_MAX;
                    buffer_R[i] += (sample * SIN_TABLE[pan]) / INT16_MAX;

                    // core1を待つ
                    while(calc_mode == CALC_PHASE);
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

    void setVoice(uint8_t voice, uint8_t osc) {
        if(osc == 1) {
            osc1_voice = voice;
        }
        else if(osc == 2) {
            osc2_voice = voice;
        }
    }

    void setDetune(uint8_t detune, uint8_t osc) {
        if(osc == 1) {
            osc1_detune = detune / 100.0f;
        }
        else if(osc == 2) {
            osc2_detune = detune / 100.0f;
        }
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

    /**
     * @brief CORE1で負荷分散処理
     * 非CALC_IDLE時の変数アクセスに注意
     */
    void calculate() {
        if(calc_mode == CALC_IDLE) return;

        else if(calc_mode == CALC_ADSR) {
            // // 基本レベル
            int32_t adsr_gain = 0;
            
            // アタック
            if (notes[calc_n].attack_cnt >= 0 && notes[calc_n].attack_cnt < notes[calc_n].attack) {
                adsr_gain = (notes[calc_n].attack_cnt * 1000) / notes[calc_n].attack;
                notes[calc_n].attack_cnt++;
            }
            // 強制リリース
            else if (notes[calc_n].force_release_cnt >= 0) {
                adsr_gain = (notes[calc_n].note_off_gain * notes[calc_n].force_release_cnt) / notes[calc_n].force_release;
                if (notes[calc_n].force_release_cnt > 0) notes[calc_n].force_release_cnt--;
            }
            // リリース
            else if (notes[calc_n].release_cnt >= 0) {
                adsr_gain = (notes[calc_n].note_off_gain * notes[calc_n].release_cnt) / notes[calc_n].release;
                if (notes[calc_n].release_cnt > 0) notes[calc_n].release_cnt--;
            }
            // ディケイ
            else if (notes[calc_n].decay_cnt >= 0) {
                adsr_gain = notes[calc_n].sustain + (notes[calc_n].level_diff * notes[calc_n].decay_cnt) / notes[calc_n].decay;
                if (notes[calc_n].decay_cnt > 0) notes[calc_n].decay_cnt--;
            }
            // サステイン
            else {
                adsr_gain = notes[calc_n].sustain;
            }

            notes[calc_n].adsr_gain = adsr_gain;

            calc_mode = CALC_IDLE;
        }

        else if(calc_mode == CALC_PHASE) {

            // 変数の事前キャッシュ
            volatile uint8_t osc1_v = osc1_voice;
            volatile uint8_t osc2_v = osc2_voice;
            volatile uint32_t* osc1_phase = notes[calc_n].osc1_phase;
            volatile uint32_t* osc2_phase = notes[calc_n].osc2_phase;
            volatile uint32_t* osc1_phase_delta = notes[calc_n].osc1_phase_delta;
            volatile uint32_t* osc2_phase_delta = notes[calc_n].osc2_phase_delta;

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

            calc_mode = CALC_IDLE;
        }

        else if(calc_mode == CALC_SET_F) {
            setFrequency(calc_i, midiNoteToFrequency(calc_note));
            calc_mode = CALC_IDLE;
        }
    }
};