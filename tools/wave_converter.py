import wave
import numpy as np

filename = "wavetable.wav"

def read_and_normalize_wave(filename):
    """Read a wave file and normalize its data to fit in the range -16383 to 16384."""
    with wave.open(filename, 'r') as wav_file:
        # チャネル数, サンプル幅, フレーム数を取得
        n_channels = wav_file.getnchannels()
        sampwidth = wav_file.getsampwidth()
        nframes = wav_file.getnframes()
        
        # フレームデータを読み込む
        data = wav_file.readframes(nframes)
        
        # サンプル幅に基づいてデータ型を決定
        dtype = np.int16 if sampwidth == 2 else np.int32
        wave_data = np.frombuffer(data, dtype=dtype)
        
        # マルチチャネルの場合、チャネルごとに分ける
        if n_channels > 1:
            wave_data = wave_data.reshape(-1, n_channels)
        
        # データを-1.0から1.0にスケーリング
        max_val = np.max(np.abs(wave_data))
        normalized_data = wave_data / max_val
        
        # データを-16383から16384にスケーリング
        scaled_data = normalized_data * 16384
        scaled_data = np.clip(scaled_data, -16383, 16384)  # 値を範囲内にクリップ
        scaled_data = np.round(scaled_data).astype(np.int16)  # 整数に変換
    
    return scaled_data

# Read and normalize the wave file data
normalized_wave_data = read_and_normalize_wave(filename)

comma_separated_str = ", ".join(map(str, normalized_wave_data.flatten()))
print(comma_separated_str)

#[-16383, 16384]