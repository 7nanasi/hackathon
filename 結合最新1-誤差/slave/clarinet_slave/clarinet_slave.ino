// slave_music.ino
// きらきら星を輪唱するArduinoスレーブ
// I2CでSYNC受信し、バイナリSerialでProcessingへ音符イベントを送信する
//
// クラリネットスレーブ: I2Cアドレス 0x11 (entry_delay=2)
// ─────────────────────────────────────────────────────────────

#include <Wire.h>
#include <avr/pgmspace.h>

// ─── I2Cアドレス ──────────────────────────────────────────────
#define MY_I2C_ADDRESS 0x11

// ─── I2Cコマンド定数 ──────────────────────────────────────────
const byte CMD_SYNC   = 0x01;
const byte CMD_START  = 0x10;
const byte CMD_CONFIG = 0x11;

// ─── バイナリプロトコル定数 ───────────────────────────────────
// パケット形式: [0xAA][楽器種別][pitch][velocity][duration_8ms]
const byte SERIAL_MARKER = 0xAA;
const byte INST_CLARINET = 0x02;

// ─── MIDIノート番号 ───────────────────────────────────────────
const byte N_C4 = 60, N_D4 = 62, N_E4 = 64;
const byte N_F4 = 65, N_G4 = 67, N_A4 = 69;

// ─── きらきら星 楽譜データ（PROGMEM）────────────────────────
// {pitch, barIndex(0-11), startBeat(0-3), durBeatsX10(10=1拍, 20=2拍)}
// 4バイト × 42エントリ = 168バイト → フラッシュメモリに配置
struct NoteEntry { byte pitch; byte barIndex; byte startBeat; byte durBeatsX10; };

const byte TOTAL_BARS = 12;
const byte SCORE_LEN  = 42;

const NoteEntry PROGMEM score[SCORE_LEN] = {
  // 小節0: C C G G
  {N_C4,  0, 0, 10}, {N_C4,  0, 1, 10}, {N_G4,  0, 2, 10}, {N_G4,  0, 3, 10},
  // 小節1: A A G(2拍)
  {N_A4,  1, 0, 10}, {N_A4,  1, 1, 10}, {N_G4,  1, 2, 20},
  // 小節2: F F E E
  {N_F4,  2, 0, 10}, {N_F4,  2, 1, 10}, {N_E4,  2, 2, 10}, {N_E4,  2, 3, 10},
  // 小節3: D D C(2拍)
  {N_D4,  3, 0, 10}, {N_D4,  3, 1, 10}, {N_C4,  3, 2, 20},
  // 小節4: G G F F
  {N_G4,  4, 0, 10}, {N_G4,  4, 1, 10}, {N_F4,  4, 2, 10}, {N_F4,  4, 3, 10},
  // 小節5: E E D(2拍)
  {N_E4,  5, 0, 10}, {N_E4,  5, 1, 10}, {N_D4,  5, 2, 20},
  // 小節6: G G F F
  {N_G4,  6, 0, 10}, {N_G4,  6, 1, 10}, {N_F4,  6, 2, 10}, {N_F4,  6, 3, 10},
  // 小節7: E E D(2拍)
  {N_E4,  7, 0, 10}, {N_E4,  7, 1, 10}, {N_D4,  7, 2, 20},
  // 小節8: C C G G（フレーズA繰り返し）
  {N_C4,  8, 0, 10}, {N_C4,  8, 1, 10}, {N_G4,  8, 2, 10}, {N_G4,  8, 3, 10},
  // 小節9: A A G(2拍)
  {N_A4,  9, 0, 10}, {N_A4,  9, 1, 10}, {N_G4,  9, 2, 20},
  // 小節10: F F E E
  {N_F4, 10, 0, 10}, {N_F4, 10, 1, 10}, {N_E4, 10, 2, 10}, {N_E4, 10, 3, 10},
  // 小節11: D D C(2拍)
  {N_D4, 11, 0, 10}, {N_D4, 11, 1, 10}, {N_C4, 11, 2, 20},
};

// ─── 演奏スケジュールバッファ（最大4音符/小節）───────────────
struct PendingNote {
  unsigned long fireUs;   // 発火予定時刻（micros()基準）
  byte pitch;
  byte durBeatsX10;
  bool fired;
};
PendingNote pendingNotes[4];
byte pendingCount = 0;

// ─── I2C受信バッファ（ISRと共有するのでvolatile）─────────────
volatile byte     rxCmd         = 0;
volatile bool     newData       = false;
volatile uint16_t rxBar         = 0;
volatile uint16_t rxBpmX10      = 0;
volatile byte     rxEntryOffset = 0;
volatile unsigned long rxTimeUs = 0;

// ─── 演奏状態 ─────────────────────────────────────────────────
uint16_t bpmX10           = 1200;       // デフォルト120BPM
unsigned long barStartUs  = 0;
unsigned long barLengthUs = 2000000;    // 2秒/小節 (120BPM)

byte entry_offset = 0;   // CONFIGで受け取る開始遅延小節数
bool started        = false;

// ─── setup() ──────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(MY_I2C_ADDRESS);
  Wire.onReceive(receiveEvent);
}

// ─── loop() ───────────────────────────────────────────────────
void loop() {
  if (newData) {
    noInterrupts();
    unsigned long t  = rxTimeUs;
    byte cmd         = rxCmd;
    uint16_t bar     = rxBar;
    uint16_t bpm     = rxBpmX10;
    byte eOffset     = rxEntryOffset;
    newData = false;
    interrupts();

    if (cmd == CMD_SYNC) {
      if (started && barStartUs != 0) {
        int16_t drift = (int16_t)constrain(
          (long)(t - (barStartUs + barLengthUs)), -32768, 32767);
        byte pkt[5] = {0xAA, 0x00, highByte(drift), lowByte(drift), (byte)bar};
        Serial.write(pkt, 5);
      }
      bpmX10       = bpm;
      barStartUs   = t;
      barLengthUs  = calcBarUs(bpm);

      if (started) {
        int16_t my_local_bar = (int16_t)bar - (int16_t)entry_offset;
        if (my_local_bar >= 0) {
          byte localBarMod = (byte)((uint16_t)my_local_bar % TOTAL_BARS);
          unsigned long beatUs = barLengthUs / 4;
          loadBarNotes(localBarMod, barStartUs, beatUs);
        }
      }
    }
    else if (cmd == CMD_START) {
      started      = true;
      pendingCount = 0;
    }
    else if (cmd == CMD_CONFIG) {
      entry_offset = eOffset;
    }
  }

  if (started) {
    firePendingNotes();
  }
}

// ─── I2C受信イベント（ISR内なのでSerial.print禁止）───────────
void receiveEvent(int howMany) {
  if (howMany < 1) return;
  byte cmd = Wire.read();

  if (cmd == CMD_SYNC && howMany == 5) {
    byte barH = Wire.read(), barL = Wire.read();
    byte bpmH = Wire.read(), bpmL = Wire.read();
    rxCmd    = cmd;
    rxBar    = ((uint16_t)barH << 8) | barL;
    rxBpmX10 = ((uint16_t)bpmH << 8) | bpmL;
    rxTimeUs = micros();
    newData  = true;
  }
  else if (cmd == CMD_START && howMany == 1) {
    rxCmd   = cmd;
    newData = true;
  }
  else if (cmd == CMD_CONFIG && howMany == 4) {
    rxCmd         = cmd;
    rxEntryOffset = Wire.read();
    Wire.read();
    Wire.read();
    newData       = true;
  }
  else {
    while (Wire.available()) Wire.read();
  }
}

// ─── 該当小節の音符をpendingNotesにロード ─────────────────────
void loadBarNotes(byte barMod, unsigned long barSt, unsigned long beatUs) {
  pendingCount = 0;
  for (byte i = 0; i < SCORE_LEN; i++) {
    if (pgm_read_byte(&score[i].barIndex) != barMod) continue;
    byte pitch = pgm_read_byte(&score[i].pitch);
    byte sb    = pgm_read_byte(&score[i].startBeat);
    byte dur   = pgm_read_byte(&score[i].durBeatsX10);
    pendingNotes[pendingCount].fireUs      = barSt + (unsigned long)sb * beatUs;
    pendingNotes[pendingCount].pitch       = pitch;
    pendingNotes[pendingCount].durBeatsX10 = dur;
    pendingNotes[pendingCount].fired       = false;
    pendingCount++;
    if (pendingCount >= 4) break;
  }
}

// ─── 発火タイムスタンプが来た音符をSerial送信 ─────────────────
void firePendingNotes() {
  unsigned long now = micros();
  for (byte i = 0; i < pendingCount; i++) {
    if (!pendingNotes[i].fired && (long)(now - pendingNotes[i].fireUs) >= 0) {
      sendNoteEvent(pendingNotes[i].pitch, pendingNotes[i].durBeatsX10);
      pendingNotes[i].fired = true;
    }
  }
}

// ─── 5バイトバイナリパケット送信 ──────────────────────────────
// 形式: [0xAA][楽器種別][pitch][velocity][duration_8ms]
// duration_8ms: 実際のms÷8（Processing側で×8してmsに戻す）
void sendNoteEvent(byte pitch, byte durBeatsX10) {
  unsigned long beatUs   = barLengthUs / 4;
  uint16_t durationMs    = (uint16_t)((unsigned long)durBeatsX10 * beatUs / 10000UL);
  byte dur8ms            = (byte)min((int)(durationMs / 8), 255);

  byte pkt[5] = {SERIAL_MARKER, INST_CLARINET, pitch, 80, dur8ms};
  Serial.write(pkt, 5);
}

// ─── ユーティリティ ───────────────────────────────────────────
unsigned long calcBarUs(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  return 60000000UL * 4UL * 10UL / bpm_x10;
}
