//// 共通システムコード (予約済み) 127~

#define INS_BEGIN     0x7F // 命令送信ビット
#define DATA_BEGIN    0x80 // データ送信ビット
#define DATA_SEPARATE 0x81 // データ区切り
#define RES_OK        0x82 // 処理正常終了
#define RES_ERROR     0x83 // エラー発生時

//// CTRL受信用の命令コード (0x00と0xffは除外) 190~
//// 例：{0x7F, 0x40, 0x80, 0x01(データサイズ 1~255), 0x04(データ)}
////
//// データサイズは255が最大だが区切りを利用するとさらに多くのデータを送れます。
//// 例：{..., 0x80, 0xff, ..., 0x84, 0xff, ...}

#define SYNTH_NOTE_ON    0xBE // ノートオン
#define SYNTH_NOTE_OFF   0xBF // ノートオフ
#define SYNTH_SET_PRESET 0xC0 // プリセット設定
#define SYNTH_SOUND_STOP 0xC1 // 音の再生を停止する
#define SYNTH_SET_PAN    0xC2 // パン(C/L/R)を設定