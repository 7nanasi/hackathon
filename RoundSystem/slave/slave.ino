// ============================================================================
//  slave.ino — 楽器 Arduino（I²C スレーブ）/ きらきら星 輪唱システム
// ----------------------------------------------------------------------------
//  ★ボードごとに MY_ADDR を変えて焼くこと:
//      フルート     → 0x10
//      クラリネット → 0x11
//      オルガン     → 0x12
//  （楽器種別は CONFIG で受け取る part_id をそのまま使うのでコード変更は MY_ADDR だけ）
//
//  動作（通信方式説明書 §3・§4 に準拠）:
//   CONFIG 受信 → entry_offset / loop_length / part_id を保存
//   START  受信 → SYNC 待機モードへ
//   SYNC   受信 → 受信時刻を小節開始としてその小節の音符を発火予約（スナップ方式）
//   メインループ → 発火時刻が来た音符を 5バイトパケットで PC(Processing) へ送信
//
//  Serial パケット（5バイト固定）:
//   [0xAA][part_id(=種別)][pitch(MIDI)][velocity=80][duration_8ms]
// ============================================================================

#include <Wire.h>
#include <avr/pgmspace.h>

// ★このボードの I²C アドレス（フルート0x10 / クラリネット0x11 / オルガン0x12）
#define MY_ADDR 0x10

// ---- コマンド定数 ----
#define CMD_SYNC   0x01
#define CMD_START  0x10
#define CMD_CONFIG 0x11

// ---- Serial パケット ----
#define MARKER    0xAA
#define VELOCITY  80    // 通信方式説明書どおり velocity は 80 固定

// ============================================================================
//  楽譜データ（きらきら星 4/4・12小節）
//  1音 = { 小節, 拍(0..3), MIDI番号, 長さ(拍) }。PROGMEM でフラッシュに置く。
//  C4=60 D4=62 E4=64 F4=65 G4=67 A4=69
// ============================================================================
struct Note {
  uint8_t bar;       // 何小節目（0始まり）
  uint8_t beat;      // 小節内の拍位置（0..3）
  uint8_t pitch;     // MIDI ノート番号
  uint8_t durBeats;  // 長さ（拍）
};

const Note NOTES[] PROGMEM = {
  // bar0: ド ド ソ ソ
  {0,0,60,1},{0,1,60,1},{0,2,67,1},{0,3,67,1},
  // bar1: ラ ラ ソ(2拍)
  {1,0,69,1},{1,1,69,1},{1,2,67,2},
  // bar2: ファ ファ ミ ミ
  {2,0,65,1},{2,1,65,1},{2,2,64,1},{2,3,64,1},
  // bar3: レ レ ド(2拍)
  {3,0,62,1},{3,1,62,1},{3,2,60,2},
  // bar4: ソ ソ ファ ファ
  {4,0,67,1},{4,1,67,1},{4,2,65,1},{4,3,65,1},
  // bar5: ミ ミ レ(2拍)
  {5,0,64,1},{5,1,64,1},{5,2,62,2},
  // bar6: ソ ソ ファ ファ
  {6,0,67,1},{6,1,67,1},{6,2,65,1},{6,3,65,1},
  // bar7: ミ ミ レ(2拍)
  {7,0,64,1},{7,1,64,1},{7,2,62,2},
  // bar8: ド ド ソ ソ
  {8,0,60,1},{8,1,60,1},{8,2,67,1},{8,3,67,1},
  // bar9: ラ ラ ソ(2拍)
  {9,0,69,1},{9,1,69,1},{9,2,67,2},
  // bar10: ファ ファ ミ ミ
  {10,0,65,1},{10,1,65,1},{10,2,64,1},{10,3,64,1},
  // bar11: レ レ ド(2拍)
  {11,0,62,1},{11,1,62,1},{11,2,60,2},
};
const uint8_t NUM_NOTES = sizeof(NOTES) / sizeof(NOTES[0]);

// ---- I²C 受信バッファ（ISR が詰める。volatile 必須）----
#define RX_BUF_SIZE 8
volatile uint8_t rxBuf[RX_BUF_SIZE];
volatile uint8_t rxLen   = 0;
volatile bool    newData = false;

// ---- パート設定（CONFIG で上書き）----
uint8_t entryOffset = 0;
uint8_t loopLength  = 12;
uint8_t partId      = 0;   // 1/2/3 = フルート/クラリネット/オルガン。Serial の種別バイトに使う

// ---- 演奏状態 ----
bool started = false;

// ---- 発火予約スロット（1小節に最大4音）----
#define MAX_SCHED 8
struct Sched {
  unsigned long fireUs;  // この時刻に送信する
  uint8_t  pitch;        // MIDI番号
  uint16_t durMs;        // 音の長さ(ms)
  bool     active;       // 予約が有効か
};
Sched sched[MAX_SCHED];

// I²C 受信割り込み。重い処理は禁止なのでバッファに詰めてフラグを立てるだけ。
void receiveEvent(int numBytes) {
  rxLen = 0;
  while (Wire.available() && rxLen < RX_BUF_SIZE) {
    rxBuf[rxLen++] = Wire.read();
  }
  while (Wire.available()) Wire.read(); // 溢れは読み捨て
  newData = true;
}

void setup() {
  Wire.begin(MY_ADDR);
  Wire.onReceive(receiveEvent);
  Serial.begin(115200);
  delay(300);
}

// 指定小節の音符を発火予約に積む。
//  targetBar    : この小節（loopLength で折り返し済み）の音符を読む
//  barStartUs   : この小節の開始時刻（SYNC 受信時刻）
//  beatUs       : 1拍のマイクロ秒
void loadBarNotes(uint8_t targetBar, unsigned long barStartUs, unsigned long beatUs) {
  // いったん全予約をクリア
  for (uint8_t i = 0; i < MAX_SCHED; i++) sched[i].active = false;

  uint8_t slot = 0;
  for (uint8_t i = 0; i < NUM_NOTES && slot < MAX_SCHED; i++) {
    uint8_t bar = pgm_read_byte(&NOTES[i].bar);
    if (bar != targetBar) continue;

    uint8_t beat     = pgm_read_byte(&NOTES[i].beat);
    uint8_t pitch    = pgm_read_byte(&NOTES[i].pitch);
    uint8_t durBeats = pgm_read_byte(&NOTES[i].durBeats);

    sched[slot].fireUs = barStartUs + (unsigned long)beat * beatUs;
    sched[slot].pitch  = pitch;
    // 実音は拍長の 90% にして、同じ音が続いても切れ目が出るようにする
    sched[slot].durMs  = (uint16_t)((unsigned long)durBeats * beatUs / 1000UL * 9UL / 10UL);
    sched[slot].active = true;
    slot++;
  }
}

// 1音を 5バイトパケットで PC へ送信する
void sendNoteEvent(uint8_t pitch, uint16_t durMs) {
  uint16_t d8 = durMs / 8;            // duration_8ms へ（÷8）
  if (d8 < 1)   d8 = 1;               // 最低 1（8ms）
  if (d8 > 255) d8 = 255;             // 1バイトに収める（最大 2040ms）
  Serial.write(MARKER);
  Serial.write(partId);               // 楽器種別（=part_id）
  Serial.write(pitch);
  Serial.write((uint8_t)VELOCITY);
  Serial.write((uint8_t)d8);
}

void loop() {
  // ---- SYNC 等の受信処理 ----
  if (newData) {
    uint8_t buf[RX_BUF_SIZE];
    uint8_t len;
    noInterrupts();
    len = rxLen;
    for (uint8_t i = 0; i < len; i++) buf[i] = rxBuf[i];
    newData = false;
    interrupts();

    if (len > 0) {
      uint8_t cmd = buf[0];

      if (cmd == CMD_CONFIG && len >= 4) {
        entryOffset = buf[1];
        loopLength  = buf[2];
        partId      = buf[3];

      } else if (cmd == CMD_START) {
        started = true;

      } else if (cmd == CMD_SYNC && len >= 5) {
        unsigned long t = micros();  // 受信時刻をこの小節の開始時刻とする（スナップ方式）
        uint16_t globalBar = ((uint16_t)buf[1] << 8) | buf[2];
        uint16_t bpmX10    = ((uint16_t)buf[3] << 8) | buf[4];

        unsigned long barLengthUs = 60000000UL * 4UL * 10UL / bpmX10; // 1小節のμs
        unsigned long beatUs      = barLengthUs / 4UL;                 // 1拍のμs

        int16_t localBar = (int16_t)globalBar - (int16_t)entryOffset;
        if (started && localBar >= 0) {
          uint8_t bar = (uint8_t)(localBar % loopLength);
          loadBarNotes(bar, t, beatUs);
        }
      }
    }
  }

  // ---- 発火時刻が来た音符を送信 ----
  unsigned long now = micros();
  for (uint8_t i = 0; i < MAX_SCHED; i++) {
    if (sched[i].active && (long)(now - sched[i].fireUs) >= 0) {
      sendNoteEvent(sched[i].pitch, sched[i].durMs);
      sched[i].active = false;
    }
  }
}
