// drift_measure_3_ppm.ino
// drift_measure_3(BPM可変対応)に、drift_measure_ppm の静的 ppm 補正を
// 載せた版。sing_bpm 系 master 環境で「補正後の残差ドリフト」を測る用途。
// ─────────────────────────────────────────────────────────────
// 役割:
//   - I2Cで master からの SYNC(0x01) を受信するだけ
//   - 連続する SYNC の受信間隔と「直前BPM由来 × ppm補正済み」の公称
//     小節長との差(drift)を ASCIIのCSVで Serial に1行ずつ出力する
//   - 音は鳴らさない / バイナリパケットも送らない
//     → 音符ストリームに 0xAA が混入してパーサが壊れる問題が原理的に起きない
//
// 元ファイルとの関係:
//   drift_measure         : BPM固定・無補正
//   drift_measure_ppm     : BPM固定・ppm補正済み
//   drift_measure_3       : BPM可変・無補正(直前BPMで公称長を算出)
//   drift_measure_3_ppm   : BPM可変・ppm補正済み (★本ファイル)
//
//   drift_measure_3 の「直前のSYNCで受け取った BPM で公称小節長を計算する」
//   ロジックを踏襲しつつ、その公称長に drift_measure_ppm と同じ #if 分岐の
//   CLOCK_DRIFT_PPM を掛けて補正する。BPM が小節頭で変わっても偽ドリフト
//   が出ず、かつクロック固有差も差し引いた純粋な残差が見える。
//
// 使い方:
//   1. 下の MY_I2C_ADDRESS を測定するボードに合わせて変更して書き込む
//        flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13
//        ※ ppm補正値は #if で自動選択されるのでここだけ変えればよい
//   2. master は sing_bpm/master/master.ino をそのまま使う
//   3. シリアルモニタ(115200)で目視、または端末で CSV にリダイレクトして収集
//        例(mac): cat /dev/cu.usbmodemXXXX > flute_drift_singbpm_ppm.csv
//
// 出力フォーマット(CSV) ※ drift_measure_3 と同一:
//   addr,bar,bpm_x10,interval_us,drift_us
//     addr        : 自分のI2Cアドレス(16進)
//     bar         : master から来た global_bar(今回のSYNCに付いてきた値)
//     bpm_x10     : 今回のSYNCに付いてきた BPM×10
//                   ※BPM切替小節では「切替後」の値が乗る。drift_us は
//                     「前小節BPM × ppm補正」で算出した残差なので、
//                     行と紐付けて解析するときは bpm_x10 を1行シフトする。
//     interval_us : 直前のSYNCからの実測間隔(µs, 自分のmicros()基準)
//     drift_us    : interval_us - (直前BPMから算出した補正済み公称小節長)
//                   = 補正をかけた上で残るタイミング誤差(µs)
//                   +なら遅い側 / -なら速い側
//                   drift_us が +(公称小節長)付近 = SYNCを1回取りこぼしたサイン
// ─────────────────────────────────────────────────────────────

#include <Wire.h>

// ─── I2Cアドレス(★ボードごとに変更)───────────────────────────
#define MY_I2C_ADDRESS 0x13   // flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13

// ─── クロックドリフト補正(ppm) ───────────────────────────────
// drift_measure_ppm.ino と同一の値。各 *_slave.ino の calcBarUs() に
// 焼き込んだ値と必ず同じにすること。
#if   MY_I2C_ADDRESS == 0x10
  const int32_t CLOCK_DRIFT_PPM = 1419;   // flute    : +2838µs/小節
#elif MY_I2C_ADDRESS == 0x11
  const int32_t CLOCK_DRIFT_PPM = 618;    // clarinet : +1237µs/小節
#elif MY_I2C_ADDRESS == 0x12
  const int32_t CLOCK_DRIFT_PPM = -584;   // organ    : -1167µs/小節
#elif MY_I2C_ADDRESS == 0x13
  const int32_t CLOCK_DRIFT_PPM = 1872;   // drum     : +3743µs/小節
#else
  const int32_t CLOCK_DRIFT_PPM = 0;      // 未測定アドレス: 補正なし
#endif

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
      long interval = (long)(t - prevRxUs);                   // 符号付き差分でoverflow安全
      long nominal  = (long)calcBarUsCorrected(prevBpmX10);   // ★直前BPM × ppm補正
      long drift    = interval - nominal;                     // 補正後の残差誤差(µs)

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

// ─── ユーティリティ:BPM×10 → 補正済み1小節長(µs)──────────
// *_slave.ino / drift_measure_ppm.ino の calcBarUs() と同一の補正式。
// 「自分のmicros()基準で実時間1小節になる長さ」を返す。
unsigned long calcBarUsCorrected(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  unsigned long nominalUs = 60000000UL * 4UL * 10UL / bpm_x10;
  // ppm補正(AVRのint32オーバーフロー回避のため /1000 してから掛ける)
  long correction = ((long)nominalUs / 1000L) * CLOCK_DRIFT_PPM / 1000L;
  return (unsigned long)((long)nominalUs + correction);
}
