import wave
import numpy as np

def wave_to_array(file_path):
    # waveファイルを開く
    with wave.open(file_path, 'rb') as wave_file:
        # チャネル数, サンプル幅, サンプリングレート, フレーム数などを取得
        n_channels = wave_file.getnchannels()
        sampwidth = wave_file.getsampwidth()
        framerate = wave_file.getframerate()
        n_frames = wave_file.getnframes()
        
        # フレームデータを読み込む
        frames = wave_file.readframes(n_frames)
        
        # numpy配列に変換
        dtype = np.int16 if sampwidth == 2 else np.int32
        audio_data = np.frombuffer(frames, dtype=dtype)
        
        # マルチチャネルの場合、チャネルごとに分ける
        if n_channels > 1:
            audio_data = audio_data.reshape(-1, n_channels)
    
    return audio_data

file_path = 'wavetable.wav'
audio_array = wave_to_array(file_path)

# 配列の要素数をコンソールに出力
element_count = audio_array.size
print(f"Element Count: {element_count}")

np.set_printoptions(threshold=np.inf)
print(",".join(map(str, audio_array.flatten())))
