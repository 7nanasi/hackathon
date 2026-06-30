// drift_measure_3.ino
// sing_bpm 系(ポテンショメータでBPMが小節頭に変動する master)に対応した
// ドリフト計測用ファーム。drift_measure.ino(無補正版)がベース。
// ─────────────────────────────────────────────────────────────
// 役割:
//   - I2Cで master からの SYNC(0x01) を受信するだけ
//   - 連続する SYNC の受信間隔を測り、公称小節長との差(drift)を
//     ASCIIのCSVで Serial に1行ずつ出力する
//   - 音は鳴らさない / バイナリパケットも送らない
//     → 音符ストリームに 0xAA が混入してパーサが壊れる問題が原理的に起きない
//
// drift_measure.ino との違い(BPM可変対応):
//   sing_bpm/master/master.ino では小節頭で BPM が変わり、
//     nextSyncUs += calcBarUs(bpmX10);
//   と「いま送ったBPM」で次の小節長を刻む。つまり SYNC[n] と SYNC[n+1] の
//   間隔は、SYNC[n] の時点で送られた BPM が決める。
//   よって drift を出すには、今回のSYNC受信時の bpm ではなく
//   「**直前のSYNCで受け取った bpm**」で公称小節長を計算しないと、
//   BPM変更直後の小節で数千〜数万µsの見かけドリフトが出てしまう。
//   本ファイルはここだけを直してある。受信プロトコル・CSVスキーマは同一。
//
// 使い方:
//   1. 下の MY_I2C_ADDRESS を測定するボードに合わせて変更して書き込む
//        flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13
//   2. master は sing_bpm/master/master.ino をそのまま使う
//   3. シリアルモニタ(115200)で目視、または端末で CSV にリダイレクトして収集
//        例(mac): cat /dev/cu.usbmodemXXXX > flute_drift_singbpm.csv
//
// 出力フォーマット(CSV):
//   addr,bar,bpm_x10,interval_us,drift_us
//     addr        : 自分のI2Cアドレス(16進)
//     bar         : master から来た global_bar(=今回のSYNCに付いてきた値)
//     bpm_x10     : 今回のSYNCに付いてきた BPM×10。
//                   ※BPM切替小節では「切替後」の値が乗る。drift_us は
//                     「前小節BPM」で算出した残差なので、行と紐付けて
//                     解析するときは bpm_x10 を1行シフトすると揃う。
//     interval_us : 直前のSYNCからの実測間隔(µs, 自分のmicros()基準)
//     drift_us    : interval_us - (直前BPMから算出した公称小節長)
//                   +なら遅い側 / -なら速い側
//                   drift_us が +(公称小節長)付近 = SYNCを1回取りこぼしたサイン
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
unsigned long prevRxUs    = 0;
uint16_t      prevBpmX10  = 0;     // 直前SYNCのBPM(公称小節長算出に使う)
bool          havePrev    = false;

// ─── setup() ──────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(MY_I2C_ADDRESS);
  Wire.onReceive(receiveEvent);
  Serial.println(F("addr,bar,bpm_x10,interval_us,drift_us"));  // CSVヘッダ
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
      long interval = (long)(t - prevRxUs);          // 符号付き差分でoverflow安全
      long nominal  = (long)calcBarUs(prevBpmX10);   // ★直前BPMで算出
      long drift    = interval - nominal;            // 小節ごとのタイミング誤差(µs)

      Serial.print(MY_I2C_ADDRESS, HEX); Serial.print(',');
      Serial.print(bar);                 Serial.print(',');
      Serial.print(bpm);                 Serial.print(',');
      Serial.print(interval);            Serial.print(',');
      Serial.println(drift);
    }

    prevRxUs   = t;
    prevBpmX10 = bpm;
    havePrev   = true;
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
