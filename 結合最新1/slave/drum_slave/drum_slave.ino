// drum_slave.ino
// きらきら星伴奏ドラムパターン Arduinoスレーブ
// I2CでSYNC受信し、バイナリSerialでProcessingへドラムイベントを送信する
//
// ドラムスレーブ: I2Cアドレス 0x13 (entry_delay=0)
// pitchフィールドをドラム種別として使う（KICK=36, HIHAT=42）
// ─────────────────────────────────────────────────────────────

#include <Wire.h>
#include <avr/pgmspace.h>

// ─── I2Cアドレス ──────────────────────────────────────────────
#define MY_I2C_ADDRESS 0x13

// ─── I2Cコマンド定数 ──────────────────────────────────────────
const byte CMD_SYNC   = 0x01;
const byte CMD_START  = 0x10;
const byte CMD_CONFIG = 0x11;

// ─── バイナリプロトコル定数 ───────────────────────────────────
// パケット形式: [0xAA][楽器種別][pitch=ドラム種別][velocity][duration_8ms]
const byte SERIAL_MARKER = 0xAA;
const byte INST_DRUM     = 0x04;

// ─── ドラム種別（MIDIノート番号準拠）─────────────────────────
const byte DRUM_KICK  = 36;   // C2: キックドラム
const byte DRUM_HIHAT = 42;   // F#2: クローズドハイハット

// ─── ドラムパターン楽譜データ（PROGMEM）────────────────────
// {pitch=ドラム種別, barIndex(0-11), startBeat(0-3), durBeatsX10}
// durBeatsX10=5 → 半拍分のトリガー時間（Processingの自前エンベロープが実音量を制御）
//
// 4拍子パターン: キック=拍0・2、ハイハット=全拍（1小節6イベント）
// 6イベント × 12小節 = 72エントリ → PROGMEMに288バイト

struct NoteEntry { byte pitch; byte barIndex; byte startBeat; byte durBeatsX10; };

const byte TOTAL_BARS = 12;
const byte SCORE_LEN  = 72;

const NoteEntry PROGMEM score[SCORE_LEN] = {
  // 小節0
  {DRUM_KICK,  0, 0, 5}, {DRUM_HIHAT, 0, 0, 5},
  {DRUM_HIHAT, 0, 1, 5},
  {DRUM_KICK,  0, 2, 5}, {DRUM_HIHAT, 0, 2, 5},
  {DRUM_HIHAT, 0, 3, 5},
  // 小節1
  {DRUM_KICK,  1, 0, 5}, {DRUM_HIHAT, 1, 0, 5},
  {DRUM_HIHAT, 1, 1, 5},
  {DRUM_KICK,  1, 2, 5}, {DRUM_HIHAT, 1, 2, 5},
  {DRUM_HIHAT, 1, 3, 5},
  // 小節2
  {DRUM_KICK,  2, 0, 5}, {DRUM_HIHAT, 2, 0, 5},
  {DRUM_HIHAT, 2, 1, 5},
  {DRUM_KICK,  2, 2, 5}, {DRUM_HIHAT, 2, 2, 5},
  {DRUM_HIHAT, 2, 3, 5},
  // 小節3
  {DRUM_KICK,  3, 0, 5}, {DRUM_HIHAT, 3, 0, 5},
  {DRUM_HIHAT, 3, 1, 5},
  {DRUM_KICK,  3, 2, 5}, {DRUM_HIHAT, 3, 2, 5},
  {DRUM_HIHAT, 3, 3, 5},
  // 小節4
  {DRUM_KICK,  4, 0, 5}, {DRUM_HIHAT, 4, 0, 5},
  {DRUM_HIHAT, 4, 1, 5},
  {DRUM_KICK,  4, 2, 5}, {DRUM_HIHAT, 4, 2, 5},
  {DRUM_HIHAT, 4, 3, 5},
  // 小節5
  {DRUM_KICK,  5, 0, 5}, {DRUM_HIHAT, 5, 0, 5},
  {DRUM_HIHAT, 5, 1, 5},
  {DRUM_KICK,  5, 2, 5}, {DRUM_HIHAT, 5, 2, 5},
  {DRUM_HIHAT, 5, 3, 5},
  // 小節6
  {DRUM_KICK,  6, 0, 5}, {DRUM_HIHAT, 6, 0, 5},
  {DRUM_HIHAT, 6, 1, 5},
  {DRUM_KICK,  6, 2, 5}, {DRUM_HIHAT, 6, 2, 5},
  {DRUM_HIHAT, 6, 3, 5},
  // 小節7
  {DRUM_KICK,  7, 0, 5}, {DRUM_HIHAT, 7, 0, 5},
  {DRUM_HIHAT, 7, 1, 5},
  {DRUM_KICK,  7, 2, 5}, {DRUM_HIHAT, 7, 2, 5},
  {DRUM_HIHAT, 7, 3, 5},
  // 小節8
  {DRUM_KICK,  8, 0, 5}, {DRUM_HIHAT, 8, 0, 5},
  {DRUM_HIHAT, 8, 1, 5},
  {DRUM_KICK,  8, 2, 5}, {DRUM_HIHAT, 8, 2, 5},
  {DRUM_HIHAT, 8, 3, 5},
  // 小節9
  {DRUM_KICK,  9, 0, 5}, {DRUM_HIHAT, 9, 0, 5},
  {DRUM_HIHAT, 9, 1, 5},
  {DRUM_KICK,  9, 2, 5}, {DRUM_HIHAT, 9, 2, 5},
  {DRUM_HIHAT, 9, 3, 5},
  // 小節10
  {DRUM_KICK,  10, 0, 5}, {DRUM_HIHAT, 10, 0, 5},
  {DRUM_HIHAT, 10, 1, 5},
  {DRUM_KICK,  10, 2, 5}, {DRUM_HIHAT, 10, 2, 5},
  {DRUM_HIHAT, 10, 3, 5},
  // 小節11
  {DRUM_KICK,  11, 0, 5}, {DRUM_HIHAT, 11, 0, 5},
  {DRUM_HIHAT, 11, 1, 5},
  {DRUM_KICK,  11, 2, 5}, {DRUM_HIHAT, 11, 2, 5},
  {DRUM_HIHAT, 11, 3, 5},
};

// ─── 演奏スケジュールバッファ（最大6イベント/小節）──────────
// 4拍子パターンで1小節に最大6イベント（拍0・2にKICK+HIHAT同時）
struct PendingNote {
  unsigned long fireUs;
  byte pitch;       // ここはドラム種別（DRUM_KICK or DRUM_HIHAT）
  byte durBeatsX10;
  bool fired;
};
PendingNote pendingNotes[6];
byte pendingCount = 0;

// ─── I2C受信バッファ（ISRと共有するのでvolatile）─────────────
volatile byte     rxCmd         = 0;
volatile bool     newData       = false;
volatile uint16_t rxBar         = 0;
volatile uint16_t rxBpmX10      = 0;
volatile byte     rxEntryOffset = 0;
volatile unsigned long rxTimeUs = 0;

// ─── 演奏状態 ─────────────────────────────────────────────────
uint16_t bpmX10           = 1200;
unsigned long barStartUs  = 0;
unsigned long barLengthUs = 2000000;

byte entry_offset = 0;
bool started      = false;

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
      bpmX10      = bpm;
      barStartUs  = t;
      barLengthUs = calcBarUs(bpm);

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

// ─── 該当小節のドラムイベントをpendingNotesにロード ──────────
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
    if (pendingCount >= 6) break;
  }
}

// ─── 発火タイムスタンプが来たイベントをSerial送信 ─────────────
void firePendingNotes() {
  unsigned long now = micros();
  for (byte i = 0; i < pendingCount; i++) {
    if (!pendingNotes[i].fired && (long)(now - pendingNotes[i].fireUs) >= 0) {
      sendDrumEvent(pendingNotes[i].pitch, pendingNotes[i].durBeatsX10);
      pendingNotes[i].fired = true;
    }
  }
}

// ─── 5バイトバイナリパケット送信 ──────────────────────────────
// 形式: [0xAA][0x04=INST_DRUM][pitch=ドラム種別][velocity][duration_8ms]
// Processing側はpitchでKICK(36)/HIHAT(42)を判別して鳴らす
void sendDrumEvent(byte drumType, byte durBeatsX10) {
  unsigned long beatUs = barLengthUs / 4;
  uint16_t durationMs  = (uint16_t)((unsigned long)durBeatsX10 * beatUs / 10000UL);
  byte dur8ms          = (byte)min((int)(durationMs / 8), 255);

  byte pkt[5] = {SERIAL_MARKER, INST_DRUM, drumType, 80, dur8ms};
  Serial.write(pkt, 5);
}

// ─── ユーティリティ ───────────────────────────────────────────
unsigned long calcBarUs(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  return 60000000UL * 4UL * 10UL / bpm_x10;
}
