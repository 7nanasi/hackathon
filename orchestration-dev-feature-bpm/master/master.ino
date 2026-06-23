#include <Wire.h>

const byte NUM_SLAVES = 4;
const byte slaveADRs[NUM_SLAVES] = {0x10, 0x11, 0x12, 0x13};

const byte CMD_SYNC   = 0x01;
const byte CMD_START  = 0x10;
const byte CMD_CONFIG = 0x11;

uint16_t global_bar = 0;
uint16_t bpmX10 = 1200;

unsigned long nextSyncUs = 0;

// --- BPM 管理（master.ino に追記） ---
float current_bpm = 120.0f;
float target_bpm  = 120.0f;
const float BPM_MIN = 60.0f;
const float BPM_MAX = 180.0f;
const float BPM_DELTA_MAX = 10.0f; // 1小節あたり
const float BPM_DEADBAND  = 1.0f;  // ±この範囲の揺れは無視（センサ雑音除去）

// --- BPM ボリューム入力（10kΩB を A0 に接続）---
const byte PIN_BPM_POT          = A0;
const unsigned long POT_INTERVAL_MS = 20;   // サンプリング周期 (50Hz)
const byte POT_FILTER_N         = 8;        // 移動平均サンプル数
uint16_t potBuf[POT_FILTER_N]   = {0};
byte     potIdx                 = 0;
bool     potBufFilled           = false;
unsigned long lastPotMs         = 0;
   
float clamp_range(float v){
  if (v < BPM_MIN) return BPM_MIN;
  if (v > BPM_MAX) return BPM_MAX;
  return v;
}
   
uint16_t encode_bpm10(float bpm){
  bpm = clamp_range(bpm);
  return (uint16_t)(bpm * 10.0f + 0.5f);
}

void setup() {
  Wire.begin();
  Serial.begin(115200);

  delay(500);

  // 旋律スレーブ（2小節ずつずらして輪唱）
  for (byte i = 0; i < 3; i++) {
    sendConfig(slaveADRs[i], i * 2, 16, i + 1);
  }
  // ドラムスレーブ（遅延なしでメロディと同時スタート）
  sendConfig(slaveADRs[3], 0, 12, 4);

  delay(100);

  sendStartToAll();

  nextSyncUs = micros() + 500000UL;
}

void loop() {
  // ボリューム読み取り → target_bpm を更新
  unsigned long nowMs = millis();
  if (nowMs - lastPotMs >= POT_INTERVAL_MS) {
    lastPotMs = nowMs;
    potBuf[potIdx++] = analogRead(PIN_BPM_POT);
    if (potIdx >= POT_FILTER_N) { potIdx = 0; potBufFilled = true; }
    onSensorRead(readPotBpm());
  }

  unsigned long now = micros();

  if ((long)(now - nextSyncUs) >= 0) {
    // 小節頭で current_bpm を target_bpm に近づけて bpmX10 に反映してから送信
    onMeasureStart_bpm_master();
    sendSyncToAll(global_bar, bpmX10);

    global_bar++;
    nextSyncUs += calcBarUs(bpmX10);
  }
}

// ボリューム平均値を BPM_MIN〜BPM_MAX に線形マップして返す
float readPotBpm() {
  byte n = potBufFilled ? POT_FILTER_N : potIdx;
  if (n == 0) return current_bpm; // 初回未読時
  uint32_t sum = 0;
  for (byte i = 0; i < n; i++) sum += potBuf[i];
  float avg = (float)sum / (float)n;        // 0〜1023
  return BPM_MIN + (avg / 1023.0f) * (BPM_MAX - BPM_MIN);
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

// センサ読み取り時に呼ぶ（デッドバンドで雑音を除去してから target に保存）
void onSensorRead(float sensor_bpm){
  sensor_bpm = clamp_range(sensor_bpm);
  if (fabs(sensor_bpm - target_bpm) < BPM_DEADBAND) return;
  target_bpm = sensor_bpm;
}

// 小節頭で呼ぶ：current_bpm を target_bpm に近づけ、bpmX10 に反映する
// 送信は呼び出し直後の sendSyncToAll(global_bar, bpmX10) が行う
void onMeasureStart_bpm_master(){
  float delta = target_bpm - current_bpm;
  if (delta > BPM_DELTA_MAX) delta = BPM_DELTA_MAX;
  else if (delta < -BPM_DELTA_MAX) delta = -BPM_DELTA_MAX;
  current_bpm += delta;
  current_bpm  = clamp_range(current_bpm);
  bpmX10       = encode_bpm10(current_bpm);
}
