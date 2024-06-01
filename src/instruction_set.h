//// 共通システムコード (予約済み) 127~
#define INS_BEGIN     0x7F // 命令送信ビット
#define DATA_BEGIN    0x80 // データ送信ビット
#define DATA_SEPARATE 0x81 // データ区切り
#define RES_OK        0x82 // 処理正常終了
#define RES_ERROR     0x83 // エラー発生時
#define DATA_END      0x84 // データ終了


//// CTRL用の命令コード (0x00と0xffは除外) 63~
//// 例：{0x7F, 0x40, 0x80, 0x01(データサイズ 1~255), 0x04(データ)}
////
//// データサイズは255が最大だが区切りを利用するとさらに多くのデータを送れます。
//// 例：{..., 0x80, 0xff, ..., 0x84, 0xff, ...}
#define CTRL_CONNECT     0x3F // 接続開始
#define CTRL_SET_SYNTH   0x40 // シンセモード設定
#define CTRL_RESET_SYNTH 0x41 // シンセをリセット
#define CTRL_DEBUG_ON    0x42 // デバッグモードを有効化
#define CTRL_DEBUG_DATA  0x43 // デバッグ用データ
#define CTRL_STOP_SYNTH  0x44 // シンセの制御を停止する
#define CTRL_START_SYNTH 0x45 // シンセの制御を開始する
#define CTRL_MIDI_ON     0x46 // DISP->CTRL MIDIモードON
#define CTRL_MIDI_OFF    0x47 // DISP->CTRL MIDIモードOFF

//// SYNTH用の命令コード (0x00と0xffは除外) 190~
#define SYNTH_NOTE_ON     0xBE // ノートオン
#define SYNTH_NOTE_OFF    0xBF // ノートオフ
#define SYNTH_SET_SHAPE   0xC0 // 基本波形設定
#define SYNTH_SOUND_STOP  0xC1 // 音の再生を停止する
#define SYNTH_SET_PAN     0xC2 // パンを設定
#define SYNTH_SET_ATTACK  0xC3 // アタックを設定
#define SYNTH_SET_RELEASE 0xC4 // リリースを設定
#define SYNTH_SET_DECAY   0xC5 // ディケイを設定
#define SYNTH_SET_SUSTAIN 0xC6 // サステインを設定
#define SYNTH_GET_USED    0xC7 // 鳴っているノート数を取得
#define SYNTH_IS_NOTE     0xC8 // ノートが存在するか
#define SYNTH_SET_CSHAPE  0xC9 // カスタムシェイプを設定
#define SYNTH_SET_VOICE   0xCA // ボイスを設定
#define SYNTH_SET_DETUNE  0xCB // デチューンを設定
#define SYNTH_SET_SPREAD  0xCC // スプレッドを設定
#define SYNTH_SET_OCT     0xCD // オクターブを設定
#define SYNTH_SET_SEMI    0xCE // セミトーンを設定
#define SYNTH_SET_CENT    0xCF // セントを設定
#define SYNTH_SET_LEVEL   0xD0 // レベルを設定
#define SYNTH_SET_OSC_LVL 0xD1 // OSCレベルを設定

//// 共通シンセ演奏状態コード
#define SYNTH_SINGLE 0x00
#define SYNTH_OCTAVE 0x01
#define SYNTH_DUAL 0x02
#define SYNTH_MULTI  0x03