import numpy as np
import wave
import struct

def generate_single_cycle_triangle_wave_modified(samples):
    """Generate a modified single cycle of a triangle wave starting and ending at 0.0."""
    wave = 2 * np.abs(2 * (np.linspace(0, 1, samples, endpoint=False) - 0.5)) - 1
    # Adjusting the wave to start and end at 0.0
    wave = np.roll(wave, -samples // 4)
    return wave

def write_wavefile(filename, data, sample_rate, num_channels, sampwidth):
    """Write the wave data to a file."""
    with wave.open(filename, 'w') as wav_file:
        wav_file.setparams((num_channels, sampwidth, sample_rate, len(data), "NONE", "not compressed"))
        for value in data:
            packed_value = struct.pack('f', value)  # 32-bit float
            wav_file.writeframes(packed_value)

# Parameters
samples = 2048  # Number of samples in a single cycle of the wave
sample_rate = 48000  # Sample rate in Hz
filename = "wavetable.wav"

# Generate a single cycle of a triangle wave
triangle_wave = generate_single_cycle_triangle_wave_modified(samples)

# Write to WAV file
write_wavefile(filename, triangle_wave, sample_rate, 1, 4)

filename
