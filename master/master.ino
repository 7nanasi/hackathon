// 指揮者 Arduino — マスター
//
// 起動時: CONFIG → START を送信
// 以降:   小節頭ごとに SYNC を定期送信
//
// コマンドバイト:
//   SYNC   = 0x01  [CMD][bar_H][bar_L][bpm_H][bpm_L]  5バイト
//   START  = 0x10  [CMD]                               1バイト
//   CONFIG = 0x11  [CMD][entry_offset][loop_length][part_id]  4バイト

// I²C 通信ライブラリ
#include <Wire.h>

// ---- コマンド定数 ----
// スレーブが受け取ったとき、最初の1バイトを見てコマンドの種類を判断する
#define CMD_SYNC   0x01  // 小節頭の通知（定期送信）
#define CMD_START  0x10  // 演奏開始の合図
#define CMD_CONFIG 0x11  // パート設定（entry_offset など）

// ---- スレーブ設定 ----
#define SLAVE_ADDR   0x10  // 送信先スレーブの I²C アドレス（楽器A）
#define ENTRY_OFFSET 0     // 何小節目から演奏を始めるか（0 = 最初から）
#define LOOP_LENGTH  16    // 1ループの小節数
#define PART_ID      1     // パート番号（1=楽器A, 2=楽器B, 3=楽器C）

// ---- テンポ設定 ----
// BPM を 10倍した整数で管理する（小数点以下を整数で表現するため）
// 例: 120.0 BPM → 1200
#define BPM_X10 1200

// ---- 状態変数 ----
uint16_t      global_bar = 0;      // 曲全体の先頭からの通算小節番号
uint16_t      bpmX10     = BPM_X10;
unsigned long nextSyncUs = 0;      // 次に SYNC を送るべき時刻（マイクロ秒）

// 1小節分のマイクロ秒を BPM×10 から計算する関数
// 4/4拍子なので 1小節 = 4拍
// 計算式: 60,000,000μs × 4拍 × 10 ÷ BPM×10
// 例: BPM=120 → 60,000,000×4×10 / 1200 = 2,000,000μs（= 2秒）
unsigned long calcBarUs(uint16_t bpm_x10) {
  return 60000000UL * 4UL * 10UL / (unsigned long)bpm_x10;
}

// CONFIG をスレーブに送る関数
// 「お前のパート設定はこれだ」と個別に伝える（起動時1回だけ）
void sendConfig(uint8_t addr, uint8_t entry_offset, uint8_t loop_len, uint8_t part_id) {
  Wire.beginTransmission(addr);        // 送信先アドレスを指定して送信開始
  Wire.write(CMD_CONFIG);              // 1バイト目: コマンド種別
  Wire.write(entry_offset);            // 2バイト目: 演奏開始小節
  Wire.write(loop_len);                // 3バイト目: ループ長
  Wire.write(part_id);                 // 4バイト目: パート番号
  uint8_t err = Wire.endTransmission();  // 送信確定（0=成功）
  Serial.print("[CONFIG] addr=0x");
  Serial.print(addr, HEX);
  Serial.print(" offset=");
  Serial.print(entry_offset);
  Serial.print(" -> ");
  Serial.println(err == 0 ? "OK" : "ERR");
}

// START をスレーブに送る関数
// 「今から SYNC を受け取ったら演奏を始めていい」という許可
void sendStart(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(CMD_START);  // 1バイトだけ送る
  uint8_t err = Wire.endTransmission();
  Serial.print("[START]  addr=0x");
  Serial.print(addr, HEX);
  Serial.print(" -> ");
  Serial.println(err == 0 ? "OK" : "ERR");
}

// SYNC をスレーブに送る関数
// 「今ちょうど第 bar 小節の頭だ。テンポは bpm_x10 だ」と通知する
void sendSync(uint8_t addr, uint16_t bar, uint16_t bpm_x10) {
  Wire.beginTransmission(addr);
  Wire.write(CMD_SYNC);
  // bar は 2バイト（uint16_t）なので上位バイトと下位バイトに分けて送る
  Wire.write((uint8_t)(bar >> 8));        // bar の上位8ビット
  Wire.write((uint8_t)(bar & 0xFF));      // bar の下位8ビット
  // BPM×10 も同様に2バイトに分けて送る
  Wire.write((uint8_t)(bpm_x10 >> 8));
  Wire.write((uint8_t)(bpm_x10 & 0xFF));
  uint8_t err = Wire.endTransmission();
  Serial.print("[SYNC]   bar=");
  Serial.print(bar);
  Serial.print(" bpm=");
  Serial.print(bpm_x10 / 10);   // 整数部
  Serial.print(".");
  Serial.print(bpm_x10 % 10);   // 小数部
  Serial.print(" -> ");
  Serial.println(err == 0 ? "OK" : "ERR");
}

void setup() {
  Wire.begin();           // マスターとして I²C 開始（アドレス指定なし）
  Serial.begin(115200);
  delay(500);             // スレーブの起動を待つ
  Serial.println("=== Master start ===");

  // 起動時シーケンス: まず CONFIG、少し待ってから START
  sendConfig(SLAVE_ADDR, ENTRY_OFFSET, LOOP_LENGTH, PART_ID);
  delay(10);
  sendStart(SLAVE_ADDR);
  delay(10);

  // 最初の SYNC をすぐ送れるよう、次の送信時刻を「今」に設定
  nextSyncUs = micros();
}

void loop() {
  unsigned long now = micros();  // 現在時刻（マイクロ秒）

  // 送信時刻になったら SYNC を送る
  // ※ (long)(now - nextSyncUs) >= 0 という書き方は
  //   micros() が約70分でゼロに戻る（オーバーフロー）対策
  //   単純に now >= nextSyncUs と書くとオーバーフロー時に誤動作する
  if ((long)(now - nextSyncUs) >= 0) {
    sendSync(SLAVE_ADDR, global_bar, bpmX10);
    global_bar++;

    // 次の送信時刻を「今の時刻 + 1小節分」ではなく「前の時刻 + 1小節分」で計算する
    // こうすることで処理時間のズレが蓄積しない（ドリフト防止）
    nextSyncUs += calcBarUs(bpmX10);
  }
}
