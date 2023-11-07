#include <Arduino.h>

class WaveGenerator {
private:
    uint32_t phase;
    uint32_t phase_delta;
    float volume_gain;
    const int32_t sample_rate;

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
            buffer[i] = triangle(phase);
            phase += phase_delta;
        }
    }

    void resetPhase() {
        phase = 0;
    }
};