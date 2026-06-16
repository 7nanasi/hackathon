// ============================================================================
//  master.ino — 指揮者 Arduino（I²C マスター）/ きらきら星 輪唱システム
// ----------------------------------------------------------------------------
//  役割（通信方式説明書 §2 に準拠）:
//   起動時に各楽器スレーブへ CONFIG（パート設定）→ START（演奏許可）を送り、
//   その後は小節の頭ごとに SYNC（現在の小節番号とテンポ）を全スレーブへ送り続ける。
//
//  I²C コマンド:
//   CONFIG 0x11 : [0x11][entry_offset][loop_length][part_id]  起動時に各スレーブへ個別
//   START  0x10 : [0x10]                                      起動時に全スレーブへ
//   SYNC   0x01 : [0x01][bar_H][bar_L][bpm_H][bpm_L]          小節頭ごとに全スレーブへ
//
//  配線（Arduino Uno）: SDA=A4, SCL=A5 を全 Arduino で共通バスに接続し、GND も共通にする。
// ============================================================================

#include <Wire.h>

// ---- コマンド定数 ----
#define CMD_SYNC   0x01
#define CMD_START  0x10
#define CMD_CONFIG 0x11

// ---- スレーブ構成（通信方式説明書の装置表どおり）----
// アドレス・entry_offset（演奏開始小節）・part_id（=Serialの楽器種別 1/2/3）を対応させる
const uint8_t SLAVE_ADDR[]   = { 0x10, 0x11, 0x12 }; // フルート / クラリネット / オルガン
const uint8_t ENTRY_OFFSET[] = { 0,    2,    4    }; // 0小節 / 2小節遅れ / 4小節遅れで参加
const uint8_t PART_ID[]      = { 1,    2,    3    }; // 種別 1=フルート 2=クラリネット 3=オルガン
const uint8_t NUM_SLAVES = 3;

// ---- 曲の設定 ----
const uint8_t  LOOP_LENGTH = 12;   // きらきら星は 4/4・12小節（各スレーブはこの数で折り返す）
uint16_t       bpmX10      = 1200; // テンポ ×10（1200 = 120.0 BPM）。ここを変えると速さが変わる

// ---- 進行状態 ----
uint16_t      globalBar  = 0;  // 今が第何小節か（0 から数える）
unsigned long nextSyncUs = 0;  // 次に SYNC を送る予定時刻（micros基準）

// 1小節のマイクロ秒を BPM×10 から計算する（4/4拍子 → 1小節 = 4拍）
// 例: 120BPM → 1拍0.5秒 → 1小節2秒 = 2,000,000μs
unsigned long calcBarUs(uint16_t bpm_x10) {
  return 60000000UL * 4UL * 10UL / bpm_x10;
}

// CONFIG を各スレーブへ個別送信する（entry_offset 等の初期設定）
void sendConfigAll() {
  for (uint8_t i = 0; i < NUM_SLAVES; i++) {
    Wire.beginTransmission(SLAVE_ADDR[i]);
    Wire.write(CMD_CONFIG);
    Wire.write(ENTRY_OFFSET[i]);
    Wire.write(LOOP_LENGTH);
    Wire.write(PART_ID[i]);
    Wire.endTransmission();
    Serial.print("CONFIG -> 0x");
    Serial.print(SLAVE_ADDR[i], HEX);
    Serial.print(" offset=");
    Serial.print(ENTRY_OFFSET[i]);
    Serial.print(" part=");
    Serial.println(PART_ID[i]);
    delay(5); // スレーブの処理が追いつくよう少し待つ
  }
}

// START を全スレーブへ送信する（演奏許可）
void sendStartAll() {
  for (uint8_t i = 0; i < NUM_SLAVES; i++) {
    Wire.beginTransmission(SLAVE_ADDR[i]);
    Wire.write(CMD_START);
    Wire.endTransmission();
    delay(5);
  }
  Serial.println("START -> all");
}

// SYNC を全スレーブへ送信する（小節番号とテンポを通知）
// 1対多の同時送信ではなく各アドレスへ順に送る。I²C 有線の遅延は数百μsで可聴差はない。
void sendSyncToAll(uint16_t bar, uint16_t bpm_x10) {
  for (uint8_t i = 0; i < NUM_SLAVES; i++) {
    Wire.beginTransmission(SLAVE_ADDR[i]);
    Wire.write(CMD_SYNC);
    Wire.write((uint8_t)(bar >> 8));      // bar 上位
    Wire.write((uint8_t)(bar & 0xFF));    // bar 下位
    Wire.write((uint8_t)(bpm_x10 >> 8));  // bpm 上位
    Wire.write((uint8_t)(bpm_x10 & 0xFF));// bpm 下位
    Wire.endTransmission();
  }
}

void setup() {
  Wire.begin();          // アドレス指定なし = I²C マスターとして開始
  Serial.begin(115200);  // 指揮者の動作確認ログ用（演奏には使わない）
  delay(1000);           // スレーブの起動を待つ

  sendConfigAll();
  sendStartAll();

  // 最初の SYNC を即送って、以後は 1小節ぶんずつ加算した理想時刻に送る
  nextSyncUs = micros();
}

void loop() {
  unsigned long now = micros();

  // 予定時刻に達したら SYNC を送る。
  // 符号付きで差を取るのは micros() のオーバーフロー（約70分周期）対策。
  if ((long)(now - nextSyncUs) >= 0) {
    sendSyncToAll(globalBar, bpmX10);

    Serial.print("SYNC bar=");
    Serial.print(globalBar);
    Serial.print(" bpm=");
    Serial.println(bpmX10 / 10.0);

    globalBar++;
    // 「理想時刻系列」に加算していくのでドリフト（誤差の蓄積）が起きない
    nextSyncUs += calcBarUs(bpmX10);
  }
}
