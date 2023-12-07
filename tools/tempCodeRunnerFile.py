import wave
import numpy as np

filename = "wavetable.wav"

def read_and_normalize_wave(filename):
    """Read a wave file and normalize its data to fit in the range -16383 to 16384."""
    with wave.open(filename, 'r') as wav_file:
        # Read the wave file data
        nframes = wav_file.getnframes()
        data = wav_file.readframes(nframes)
        # Convert to numpy array and normalize
        wave_data = np.frombuffer(data, dtype=np.float32)
        normalized_data = np.int16(wave_data * 16384)
    return normalized_data

# Read and normalize the wave file data
normalized_wave_data = read_and_normalize_wave(filename)

comma_separated_str = ", ".join(map(str, normalized_wave_data))
print(comma_separated_str)

#[-16383, 16384]