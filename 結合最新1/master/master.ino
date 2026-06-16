#include <Wire.h>

const byte NUM_SLAVES = 4;
const byte slaveADRs[NUM_SLAVES] = {0x10, 0x11, 0x12, 0x13};

const byte CMD_SYNC   = 0x01;
const byte CMD_START  = 0x10;
const byte CMD_CONFIG = 0x11;

uint16_t global_bar = 0;
uint16_t bpmX10 = 800;

unsigned long nextSyncUs = 0;

// スタート/ストップボタン（GND と D2 に接続、内蔵プルアップで押下時 LOW）
const byte BUTTON_PIN = 2;
bool playing       = false;  // 演奏中フラグ
bool lastButtonLow = false;  // 前回のボタン押下状態（エッジ検出用）

void setup() {
  Wire.begin();
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  delay(500);

  // 旋律スレーブ（2小節ずつずらして輪唱）
  for (byte i = 0; i < 3; i++) {
    sendConfig(slaveADRs[i], i * 2, 16, i + 1);
  }
  // ドラムスレーブ（遅延なしでメロディと同時スタート）
  sendConfig(slaveADRs[3], 0, 12, 4);

  delay(100);
}

void loop() {
  // ボタンの押下エッジ（離した状態→押した瞬間）でトグル
  bool buttonLow = (digitalRead(BUTTON_PIN) == LOW);
  if (buttonLow && !lastButtonLow) {
    delay(20);  // 簡易デバウンス
    if (digitalRead(BUTTON_PIN) == LOW) {
      togglePlay();
    }
  }
  lastButtonLow = buttonLow;

  // 停止中は同期を送らない（スレーブへ sync が届かなくなり、順次鳴り止む）
  if (!playing) return;

  unsigned long now = micros();

  if ((long)(now - nextSyncUs) >= 0) {
    sendSyncToAll(global_bar, bpmX10);

    global_bar++;
    nextSyncUs += calcBarUs(bpmX10);
  }
}

// ボタンで開始/停止を切り替える
void togglePlay() {
  if (!playing) {
    global_bar = 0;                       // 曲の頭から
    sendStartToAll();
    nextSyncUs = micros() + 500000UL;
    playing = true;
  } else {
    playing = false;                      // sync 送信を止める
  }
}

unsigned long calcBarUs(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  return 60000000UL * 4UL * 10UL / bpm_x10;
}

void sendStartToAll() {
  for (byte i = 0; i < NUM_SLAVES; i++) {
    sendStart(slaveADRs[i]);
  }
}

void sendSyncToAll(uint16_t song_bar, uint16_t bpm_x10) {
  for (byte i = 0; i < NUM_SLAVES; i++) {
    sendSync(slaveADRs[i], song_bar, bpm_x10);
  }
}

void sendSync(byte targetADR, uint16_t song_bar, uint16_t bpm_x10) {
  Wire.beginTransmission(targetADR);
  Wire.write(CMD_SYNC);
  Wire.write(highByte(song_bar));
  Wire.write(lowByte(song_bar));
  Wire.write(highByte(bpm_x10));
  Wire.write(lowByte(bpm_x10));
  Wire.endTransmission();
}

void sendStart(byte targetADR) {
  Wire.beginTransmission(targetADR);
  Wire.write(CMD_START);
  Wire.endTransmission();
}

void sendConfig(byte targetADR, byte entry_offset, byte loop_length, byte part_id) {
  Wire.beginTransmission(targetADR);
  Wire.write(CMD_CONFIG);
  Wire.write(entry_offset);
  Wire.write(loop_length);
  Wire.write(part_id);
  Wire.endTransmission();
}
