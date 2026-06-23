// drift_measure.ino
// 同期誤差(SYNCタイミング誤差)のベースライン測定用 計測専用ファーム
// ─────────────────────────────────────────────────────────────
// 役割:
//   - I2Cで master からの SYNC(0x01) を受信するだけ
//   - 連続する SYNC の受信間隔を測り、公称小節長との差(drift)を
//     ASCIIのCSVで Serial に1行ずつ出力する
//   - 音は鳴らさない / バイナリパケットも送らない
//     → 音符ストリームに 0xAA が混入してパーサが壊れる問題が原理的に起きない
//
// 使い方:
//   1. 下の MY_I2C_ADDRESS を測定するボードに合わせて変更して書き込む
//        flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13
//   2. master は本番の sync/arduino/master/master.ino をそのまま使う
//   3. シリアルモニタ(115200)で目視、または端末で CSV にリダイレクトして収集
//        例(mac): cat /dev/cu.usbmodemXXXX > flute_drift.csv
//
// 出力フォーマット(CSV):
//   addr,bar,interval_us,drift_us
//     addr        : 自分のI2Cアドレス(16進)
//     bar         : master から来た global_bar
//     interval_us : 直前のSYNCからの実測間隔(µs, 自分のmicros()基準)
//     drift_us    : interval_us - 公称小節長 = この小節のタイミング誤差(µs)
//                   +なら遅い側 / -なら速い側
//                   drift_us が +2,000,000 付近 = SYNCを1回取りこぼしたサイン
//
// 測れる範囲: 各スレーブの「小節ごとのタイミング誤差(ジッタ＋masterに対する
//             クロックドリフト)」。
// 測れない範囲: 楽器間の絶対スキュー(I2C逐次配信の一定ズレは前後差で相殺される)。
//             それは共通クロックが要るので別途(監視Arduino/オシロ)。
// ─────────────────────────────────────────────────────────────

#include <Wire.h>

// ─── I2Cアドレス(★ボードごとに変更)───────────────────────────
#define MY_I2C_ADDRESS 0x13   // flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13

// ─── I2Cコマンド定数(master/本番slaveと同一)─────────────────
const byte CMD_SYNC = 0x01;

// ─── I2C受信バッファ(ISRと共有するのでvolatile)─────────────
volatile bool          newData  = false;
volatile uint16_t      rxBar    = 0;
volatile uint16_t      rxBpmX10 = 0;
volatile unsigned long rxTimeUs = 0;

// ─── 測定状態 ─────────────────────────────────────────────────
unsigned long prevRxUs = 0;
bool          havePrev = false;

// ─── setup() ──────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(MY_I2C_ADDRESS);
  Wire.onReceive(receiveEvent);
  Serial.println(F("addr,bar,interval_us,drift_us"));  // CSVヘッダ
}

// ─── loop() ───────────────────────────────────────────────────
void loop() {
  if (newData) {
    noInterrupts();
    unsigned long t   = rxTimeUs;
    uint16_t      bar = rxBar;
    uint16_t      bpm = rxBpmX10;
    newData = false;
    interrupts();

    if (havePrev) {
      long interval = (long)(t - prevRxUs);   // 符号付き差分でoverflow安全
      long nominal  = (long)calcBarUs(bpm);
      long drift    = interval - nominal;     // 小節ごとのタイミング誤差(µs)

      Serial.print(MY_I2C_ADDRESS, HEX); Serial.print(',');
      Serial.print(bar);                 Serial.print(',');
      Serial.print(interval);            Serial.print(',');
      Serial.println(drift);
    }

    prevRxUs = t;
    havePrev = true;
  }
}

// ─── I2C受信イベント(ISR内なのでSerial.print禁止)───────────
void receiveEvent(int howMany) {
  if (howMany < 1) return;
  byte cmd = Wire.read();

  if (cmd == CMD_SYNC && howMany == 5) {
    byte barH = Wire.read(), barL = Wire.read();
    byte bpmH = Wire.read(), bpmL = Wire.read();
    rxBar    = ((uint16_t)barH << 8) | barL;
    rxBpmX10 = ((uint16_t)bpmH << 8) | bpmL;
    rxTimeUs = micros();   // ※ISR内micros()の癖で稀に~1msぶれる=ノイズ床
    newData  = true;
  }
  else {
    // SYNC以外(START/CONFIG等)は測定に不要なので読み捨て
    while (Wire.available()) Wire.read();
  }
}

// ─── ユーティリティ:BPM×10 → 1小節の長さ(µs)──────────────
unsigned long calcBarUs(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  return 60000000UL * 4UL * 10UL / bpm_x10;
}
