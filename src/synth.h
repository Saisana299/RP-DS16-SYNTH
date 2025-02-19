#include <limits.h>
#include <shape.h>
#include <ring_buffer.h>

#define CALC_IDLE       0x00
#define CALC_NOTE       0x01
#define CALC_SET_F      0x02
#define CALC_PAN_FILTER 0x03

#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)
#define PI_4 ((int32_t)(M_PI_4 * FIXED_ONE))

// プログラムが複雑化している

/* --- 後々実装・確認すること ---*/
// FilterEG (全体に適用) プリセット編集画面->FILTERから設定 noteon/offのたびにattackとrelease
// OSCEG (各ノートに適用) 音程の変調
// LFO流れ: 波形の設定(1周期分)->再生時間の設定->ループ設定->配列に保存->再生
// AMP LFO(全体, 音量・パン)、Filter LFO(全体, カット周波数)、OSC LFO(個別, 音程)の三種類
// LFOモード Triger(NOTEONで再生・繰り返し) Sync(1音目弾いてる間それに合わせる・繰り返し) Env(1度だけ再生・繰り返しなし)

// LEGATO？
// FM?
// モーフィング？

// エイリアシングの確認
// パラメータ変更時の動作確認
// リアルタイムでパラメータ変更(MIDI CC)

class WaveGenerator {
private:

    // 定数
    static const int MAX_NOTES = 4;
    static const int MAX_VOICE = 8;
    static const size_t SAMPLE_SIZE = 2048;
    const int32_t SAMPLE_RATE;
    const uint8_t BIT_SHIFT = bitShift(SAMPLE_SIZE);
    const float HALFTONE = pow(2.0, 1.0 / 12.0) - 1.0;
    const uint16_t DIVIDE_FIXED[7] = {141, 173, 200, 224, 245, 265, 283};

    const int16_t PAN_SIN_TABLE[101] = {
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

    const int16_t PAN_COS_TABLE[101] = {
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

    // コア1制御用(コア0からもアクセスあり)
    volatile uint8_t calc_mode = CALC_IDLE;
    volatile int8_t calc_i = 0x00;
    volatile uint8_t calc_note = 0x00;
    volatile int16_t calc_r;
    volatile uint16_t calc_divide;
    volatile int16_t calc_result_L;
    volatile int16_t calc_result_R;

    // ノート用パラメータ
    struct Note {
        uint32_t osc1_phase[MAX_VOICE];
        uint32_t osc2_phase[MAX_VOICE];
        uint32_t osc_sub_phase;

        uint32_t osc1_phase_delta[MAX_VOICE];
        uint32_t osc2_phase_delta[MAX_VOICE];
        uint32_t osc_sub_phase_delta;

        uint32_t osc1_glide_delta[MAX_VOICE];
        uint32_t osc2_glide_delta[MAX_VOICE];
        uint32_t osc_sub_glide_delta;

        bool active;
        int8_t actnum;

        uint8_t note;
        int32_t gain;
        int32_t adsr_gain;
        int32_t note_off_gain;

        // AMP(音量)用エンベロープ
        int32_t level_diff;
        int32_t sustain;
        int32_t attack;
        int32_t decay;
        int32_t release;
        int32_t force_release;

        // AMP用カウンタ
        int32_t attack_cnt;
        int32_t decay_cnt;
        int32_t release_cnt;
        int32_t force_release_cnt;
    };
    volatile Note notes[MAX_NOTES]; // core1でも利用する

    // 生成できていないノートを保持するキャッシュ
    struct NoteCache {
        bool processed;
        uint8_t note;
        uint8_t velocity;
    };
    NoteCache cache[MAX_NOTES];

    // グライド用
    volatile bool monophonic = false;  // モノフォニック
    volatile bool glide_mode = false;  // グライドモードが有効か
    volatile bool isGlided = false;    // グライドモード有効後にノートが押されたか
    volatile uint16_t glide_time = 15; // グライド時間(ms)

    // Master
    int16_t amp_gain = 1024;   // 1.0% = 1024 (in1000 = out1024)
    volatile uint8_t pan = 50; // 0=L, 50=C, 100=R

    // 波形
    volatile int16_t* osc1_wave = sine;
    volatile int16_t* osc2_wave = nullptr;
    int16_t osc1_cwave[SAMPLE_SIZE];
    int16_t osc2_cwave[SAMPLE_SIZE];

    // サブ波形(ユーザー波形設定不可)
    volatile int16_t* osc_sub_wave = nullptr;

    // OSC特殊合成モード
    volatile bool ring_modulation = false;

    // OSCパラメータ
    volatile uint8_t osc1_voice = 1; // 通常時ボイス数8まで
    volatile uint8_t osc2_voice = 1;
    volatile float osc1_detune = 0.2f; // 0.0f ~ 1.0f
    volatile float osc2_detune = 0.2f;
    uint8_t osc1_spread = 50; // MAX100
    uint8_t osc2_spread = 50;
    volatile int32_t osc1_spread_pan[MAX_VOICE][2]; // [voice][cos|sin]
    volatile int32_t osc2_spread_pan[MAX_VOICE][2];
    volatile int8_t osc1_oct = 0; // -4 ~ 4
    volatile int8_t osc2_oct = 0;
    volatile int8_t osc1_semi = 0; // -12 ~ 12
    volatile int8_t osc2_semi = 0;
    volatile int8_t osc1_cent = 0; // -100 ~ 100
    volatile int8_t osc2_cent = 0;
    volatile int16_t osc1_level = 1024; // 0 ~ 1024 (1.0% = 1024 (in1000 = out1024))
    volatile int16_t osc2_level = 1024;

    // サブOSCパラメータ
    volatile int8_t osc_sub_oct = 0;
    volatile int8_t osc_sub_semi = 0;
    volatile int8_t osc_sub_cent = 0;
    volatile int16_t osc_sub_level = 1024;

    // ADSR
    int16_t sustain_level = 1024; // 1.0% = 1024 (in1000 = out1024)
    int16_t level_diff = 0; // 1.0% = 1024 (in1000 = out1024)
    int32_t attack_sample = (1 * SAMPLE_RATE) >> 10;
    int32_t decay_sample = (SAMPLE_RATE << 10) >> 10;
    int32_t release_sample = (10 * SAMPLE_RATE) >> 10;
    int32_t force_release_sample = (10 * SAMPLE_RATE) >> 10; // 強制Release

    // LPF 初期値 1000Hz 1/sqrt(2)
    // 推奨値 freq 20～20,000 q 0.02～40.0
    bool lpf_enabled = false;
    int32_t lp_f0_L, lp_f1_L, lp_f2_L, lp_f3_L, lp_f4_L;
    int32_t lp_f0_R, lp_f1_R, lp_f2_R, lp_f3_R, lp_f4_R;
    int32_t lp_in1_L = 0, lp_in2_L = 0; // バッファ
    int32_t lp_in1_R = 0, lp_in2_R = 0;
    int32_t lp_out1_L = 0, lp_out2_L = 0;
    int32_t lp_out1_R = 0, lp_out2_R = 0;

    // HPF 初期値 500Hz 1/sqrt(2)
    // 推奨値 freq 20～20,000 q 0.02～40.0
    bool hpf_enabled = false;
    int32_t hp_f0_L, hp_f1_L, hp_f2_L, hp_f3_L, hp_f4_L;
    int32_t hp_f0_R, hp_f1_R, hp_f2_R, hp_f3_R, hp_f4_R;
    int32_t hp_in1_L = 0, hp_in2_L = 0;
    int32_t hp_in1_R = 0, hp_in2_R = 0;
    int32_t hp_out1_L = 0, hp_out2_L = 0;
    int32_t hp_out1_R = 0, hp_out2_R = 0;

    // ディレイエフェクト(Effect)
    RingBuffer ringbuff_L, ringbuff_R;
    bool delay_enabled = false;
    int16_t time; // ms
    int16_t level; // 1.0 = 1024
    int16_t feedback; // 0.5 = 512
    uint32_t delay_long = 0;

    // ビットシフト
    uint8_t bitShift(size_t tableSize) {
        uint8_t shift = 0;
        while (tableSize > 1) {
            tableSize >>= 1;
            shift++;
        }
        return 32 - shift;
    }

    // 線形補間
    float lerp(float a, float b, float t) {
        return a + t * (b - a);
    }

    // MIDIノートを周波数に変換
    float midiNoteToFrequency(uint8_t midiNote, int8_t cent) {
        float frequency = 440.0 * pow(2.0, (midiNote - 69) / 12.0);
        return frequency * pow(2.0, cent / 1200.0);
    }

    // ノート番号を周波数に変換
    void setFrequency(int noteIndex) {
        if (noteIndex >= 0 && noteIndex < MAX_NOTES) {

            float osc1_freq =  midiNoteToFrequency(calc_note + (osc1_oct * 12) + (osc1_semi), osc1_cent);
            float osc2_freq =  midiNoteToFrequency(calc_note + (osc2_oct * 12) + (osc2_semi), osc2_cent);
            float osc_sub_freq =  midiNoteToFrequency(calc_note + (osc_sub_oct * 12) + (osc_sub_semi), osc_sub_cent);

            // 変数キャッシュ
            volatile Note* p_note = &notes[noteIndex];
            volatile uint32_t* osc1_phase_delta = &p_note->osc1_phase_delta[0];
            volatile uint32_t* osc2_phase_delta = &p_note->osc2_phase_delta[0];

            // osc1処理
            if(osc1_voice == 1) {
                *osc1_phase_delta = osc1_freq * (float)(1ULL << 32) / SAMPLE_RATE;
            }
            else {
                for(uint8_t d = 0; d < osc1_voice; ++d, ++osc1_phase_delta) {
                    const auto pos = lerp(-1.0f, 1.0f, 1.0f * d / (osc1_voice - 1));
                    float detuneFactor = static_cast<float>(1.0 + HALFTONE * osc1_detune * pos);
                    *osc1_phase_delta = (osc1_freq * detuneFactor) * (float)(1ULL << 32) / SAMPLE_RATE;
                }
            }

            // osc2処理
            if(osc2_voice == 1) {
                *osc2_phase_delta = osc2_freq * (float)(1ULL << 32) / SAMPLE_RATE;
            }
            else {
                for(uint8_t d = 0; d < osc2_voice; ++d, ++osc2_phase_delta) {
                    const auto pos = lerp(-1.0f, 1.0f, 1.0f * d / (osc2_voice - 1));
                    float detuneFactor = static_cast<float>(1.0 + HALFTONE * osc2_detune * pos);
                    *osc2_phase_delta = (osc2_freq * detuneFactor) * (float)(1ULL << 32) / SAMPLE_RATE;
                }
            }

            // sub osc処理
            p_note->osc_sub_phase_delta = osc_sub_freq * (float)(1ULL << 32) / SAMPLE_RATE;
        }
    }

    void initSpreadPan() {
        volatile int32_t (*p_osc1_spread_pan)[2] = &osc1_spread_pan[0];
        volatile int32_t (*p_osc2_spread_pan)[2] = &osc2_spread_pan[0];

        for (uint8_t d = 0; d < osc1_voice; ++d, ++p_osc1_spread_pan) {
            const auto osc1_pos = lerp(-1.0f, 1.0f, 1.0f * d / (osc1_voice - 1));
            float osc1_angle = M_PI_4 * (1.0f + osc1_pos * (osc1_spread / 100.0f));
            *p_osc1_spread_pan[0] = (int32_t)(cos(osc1_angle) * FIXED_ONE); // X = cos
            *p_osc1_spread_pan[1] = (int32_t)(sin(osc1_angle) * FIXED_ONE); // Y = sin
        }
        for (uint8_t d = 0; d < osc2_voice; ++d, ++p_osc2_spread_pan) {
            const auto osc2_pos = lerp(-1.0f, 1.0f, 1.0f * d / (osc2_voice - 1));
            float osc2_angle = M_PI_4 * (1.0f + osc2_pos * (osc2_spread / 100.0f));
            *p_osc2_spread_pan[0] = (int32_t)(cos(osc2_angle) * FIXED_ONE); // X = cos
            *p_osc2_spread_pan[1] = (int32_t)(sin(osc2_angle) * FIXED_ONE); // Y = sin
        }
    }

    int16_t lpfProcessL(int16_t in, int16_t mix = 1 << 10) {
        int16_t out = ((lp_f0_L * in) + (lp_f1_L * lp_in1_L) + (lp_f2_L * lp_in2_L) - (lp_f3_L * lp_out1_L) - (lp_f4_L * lp_out2_L)) >> FIXED_SHIFT;
        lp_in2_L = lp_in1_L;
        lp_in1_L = in;
        lp_out2_L = lp_out1_L;
        lp_out1_L = out;
        return ((1024 - mix) * in + mix * out) >> 10;
    }

    int16_t lpfProcessR(int16_t in, int16_t mix = 1 << 10) {
        int16_t out = ((lp_f0_R * in) + (lp_f1_R * lp_in1_R) + (lp_f2_R * lp_in2_R) - (lp_f3_R * lp_out1_R) - (lp_f4_R * lp_out2_R)) >> FIXED_SHIFT;
        lp_in2_R = lp_in1_R;
        lp_in1_R = in;
        lp_out2_R = lp_out1_R;
        lp_out1_R = out;
        return ((1024 - mix) * in + mix * out) >> 10;
    }

    int16_t hpfProcessL(int16_t in, int16_t mix = 1 << 10) {
        int16_t out = ((hp_f0_L * in) + (hp_f1_L * hp_in1_L) + (hp_f2_L * hp_in2_L) - (hp_f3_L * hp_out1_L) - (hp_f4_L * hp_out2_L)) >> FIXED_SHIFT;
        hp_in2_L = hp_in1_L;
        hp_in1_L = in;
        hp_out2_L = hp_out1_L;
        hp_out1_L = out;
        return ((1024 - mix) * in + mix * out) >> 10;
    }

    int16_t hpfProcessR(int16_t in, int16_t mix = 1 << 10) {
        int16_t out = ((hp_f0_R * in) + (hp_f1_R * hp_in1_R) + (hp_f2_R * hp_in2_R) - (hp_f3_R * hp_out1_R) - (hp_f4_R * hp_out2_R)) >> FIXED_SHIFT;
        hp_in2_R = hp_in1_R;
        hp_in1_R = in;
        hp_out2_R = hp_out1_R;
        hp_out1_R = out;
        return ((1024 - mix) * in + mix * out) >> 10;
    }

    void lowPass(float freq, float q) {
        // フィルタ係数計算で使用する中間値を求める。
        float omega = 2.0f * M_PI *  freq / (float)SAMPLE_RATE;
        float alpha = sin(omega) / (2.0f * q);

        // フィルタ係数を求める。
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cos(omega);
        float a2 = 1.0f - alpha;
        float b0 = (1.0f - cos(omega)) / 2.0f;
        float b1 = 1.0f - cos(omega);
        float b2 = (1.0f - cos(omega)) / 2.0f;

        lp_f0_L = lp_f0_R = (int32_t)((b0 / a0) * 65536);
        lp_f1_L = lp_f1_R = (int32_t)((b1 / a0) * 65536);
        lp_f2_L = lp_f2_R = (int32_t)((b2 / a0) * 65536);
        lp_f3_L = lp_f3_R = (int32_t)((a1 / a0) * 65536);
        lp_f4_L = lp_f4_R = (int32_t)((a2 / a0) * 65536);
    }

    void highPass(float freq, float q) {
        // フィルタ係数計算で使用する中間値を求める。
        float omega = 2.0f * M_PI *  freq / (float)SAMPLE_RATE;
        float alpha = sin(omega) / (2.0f * q);

        // フィルタ係数を求める。
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cos(omega);
        float a2 = 1.0f - alpha;
        float b0 = (1.0f + cos(omega)) / 2.0f;
        float b1 = -(1.0f + cos(omega));
        float b2 = (1.0f + cos(omega)) / 2.0f;

        hp_f0_L = hp_f0_R = (int32_t)((b0 / a0) * 65536);
        hp_f1_L = hp_f1_R = (int32_t)((b1 / a0) * 65536);
        hp_f2_L = hp_f2_R = (int32_t)((b2 / a0) * 65536);
        hp_f3_L = hp_f3_R = (int32_t)((a1 / a0) * 65536);
        hp_f4_L = hp_f4_R = (int32_t)((a2 / a0) * 65536);
    }

    bool canSetVoice(uint8_t osc, uint8_t voice, bool setWave = false, uint8_t new_id = 0xff) {
        if(osc == 0x01) {
            if (osc1_wave == nullptr && !setWave) return false;
            uint8_t sum = 0;
            if (new_id != 0xff || !setWave) sum += voice;
            if (osc2_wave != nullptr) sum += osc2_voice;
            if (osc_sub_wave != nullptr) sum += 1;

            return sum <= MAX_VOICE;
        }
        else if(osc == 0x02) {
            if (osc2_wave == nullptr && !setWave) return false;
            uint8_t sum = 0;
            if (new_id != 0xff || !setWave) sum += voice;
            if (osc1_wave != nullptr) sum += osc1_voice;
            if (osc_sub_wave != nullptr) sum += 1;

            return sum <= MAX_VOICE;
        }
        else if(osc == 0x03) {
            if (osc_sub_wave == nullptr && !setWave) return false;
            uint8_t sum = 0;
            if (new_id != 0xff || !setWave) sum += voice;
            if (osc1_wave != nullptr) sum += osc1_voice;
            if (osc2_wave != nullptr) sum += osc2_voice;

            return sum <= MAX_VOICE;
        }

        return false;
    }

    int8_t getOldNote() {
        int8_t index = -1;
        uint8_t min = 0xff;
        volatile Note* p_note = &notes[0];

        for(uint8_t i = 0; i < MAX_NOTES; ++i, ++p_note) {
            if(getActiveNote() == MAX_NOTES) {
                if(p_note->actnum < min){
                    min = p_note->actnum;
                    index = i;
                }
            } else if(p_note->active == false) {
                index = i;
            }
        }
        return index;
    }

    int8_t getNoteIndex(uint8_t note) {
        int8_t index = -1;
        volatile Note* p_note = &notes[0];

        for(uint8_t i = 0; i < MAX_NOTES; ++i, ++p_note) {
            if(p_note->note == note) index = i;
        }
        return index;
    }

    void updateActNumOn(int noteIndex) {
        volatile Note* i_note = &notes[noteIndex];

        if (noteIndex < 0 || noteIndex >= MAX_NOTES) {
            return;
        }
        if (!i_note->active) {
            return;
        }
        if (i_note->actnum != 3) return;

        volatile Note* p_note = &notes[0];

        for (uint8_t i = 0; i < MAX_NOTES; ++i, ++p_note) {
            if (i == noteIndex) continue;
            if (p_note->active && p_note->actnum <= i_note->actnum) {
                if(p_note->actnum > -1) p_note->actnum--;
            }
        }
    }

    void updateActNumOff(int noteIndex) {
        if (noteIndex < 0 || noteIndex >= MAX_NOTES) {
            return;
        }

        volatile Note* i_note = &notes[noteIndex];
        volatile Note* p_note = &notes[0];

        for (uint8_t i = 0; i < MAX_NOTES; ++i, ++p_note) {
            if (p_note->actnum > i_note->actnum) {
                if(p_note->actnum > -1) p_note->actnum--;
            }
        }
    }

    bool isActiveNote(uint8_t note) {
        bool active = false;
        volatile Note* p_note = &notes[0];

        for(uint8_t i = 0; i < MAX_NOTES; ++i, ++p_note) {
            if(p_note->note == note && p_note->active == true){
                active = true;
            }
        }
        return active;
    }

    void resetPhase(int8_t noteIndex) {
        volatile Note* i_note = &notes[noteIndex];
        volatile uint32_t* osc1_phase = i_note->osc1_phase;
        volatile uint32_t* osc2_phase = i_note->osc2_phase;

        for(uint8_t i = 0; i < MAX_VOICE; ++i) {
            uint32_t random = rand();
            i_note->osc1_phase[i] = random;
            i_note->osc2_phase[i] = random;

            if(i == 0) i_note->osc_sub_phase = random;
        }
    }

    void resetPhaseDelta(int8_t noteIndex) {
        volatile Note* i_note = &notes[noteIndex];
        volatile uint32_t* osc1_phase_delta = i_note->osc1_phase_delta;
        volatile uint32_t* osc2_phase_delta = i_note->osc2_phase_delta;

        for(uint8_t i = 0; i < MAX_VOICE; ++i) {
            i_note->osc1_phase_delta[i] = 0;
            i_note->osc2_phase_delta[i] = 0;
        }
        i_note->osc_sub_phase_delta = 0;
    }

    uint32_t calculate_delay_samples() {
        // フィードバックを浮動小数点数に変換（16ビット整数の最大値を1024とする）
        float feedback_ratio = (float)feedback / 1024.0f;

        // フィードバックが1.0（1024）の場合は無限大
        if (feedback_ratio >= 1.0f) {
            return UINT32_MAX; // 可能な限り大きな整数を返す
        }

        // 60dB減衰するまでのエコー回数を計算
        float n = log(0.001f) / log(feedback_ratio);

        // 全体の残響時間をミリ秒で計算
        float reverb_time_ms = n * (float)time;

        // 残響時間をサンプル数に変換
        uint32_t reverb_samples = (uint32_t)((reverb_time_ms / 1000.0f) * (float)SAMPLE_RATE);

        return reverb_samples;
    }

public:
    WaveGenerator(int32_t rate): SAMPLE_RATE(rate) {
        noteReset();
        for(uint8_t i = 0; i < MAX_NOTES; i++) {
            cache[i].processed = true;
            cache[i].note = 0;
            cache[i].velocity = 0;
        }
        initSpreadPan();
        lowPass(1000.0f, 1.0f/sqrt(2.0f));
        highPass(500.0f, 1.0f/sqrt(2.0f));
    }

    uint8_t getActiveNote() {
        uint8_t active = 0;
        volatile Note* p_note = &notes[0];

        for(uint8_t i = 0; i < MAX_NOTES; ++i, ++p_note) {
            if(p_note->active == true) active++;
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

        int8_t i = -1;

        if(!monophonic) {
            i = getOldNote();
            if(isActiveNote(note)) {
                i = getNoteIndex(note);
            }
            if(isCache) i = cacheIndex;
        }
        else {
            i = 0;
        }
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

        // AMP ADSR
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
        notes[i].gain = ((amp_gain / MAX_NOTES) * ((velocity << 10) / 127)) >> 10;

        if(isCache) updateActNumOn(i);

        // actnumの更新
        int8_t newActNum = -1;
        volatile Note* p_note = &notes[0];
        for(uint8_t j = 0; j < MAX_NOTES; ++j, ++p_note) {
            if(p_note->actnum > newActNum) newActNum = p_note->actnum;
        }
        newActNum++;
        if(newActNum >= 4) newActNum = 3;
        else if(newActNum == -1) newActNum = 0;
        notes[i].actnum = newActNum;

        // core1を待つ
        while(calc_mode == CALC_SET_F);

        notes[i].active = true;
    }

    void noteOff(uint8_t note) {
        // cache にある場合は消す
        NoteCache* p_cache = &cache[0];
        for(uint8_t n = 0; n < MAX_NOTES; ++n, ++p_cache) {
            if(p_cache->note == note && !p_cache->processed) {
                p_cache->processed = true;
            }
        }

        if(!isActiveNote(note)) return;

        int8_t i = getNoteIndex(note);
        if(i == -1) return;

        // リリースはnoteOff時のgainから行う
        notes[i].note_off_gain = notes[i].adsr_gain;
        notes[i].release_cnt = notes[i].release;

        notes[i].attack_cnt = -1;
        notes[i].decay_cnt = -1;
   }

    void noteReset() {
        volatile Note* p_note = &notes[0];

        for(uint8_t i = 0; i < MAX_NOTES; ++i, ++p_note) {
            resetPhase(i);
            resetPhaseDelta(i);
            p_note->active = false;
            p_note->actnum = -1;
            p_note->note = 0xff;
            p_note->gain = 0;
            p_note->adsr_gain = 0;
            p_note->note_off_gain = 0;
            p_note->attack_cnt = -1;
            p_note->decay_cnt = -1;
            p_note->release_cnt = -1;
            p_note->force_release_cnt = -1;

            p_note->level_diff = level_diff;
            p_note->sustain = sustain_level;
            p_note->attack = attack_sample;
            p_note->decay = decay_sample;
            p_note->release = release_sample;
        }
    }

    void setShape(uint8_t id, uint8_t osc) {
        if(!canSetVoice(osc, 1, true, id)) return;
        switch(id) {
            case 0x00:
                if(osc == 0x01) osc1_wave = sine;
                else if(osc == 0x02) osc2_wave = sine;
                else if(osc == 0x03) osc_sub_wave = sine;
                break;
            case 0x01:
                if(osc == 0x01) osc1_wave = triangle;
                else if(osc == 0x02) osc2_wave = triangle;
                else if(osc == 0x03) osc_sub_wave = triangle;
                break;
            case 0x02:
                if(osc == 0x01) osc1_wave = saw;
                else if(osc == 0x02) osc2_wave = saw;
                else if(osc == 0x03) osc_sub_wave = saw;
                break;
            case 0x03:
                if(osc == 0x01) osc1_wave = square;
                else if(osc == 0x02) osc2_wave = square;
                else if(osc == 0x03) osc_sub_wave = square;
                break;
            case 0xff:
                if(osc == 0x01) {
                    osc1_wave = nullptr;
                    osc1_voice = 1;
                }
                else if(osc == 0x02) {
                    osc2_wave = nullptr;
                    osc2_voice = 1;
                }
                else if(osc == 0x03) {
                    osc_sub_wave = nullptr;
                }
                break;
        }
    }

    void setAttack(int16_t attack) {
        if(attack > 32000) attack = 32000;
        else if(attack < 0) attack = 0;

        // in1000 = out1024, in500 = out512
        attack_sample = (((attack << 10) / 1000) * SAMPLE_RATE) >> 10;
    }

    void setRelease(int16_t release) {
        if(release > 32000) release = 32000;
        else if(release < 0) release = 0;

        // in1000 = out1024, in500 = out512
        release_sample = (((release << 10) / 1000) * SAMPLE_RATE) >> 10;
    }

    void setDecay(int16_t decay) {
        if(decay > 32000) decay = 32000;
        else if(decay < 0) decay = 0;

        // in1000 = out1024, in500 = out512
        decay_sample = (((decay << 10) / 1000) * SAMPLE_RATE) >> 10;
    }

    void setSustain(int16_t sustain) {
        if(sustain > 1000) sustain = 1000;
        else if(sustain < 0) sustain = 0;

        sustain_level = (sustain << 10) / 1000; // in1000 = out1024, in500 = out512
        level_diff = 1024 - sustain_level;
    }

    void setVoice(uint8_t voice, uint8_t osc) {
        if(voice > MAX_VOICE) voice = MAX_VOICE;
        if(canSetVoice(osc, voice)) {
            if(osc == 1) {
                osc1_voice = voice;
            }
            else if(osc == 2) {
                osc2_voice = voice;
            }
            initSpreadPan();
        }
    }

    void setDetune(uint8_t detune, uint8_t osc) {
        if(detune > 100) detune = 100;
        if(osc == 1) {
            osc1_detune = detune / 100.0f;
        }
        else if(osc == 2) {
            osc2_detune = detune / 100.0f;
        }
        initSpreadPan();
    }

    void setSpread(uint8_t spread, uint8_t osc) {
        if(spread > 100) spread = 100;
        if(osc == 1) {
            osc1_spread = spread;
        }
        else if(osc == 2) {
            osc2_spread = spread;
        }
        initSpreadPan();
    }

    void setCustomShape(int16_t *wave, uint8_t osc) {
        if(!canSetVoice(osc, 1, true, 0x00)) return;
        if(osc == 1) {
            memcpy(osc1_cwave, wave, 2048 * sizeof(int16_t));
            osc1_wave = osc1_cwave;
        }
        else if(osc == 2) {
            memcpy(osc2_cwave, wave, 2048 * sizeof(int16_t));
            osc2_wave = osc2_cwave;
        }
    }

    void setLowPassFilter(bool enable, float freq = 1000.0f, float q = 1.0f/sqrt(2.0f)){
        if(freq < 20) freq = 20;
        else if(freq > 20000) freq = 20000;
        if(q < 0.02f) q = 0.02f;
        else if(q > 40.0f) q = 40.0f;

        lpf_enabled = enable;
        if(lpf_enabled) lowPass(freq, q);
    }

    void setHighPassFilter(bool enable, float freq = 500.0f, float q = 1.0f/sqrt(2.0f)){
        if(freq < 20) freq = 20;
        else if(freq > 20000) freq = 20000;
        if(q < 0.02f) q = 0.02f;
        else if(q > 40.0f) q = 40.0f;

        hpf_enabled = enable;
        if(hpf_enabled) highPass(freq, q);
    }

    void setOscLevel(uint8_t osc, int16_t level) {
        if(level > 1000) level = 1000;
        else if(level < 0) level = 0;

        if(osc == 0x01) {
            osc1_level = (level << 10) / 1000; // in1000 = out1024, in500 = out512
        }
        else if(osc == 0x02) {
            osc2_level = (level << 10) / 1000; // in1000 = out1024, in500 = out512
        }
        else if(osc == 0x03) {
            osc_sub_level = (level << 10) / 1000; // in1000 = out1024, in500 = out512
        }
    }

    void setOscPan(uint8_t osc, uint8_t pan) {
        //
    }

    void setOscOctave(uint8_t osc, int8_t octave) {
        if(octave > 4) octave = 4;
        else if(octave < -4) octave -4;

        if(osc == 0x01) {
            osc1_oct = octave;
        }
        else if(osc == 0x02) {
            osc2_oct = octave;
        }
        else if(osc == 0x03) {
            osc_sub_oct = octave;
        }
    }

    void setOscSemitone(uint8_t osc, int8_t semitone) {
        if(semitone > 12) semitone = 12;
        else if(semitone < -12) semitone = -12;

        if(osc == 0x01) {
            osc1_semi = semitone;
        }
        else if(osc == 0x02) {
            osc2_semi = semitone;
        }
        else if(osc == 0x03) {
            osc_sub_semi = semitone;
        }
    }

    void setOscCent(uint8_t osc, int8_t cent) {
        if(cent > 100) cent = 100;
        else if(cent < -100) cent = -100;

        if(osc == 0x01) {
            osc1_cent = cent;
        }
        else if(osc == 0x02) {
            osc2_cent = cent;
        }
        else if(osc == 0x03) {
            osc_sub_cent = cent;
        }
    }

    void setAmpLevel(int16_t level) {
        if(level > 1000) level = 1000;
        else if(level < 0) level = 0;

        amp_gain = (level << 10) / 1000; // in1000 = out1024, in500 = out512
    }

    void setAmpPan(uint8_t pan) {
        if(pan > 100) pan = 100;
        else if(pan < 0) pan = 0;
        this->pan = pan;
    }

    void setDelay(bool enable, int16_t time = 250, int16_t level = 300, int16_t feedback = 500) {
        if(time < 10) time = 10;
        else if(time > 300) time = 300;
        if(feedback > 900) feedback = 900;
        else if(feedback < 0) feedback = 0;
        if(level > 1000) level = 1000;
        else if(level < 0) level = 0;

        delay_enabled = enable;
        if(delay_enabled) {
            this->time = time;
            this->level = (level << 10) / 1000;
            this->feedback = (feedback << 10) / 1000;
            int delay_sample = SAMPLE_RATE * this->time / 1000;
            delay_long = calculate_delay_samples();
            ringbuff_L.SetInterval(delay_sample);
            ringbuff_R.SetInterval(delay_sample);
        }
        else {
            delay_long = 0;
            ringbuff_L.reset();
            ringbuff_R.reset();
        }
    }

    void setMod(uint8_t mod) {
        switch (mod) {
            case 0x00:
                ring_modulation = false;
                break;

            case 0x01:
                ring_modulation = true;
                break;
        }
    }

    void setMonophonic(bool enable) {
        monophonic = enable;
        if(!enable) {
            glide_mode = false;
            isGlided = false;
        }
    }

    void setGlideMode(bool enable, uint16_t time = 15) {
        if(enable && monophonic) {
            if(time > 3000) time = 3000;
            else if(time < 1) time = 1;
            glide_mode = true;
            isGlided = false;
            glide_time = time;
        }
        else if(!enable) {
            glide_mode = false;
            isGlided = false;
        }
    }

    bool isDelayEnabled() {
        return delay_enabled;
    }

    uint32_t* getDelayLong() {
        return &delay_long;
    }

    int16_t delayProcess(int16_t in, uint8_t lr) {
        int16_t tmp;

        // ディレイ信号を加える
        if(lr == 0x00)
            tmp = in + ((level * ringbuff_L.Read()) >> 10);
        else if(lr == 0x01)
            tmp = in + ((level * ringbuff_R.Read()) >> 10);

        // 入力信号をリングバッファへ
        if(lr == 0x00)
            ringbuff_L.Write(in + ((feedback * ringbuff_L.Read()) >> 10));
        else if(lr == 0x01)
            ringbuff_R.Write(in + ((feedback * ringbuff_R.Read()) >> 10));

        // 更新
        if(lr == 0x00)
            ringbuff_L.Update();
        else if(lr == 0x01)
            ringbuff_R.Update();

        // 出力信号
        return tmp;
    }

    /**
     * @brief シンセのパラメータをリセットします
     */
    void resetParam() {
        // 波形リセット
        setShape(0xff, 0x01);
        setShape(0xff, 0x02);
        setShape(0xff, 0x03);
        // ADSRリセット
        setAttack(1);
        setDecay(1000);
        setSustain(1000);
        setRelease(10);
        // Chorusリセット
        setVoice(1, 0x01);
        setVoice(1, 0x02);
        setDetune(20, 0x01);
        setDetune(20, 0x02);
        setSpread(0, 0x01);
        setSpread(0, 0x02);
        // LP/HPリセット
        setLowPassFilter(false);
        setHighPassFilter(false);
        // OSCリセット
        setOscLevel(0x01, 1000);
        setOscLevel(0x02, 1000);
        setOscLevel(0x03, 1000);
        setOscPan(0x01, 50);
        setOscPan(0x02, 50);
        setOscPan(0x03, 50);
        setOscOctave(0x01, 0);
        setOscOctave(0x02, 0);
        setOscOctave(0x03, 0);
        setOscSemitone(0x01, 0);
        setOscSemitone(0x02, 0);
        setOscSemitone(0x03, 0);
        setOscCent(0x01, 0);
        setOscCent(0x02, 0);
        setOscCent(0x03, 0);
        // Ampリセット
        setAmpLevel(1000);
        setAmpPan(50);
        // Delayリセット
        setDelay(false);
        // Modulationリセット
        setMod(0x00);
        // Glideリセット（monophonicはここではリセットしない）
        setGlideMode(false);
    }

    void generate(int16_t *buffer_L, int16_t *buffer_R, size_t size) {

        // ローカル変数用
        uint8_t d;
        int16_t OSC1, OSC2, OSC_SUB;
        int16_t OSC1_L, OSC1_R;
        int16_t OSC2_L, OSC2_R;
        int16_t OSC_SUB_L, OSC_SUB_R;
        int16_t L, RM_L;
        int16_t R, RM_R;
        int16_t osc1_pre_level, osc2_pre_level, osc_sub_pre_level;
        int32_t adsr_gain;

        // 配列のキャッシュ用
        volatile Note* p_note;
        NoteCache* p_cache;
        volatile uint32_t* p_osc1_phase;
        volatile uint32_t* p_osc2_phase;
        volatile uint32_t* p_osc1_phase_delta;
        volatile uint32_t* p_osc2_phase_delta;
        volatile int32_t (*p_osc1_spread_pan)[2];
        volatile int32_t (*p_osc2_spread_pan)[2];
        int16_t* p_buffer_L;
        int16_t* p_buffer_R;

        // 変数のキャッシュ
        uint8_t osc1_v = osc1_voice;
        uint8_t osc2_v = osc2_voice;
        uint16_t osc1_level_local = osc1_level;
        uint16_t osc2_level_local = osc2_level;
        uint16_t osc_sub_level_local = osc_sub_level;
        uint8_t pan_local = pan;

        // バッファ初期化
        memset(buffer_L, 0, size * sizeof(int16_t));
        memset(buffer_R, 0, size * sizeof(int16_t));

        // レベル調整用 OSCが複数ある場合下げる
        uint16_t osc_divide = 100;
        uint8_t not_null = 0;
        if(osc1_wave != nullptr) not_null++;
        if(osc2_wave != nullptr) not_null++;
        if(osc_sub_wave != nullptr) not_null++;
        if(not_null == 3) {
            osc_divide = DIVIDE_FIXED[2];
        }
        else if(not_null == 2) {
            osc_divide = DIVIDE_FIXED[0];
        }

        // core1用
        calc_divide = osc_divide;

        // バッファ配列の事前キャッシュ
        p_buffer_L = &buffer_L[0];
        p_buffer_R = &buffer_R[0];

        for (size_t i = 0; i < size; ++i, ++p_buffer_L, ++p_buffer_R) {
            // notesの1アドレス
            p_note = &notes[1];

            // core1で半分計算
            /*core1*/ calc_mode = CALC_NOTE;

            // 1, 3, 5...
            for (uint8_t n = 1; n < MAX_NOTES; n += 2, p_note += 2) {
                if (!p_note->active) continue;

                // 初期化
                OSC1_L = 0, OSC1_R = 0;
                OSC2_L = 0, OSC2_R = 0;
                OSC_SUB_L = 0, OSC_SUB_R = 0;

                // 配列の事前キャッシュ
                p_osc1_phase = &p_note->osc1_phase[0];
                p_osc2_phase = &p_note->osc2_phase[0];
                p_osc1_spread_pan = &osc1_spread_pan[0];
                p_osc2_spread_pan = &osc2_spread_pan[0];

                if (osc1_wave != nullptr || osc2_wave != nullptr || osc_sub_wave != nullptr) {

                    /**
                     * Amplifier + Envelope Generator
                     * エンベロープの状態から音量を計算します
                     */
                    {
                        // 基本レベル
                        adsr_gain = 0;

                        // アタック
                        if (p_note->attack_cnt >= 0 && p_note->attack_cnt < p_note->attack) {
                            adsr_gain = (p_note->attack_cnt << 10) / p_note->attack;
                            p_note->attack_cnt++;
                        }
                        // 強制リリース
                        else if (p_note->force_release_cnt >= 0) {
                            adsr_gain = (p_note->note_off_gain * p_note->force_release_cnt) / p_note->force_release;
                            if (p_note->force_release_cnt > 0) p_note->force_release_cnt--;
                        }
                        // リリース
                        else if (p_note->release_cnt >= 0) {
                            adsr_gain = (p_note->note_off_gain * p_note->release_cnt) / p_note->release;
                            if (p_note->release_cnt > 0) p_note->release_cnt--;
                        }
                        // ディケイ
                        else if (p_note->decay_cnt >= 0) {
                            adsr_gain = p_note->sustain + (p_note->level_diff * p_note->decay_cnt) / p_note->decay;
                            if (p_note->decay_cnt > 0) p_note->decay_cnt--;
                        }
                        // サステイン
                        else {
                            adsr_gain = p_note->sustain;
                        }

                        p_note->adsr_gain = adsr_gain;
                    }

                    /**
                     * Oscillator 1
                     * オシレーターで波形を生成します
                     */
                    if(osc1_wave != nullptr) {
                        if(osc1_v == 1) {
                            OSC1 = osc1_wave[(*p_osc1_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                            OSC1_L += OSC1;
                            OSC1_R += OSC1;
                        }
                        else {
                            uint16_t divide = DIVIDE_FIXED[osc1_v - 2];
                            for(d = 0; d < osc1_v; ++d, ++p_osc1_phase, ++p_osc1_spread_pan) {
                                OSC1 = ((osc1_wave[(*p_osc1_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)])*100) / divide;
                                OSC1_L += (OSC1 * (*p_osc1_spread_pan[0])) >> FIXED_SHIFT; // cos
                                OSC1_R += (OSC1 * (*p_osc1_spread_pan[1])) >> FIXED_SHIFT; // sin
                            }
                        }
                        // OSC1レベル処理
                        osc1_pre_level = (osc1_level_local*100) / osc_divide;
                        OSC1_L = (OSC1_L * (osc1_pre_level)) >> 10;
                        OSC1_R = (OSC1_R * (osc1_pre_level)) >> 10;
                    }

                    /**
                     * Oscillator 2
                     * オシレーターで波形を生成します
                     */
                    if(osc2_wave != nullptr) {
                        if(osc2_v == 1) {
                            OSC2 = osc2_wave[(*p_osc2_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                            OSC2_L += OSC2;
                            OSC2_R += OSC2;
                        }
                        else {
                            uint16_t divide = DIVIDE_FIXED[osc2_v - 2];
                            for(d = 0; d < osc2_v; ++d, ++p_osc2_phase, ++p_osc2_spread_pan) {
                                OSC2 = ((osc2_wave[(*p_osc2_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)])*100) / divide;
                                OSC2_L += (OSC2 * (*p_osc2_spread_pan[0])) >> FIXED_SHIFT; // cos
                                OSC2_R += (OSC2 * (*p_osc2_spread_pan[1])) >> FIXED_SHIFT; // sin
                            }
                        }
                        // OSC2レベル処理
                        osc2_pre_level = (osc2_level_local*100) / osc_divide;
                        OSC2_L = (OSC2_L * (osc2_pre_level)) >> 10;
                        OSC2_R = (OSC2_R * (osc2_pre_level)) >> 10;
                    }

                    /**
                     * Oscillator SUB
                     * オシレーターで波形を生成します
                     */
                    if(osc_sub_wave != nullptr) {
                        OSC_SUB = osc_sub_wave[(p_note->osc_sub_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                        OSC_SUB_L += OSC_SUB;
                        OSC_SUB_R += OSC_SUB;
                        // OSC_SUBレベル処理
                        osc_sub_pre_level = ((osc_sub_level_local*100) / osc_divide);
                        OSC_SUB_L = (OSC_SUB_L * (osc_sub_pre_level)) >> 10;
                        OSC_SUB_R = (OSC_SUB_R * (osc_sub_pre_level)) >> 10;
                    }

                    // 合成用変数 初期化
                    L = 0, RM_L = 0;
                    R = 0, RM_R = 0;

                    // リングモジュレーション
                    if(ring_modulation) {
                        if(osc1_wave != nullptr && osc2_wave != nullptr) {
                            RM_L = (OSC1_L * OSC2_L) / 16384;
                            RM_R = (OSC1_R * OSC2_R) / 16384;
                            OSC1_L = (OSC1_L + OSC2_L) / 2;
                            OSC1_R = (OSC1_R + OSC2_R) / 2;
                            OSC2_L = RM_L;
                            OSC2_R = RM_R;
                        }
                    }

                    // OSC合成
                    L = OSC1_L + OSC2_L + OSC_SUB_L;
                    R = OSC1_R + OSC2_R + OSC_SUB_R;

                    // アンプボリューム処理
                    calc_result_L += (((L * p_note->adsr_gain) >> 10) * p_note->gain) >> 10;
                    calc_result_R += (((R * p_note->adsr_gain) >> 10) * p_note->gain) >> 10;

                    p_osc1_phase = &p_note->osc1_phase[0];
                    p_osc2_phase = &p_note->osc2_phase[0];
                    p_osc1_phase_delta = &p_note->osc1_phase_delta[0];
                    p_osc2_phase_delta = &p_note->osc2_phase_delta[0];

                    // OSC1 次の位相へ
                    if(osc1_v == 1) {
                        *p_osc1_phase += *p_osc1_phase_delta;
                    }
                    else {
                        for(d = 0; d < osc1_v; ++d, ++p_osc1_phase, ++p_osc1_phase_delta) {
                            *p_osc1_phase += *p_osc1_phase_delta;
                        }
                    }
                    // OSC2 次の位相へ
                    if(osc2_v == 1) {
                        *p_osc2_phase += *p_osc2_phase_delta;
                    }
                    else {
                        for(d = 0; d < osc2_v; ++d, ++p_osc2_phase, ++p_osc2_phase_delta) {
                            *p_osc2_phase += *p_osc2_phase_delta;
                        }
                    }
                    // OSC SUB 次の位相へ
                    p_note->osc_sub_phase += p_note->osc_sub_phase_delta;

                } else {
                    noteReset();
                }
            }

            // core1を待つ
            while(calc_mode == CALC_NOTE);

            *p_buffer_L += calc_result_L;
            *p_buffer_R += calc_result_R;

            // core1で次Rのパン計算
            /*core1*/ calc_r = *p_buffer_R;
            /*core1*/ calc_mode = CALC_PAN_FILTER;

            // core1では処理できないのでこちらで処理
            p_note = &notes[0];
            p_cache = &cache[0];
            for (uint8_t n = 0; n < MAX_NOTES; ++n, ++p_note, ++p_cache) {
                // アタック終了したらディケイへ
                if (p_note->attack_cnt >= p_note->attack) {
                    p_note->attack_cnt = -1;
                    p_note->decay_cnt = p_note->decay;
                }

                // リリースが終了
                else if (p_note->release_cnt == 0 || p_note->force_release_cnt == 0) {
                    p_note->release_cnt = -1;
                    p_note->active = false;
                    p_note->note = 0xff;
                    p_note->gain = 0;

                    updateActNumOff(n); // 更新してから-1にする
                    p_note->actnum = -1;

                    if(!p_cache->processed) {
                        p_cache->processed = true;
                        noteOn(p_cache->note, p_cache->velocity, true, n);
                    }
                }

                // ディケイが終了
                else if (p_note->decay_cnt == 0) {
                    p_note->decay_cnt = -1;
                }
            }

            // パン処理
            *p_buffer_L = (*p_buffer_L * PAN_COS_TABLE[pan_local]) / INT16_MAX;

            // フィルタ処理
            if(lpf_enabled) {
                *p_buffer_L = lpfProcessL(*p_buffer_L);
            }
            if(hpf_enabled) {
                *p_buffer_L = hpfProcessL(*p_buffer_L);
            }

            // core1を待つ
            while(calc_mode == CALC_PAN_FILTER);

            *p_buffer_R = calc_r;

            // ディレイ処理
            if(delay_enabled) {
                *p_buffer_L = delayProcess(*p_buffer_L, 0x00);
                *p_buffer_R = delayProcess(*p_buffer_R, 0x01);
            }
        }
    }

    /**
     * @brief CORE1で負荷分散処理
     * 非CALC_IDLE時の変数アクセスに注意
     */
    void generate1() {
        if(calc_mode == CALC_IDLE) return;

        else if(calc_mode == CALC_NOTE) {

            // ローカル変数用
            uint8_t d;
            int16_t OSC1, OSC2, OSC_SUB;
            int16_t OSC1_L, OSC1_R;
            int16_t OSC2_L, OSC2_R;
            int16_t OSC_SUB_L, OSC_SUB_R;
            int16_t L, RM_L;
            int16_t R, RM_R;
            int16_t osc1_pre_level, osc2_pre_level, osc_sub_pre_level;
            int32_t adsr_gain;

            // 配列のキャッシュ用
            volatile Note* p_note;
            volatile uint32_t* p_osc1_phase;
            volatile uint32_t* p_osc2_phase;
            volatile uint32_t* p_osc1_phase_delta;
            volatile uint32_t* p_osc2_phase_delta;
            volatile uint32_t* p_osc1_glide_delta;
            volatile uint32_t* p_osc2_glide_delta;
            volatile int32_t (*p_osc1_spread_pan)[2];
            volatile int32_t (*p_osc2_spread_pan)[2];

            // 変数のキャッシュ
            uint8_t osc1_v = osc1_voice;
            uint8_t osc2_v = osc2_voice;
            uint16_t osc1_level_local = osc1_level;
            uint16_t osc2_level_local = osc2_level;
            uint16_t osc_sub_level_local = osc_sub_level;
            uint16_t osc_divide = calc_divide;

            // notesの先頭アドレス
            p_note = &notes[0];

            calc_result_L = 0;
            calc_result_R = 0;

            // 0, 2, 4...
            for (uint8_t n = 0; n < MAX_NOTES; n += 2, p_note += 2) {
                if (!p_note->active) continue;

                // 初期化
                OSC1_L = 0, OSC1_R = 0;
                OSC2_L = 0, OSC2_R = 0;
                OSC_SUB_L = 0, OSC_SUB_R = 0;

                // 配列の事前キャッシュ
                p_osc1_phase = &p_note->osc1_phase[0];
                p_osc2_phase = &p_note->osc2_phase[0];
                p_osc1_spread_pan = &osc1_spread_pan[0];
                p_osc2_spread_pan = &osc2_spread_pan[0];

                if (osc1_wave != nullptr || osc2_wave != nullptr || osc_sub_wave != nullptr) {

                    /**
                     * Amplifier + Envelope Generator
                     * エンベロープの状態から音量を計算します
                     */
                    {
                        // 基本レベル
                        adsr_gain = 0;

                        // アタック
                        if (p_note->attack_cnt >= 0 && p_note->attack_cnt < p_note->attack) {
                            adsr_gain = (p_note->attack_cnt << 10) / p_note->attack;
                            p_note->attack_cnt++;
                        }
                        // 強制リリース
                        else if (p_note->force_release_cnt >= 0) {
                            adsr_gain = (p_note->note_off_gain * p_note->force_release_cnt) / p_note->force_release;
                            if (p_note->force_release_cnt > 0) p_note->force_release_cnt--;
                        }
                        // リリース
                        else if (p_note->release_cnt >= 0) {
                            adsr_gain = (p_note->note_off_gain * p_note->release_cnt) / p_note->release;
                            if (p_note->release_cnt > 0) p_note->release_cnt--;
                        }
                        // ディケイ
                        else if (p_note->decay_cnt >= 0) {
                            adsr_gain = p_note->sustain + (p_note->level_diff * p_note->decay_cnt) / p_note->decay;
                            if (p_note->decay_cnt > 0) p_note->decay_cnt--;
                        }
                        // サステイン
                        else {
                            adsr_gain = p_note->sustain;
                        }

                        p_note->adsr_gain = adsr_gain;
                    }

                    /**
                     * Oscillator 1
                     * オシレーターで波形を生成します
                     */
                    if(osc1_wave != nullptr) {
                        if(osc1_v == 1) {
                            OSC1 = osc1_wave[(*p_osc1_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                            OSC1_L += OSC1;
                            OSC1_R += OSC1;
                        }
                        else {
                            uint16_t divide = DIVIDE_FIXED[osc1_v - 2];
                            for(d = 0; d < osc1_v; ++d, ++p_osc1_phase, ++p_osc1_spread_pan) {
                                OSC1 = ((osc1_wave[(*p_osc1_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)])*100) / divide;
                                OSC1_L += (OSC1 * (*p_osc1_spread_pan[0])) >> FIXED_SHIFT; // cos
                                OSC1_R += (OSC1 * (*p_osc1_spread_pan[1])) >> FIXED_SHIFT; // sin
                            }
                        }
                        // OSC1レベル処理
                        osc1_pre_level = (osc1_level_local*100) / osc_divide;
                        OSC1_L = (OSC1_L * (osc1_pre_level)) >> 10;
                        OSC1_R = (OSC1_R * (osc1_pre_level)) >> 10;
                    }

                    /**
                     * Oscillator 2
                     * オシレーターで波形を生成します
                     */
                    if(osc2_wave != nullptr) {
                        if(osc2_v == 1) {
                            OSC2 = osc2_wave[(*p_osc2_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                            OSC2_L += OSC2;
                            OSC2_R += OSC2;
                        }
                        else {
                            uint16_t divide = DIVIDE_FIXED[osc2_v - 2];
                            for(d = 0; d < osc2_v; ++d, ++p_osc2_phase, ++p_osc2_spread_pan) {
                                OSC2 = ((osc2_wave[(*p_osc2_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)])*100) / divide;
                                OSC2_L += (OSC2 * (*p_osc2_spread_pan[0])) >> FIXED_SHIFT; // cos
                                OSC2_R += (OSC2 * (*p_osc2_spread_pan[1])) >> FIXED_SHIFT; // sin
                            }
                        }
                        // OSC2レベル処理
                        osc2_pre_level = (osc2_level_local*100) / osc_divide;
                        OSC2_L = (OSC2_L * (osc2_pre_level)) >> 10;
                        OSC2_R = (OSC2_R * (osc2_pre_level)) >> 10;
                    }

                    /**
                     * Oscillator SUB
                     * オシレーターで波形を生成します
                     */
                    if(osc_sub_wave != nullptr) {
                        OSC_SUB = osc_sub_wave[(p_note->osc_sub_phase >> BIT_SHIFT) & (SAMPLE_SIZE - 1)];
                        OSC_SUB_L += OSC_SUB;
                        OSC_SUB_R += OSC_SUB;
                        // OSC_SUBレベル処理
                        osc_sub_pre_level = ((osc_sub_level_local*100) / osc_divide);
                        OSC_SUB_L = (OSC_SUB_L * (osc_sub_pre_level)) >> 10;
                        OSC_SUB_R = (OSC_SUB_R * (osc_sub_pre_level)) >> 10;
                    }

                    // 合成用変数 初期化
                    L = 0, RM_L = 0;
                    R = 0, RM_R = 0;

                    // リングモジュレーション
                    if(ring_modulation) {
                        if(osc1_wave != nullptr && osc2_wave != nullptr) {
                            RM_L = (OSC1_L * OSC2_L) / 16384;
                            RM_R = (OSC1_R * OSC2_R) / 16384;
                            OSC1_L = (OSC1_L + OSC2_L) / 2;
                            OSC1_R = (OSC1_R + OSC2_R) / 2;
                            OSC2_L = RM_L;
                            OSC2_R = RM_R;
                        }
                    }

                    // OSC合成
                    L = OSC1_L + OSC2_L + OSC_SUB_L;
                    R = OSC1_R + OSC2_R + OSC_SUB_R;

                    // アンプボリューム処理
                    calc_result_L += (((L * p_note->adsr_gain) >> 10) * p_note->gain) >> 10;
                    calc_result_R += (((R * p_note->adsr_gain) >> 10) * p_note->gain) >> 10;

                    p_osc1_phase = &p_note->osc1_phase[0];
                    p_osc2_phase = &p_note->osc2_phase[0];
                    p_osc1_phase_delta = &p_note->osc1_phase_delta[0];
                    p_osc2_phase_delta = &p_note->osc2_phase_delta[0];
                    p_osc1_glide_delta = &p_note->osc1_glide_delta[0];
                    p_osc2_glide_delta = &p_note->osc2_glide_delta[0];

                    // glideモードかつisGlidedかつモノフォニックの場合
                    if(glide_mode && isGlided && monophonic) {
                        // OSC1 次の位相へ
                        if(osc1_v == 1) {
                            *p_osc1_glide_delta = lerp(*p_osc1_glide_delta, *p_osc1_phase_delta, 1.0f / (glide_time * SAMPLE_RATE / 1000.0f));
                            *p_osc1_phase += *p_osc1_glide_delta;
                        }
                        else {
                            for(d = 0; d < osc1_v; ++d, ++p_osc1_phase, ++p_osc1_phase_delta, ++p_osc1_glide_delta) {
                                *p_osc1_glide_delta = lerp(*p_osc1_glide_delta, *p_osc1_phase_delta, 1.0f / (glide_time * SAMPLE_RATE / 1000.0f));
                                *p_osc1_phase += *p_osc1_glide_delta;
                            }
                        }
                        // OSC2 次の位相へ
                        if(osc2_v == 1) {
                            *p_osc2_glide_delta = lerp(*p_osc2_glide_delta, *p_osc2_phase_delta, 1.0f / (glide_time * SAMPLE_RATE / 1000.0f));
                            *p_osc2_phase += *p_osc2_glide_delta;
                        }
                        else {
                            for(d = 0; d < osc2_v; ++d, ++p_osc2_phase, ++p_osc2_phase_delta) {
                                *p_osc2_glide_delta = lerp(*p_osc2_glide_delta, *p_osc2_phase_delta, 1.0f / (glide_time * SAMPLE_RATE / 1000.0f));
                                *p_osc2_phase += *p_osc2_glide_delta;
                            }
                        }
                        // OSC SUB 次の位相へ
                        p_note->osc_sub_glide_delta = lerp(p_note->osc_sub_glide_delta, p_note->osc_sub_phase_delta, 1.0f / (glide_time * SAMPLE_RATE / 1000.0f));
                        p_note->osc_sub_phase += p_note->osc_sub_glide_delta;
                    }

                    // isGlidedではない場合
                    else {
                        // glidemodeならphase_deltaをキャッシュし、isGlidedをtrueにする。
                        if(glide_mode && !isGlided && monophonic) {
                            if(osc1_v == 1) {
                                *p_osc1_glide_delta = *p_osc1_phase_delta;
                            }
                            else {
                                for(d = 0; d < osc1_v; ++d, ++p_osc1_phase_delta, ++p_osc1_glide_delta) {
                                    *p_osc1_glide_delta = *p_osc1_phase_delta;
                                }
                            }
                            // OSC2 次の位相へ
                            if(osc2_v == 1) {
                                *p_osc2_glide_delta = *p_osc2_phase_delta;
                            }
                            else {
                                for(d = 0; d < osc2_v; ++d, ++p_osc2_phase_delta, ++p_osc2_glide_delta) {
                                    *p_osc2_glide_delta = *p_osc2_phase_delta;
                                }
                            }
                            p_note->osc_sub_glide_delta = p_note->osc_sub_phase_delta;

                            p_osc1_phase_delta = &p_note->osc1_phase_delta[0];
                            p_osc2_phase_delta = &p_note->osc2_phase_delta[0];
                            p_osc1_glide_delta = &p_note->osc1_glide_delta[0];
                            p_osc2_glide_delta = &p_note->osc2_glide_delta[0];
                            isGlided = true;
                        }

                        // OSC1 次の位相へ
                        if(osc1_v == 1) {
                            *p_osc1_phase += *p_osc1_phase_delta;
                        }
                        else {
                            for(d = 0; d < osc1_v; ++d, ++p_osc1_phase, ++p_osc1_phase_delta) {
                                *p_osc1_phase += *p_osc1_phase_delta;
                            }
                        }
                        // OSC2 次の位相へ
                        if(osc2_v == 1) {
                            *p_osc2_phase += *p_osc2_phase_delta;
                        }
                        else {
                            for(d = 0; d < osc2_v; ++d, ++p_osc2_phase, ++p_osc2_phase_delta) {
                                *p_osc2_phase += *p_osc2_phase_delta;
                            }
                        }
                        // OSC SUB 次の位相へ
                        p_note->osc_sub_phase += p_note->osc_sub_phase_delta;
                    }

                } else {
                    noteReset();
                }
            }

            calc_mode = CALC_IDLE;
        }

        else if(calc_mode == CALC_SET_F) {
            setFrequency(calc_i);
            calc_mode = CALC_IDLE;
        }

        else if(calc_mode == CALC_PAN_FILTER) {

            uint8_t calc_pan = pan;

            // パン処理
            calc_r = (calc_r * PAN_SIN_TABLE[calc_pan]) / INT16_MAX;

            // フィルタ処理
            if(lpf_enabled) {
                calc_r = lpfProcessR(calc_r);
            }
            if(hpf_enabled) {
                calc_r = hpfProcessR(calc_r);
            }

            calc_mode = CALC_IDLE;
        }
    }
};