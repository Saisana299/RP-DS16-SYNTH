#include <Arduino.h>

class WaveGenerator {
private:
    uint32_t phase;
    uint32_t phase_delta;
    float volume_gain;
    const int32_t sample_rate;
    uint8_t preset = 0x01; //todo

    int16_t sine(uint32_t phase) {
        return 0;
    }

    int16_t pulse(uint32_t phase) {
        int16_t value = (phase < (1U << 31)) ? 32767 : -32767;
        return constrain(value * volume_gain, -32768, 32767);
    }

    int16_t triangle(uint32_t phase) {
        phase += (1 << 30);
        int16_t value = ((phase < (uint32_t)(1 << 31)) ? (phase >> 16) : ((1 << 16) - (phase >> 16) - 1)) - 16383;
        return constrain(value * volume_gain, -32768, 32767);
    }

public:
    WaveGenerator(int32_t rate = 48000, float gain = 2.0f)
        : phase(0), phase_delta(0), volume_gain(gain), sample_rate(rate) {}

    void setFrequency(float frequency) {
        phase_delta = frequency * (float)(1ULL << 32) / sample_rate;
    }

    void generate(int16_t *buffer, size_t size) {
        for (size_t i = 0; i < size; i++) {
            if(preset == 0x00) buffer[i] = sine(phase);
            else if(preset == 0x01) buffer[i] = pulse(phase);
            else if(preset == 0x02) buffer[i] = triangle(phase);
            phase += phase_delta;
        }
    }

    void resetPhase() {
        phase = 0;
    }

    void applyFadeIn(int16_t *buffer, size_t size, int8_t fade_in_samples) {
        for (size_t i = 0; i < size && i < fade_in_samples; ++i) {
            float fade_gain = (float)i / fade_in_samples;
            buffer[i] *= fade_gain;
        }
    }

    void applyFadeOut(int16_t *buffer, size_t size, int8_t fade_out_samples) {
        if (size < fade_out_samples) return;

        for (size_t i = size - fade_out_samples; i < size; ++i) {
            float fade_gain = (float)(size - i) / fade_out_samples;
            buffer[i] *= fade_gain;
        }
    }

    void setPreset(uint8_t id) {
        preset = id;
    }
};