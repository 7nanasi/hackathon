// drift_measure_bpm_ppm_pll.ino
// drift_measure_3_ppm(BPM可変・ppm補正済み)に、設計書のPLL補正(P-only, Kp一次ローパス
// ＋ 50ms強制スナップ)を追加した版。ppm補正の後段にPLLを乗せる構成。
// ─────────────────────────────────────────────────────────────
// 役割:
//   - I2Cで master からの SYNC(0x01) を受信するだけ
//   - 連続するSYNCの受信間隔と「ppm補正済み公称長 + PLL予測補正」との差(=ppm+PLL残差)を
//     ASCIIのCSVで Serial に1行ずつ出力する
//   - 音は鳴らさない / バイナリパケットも送らない
//
// 元ファイルとの関係:
//   drift_measure           : BPM固定・無補正
//   drift_measure_ppm       : BPM固定・ppm補正済み
//   drift_measure_3         : BPM可変・無補正(直前BPMで公称長を算出)
//   drift_measure_3_ppm     : BPM可変・ppm補正済み
//   drift_measure_bpm_pll     : BPM可変・PLL補正のみ(段階1)
//   drift_measure_bpm_ppm_pll : BPM可変・ppm + PLL併用 (★本ファイル / 段階2)
//
// ppm + PLL の順序:
//   ppm補正で定常クロックドリフトを先に消し、その上で残ったジッタとppm補正の取りこぼし
//   だけをPLLで追従する。ppm補正済み公称長を「PLLにとっての理論値」として扱うため、
//   nominal = calcBarUsCorrected(prevBpmX10) を採用する。
//
// PLL補正の出典:
//   システム設計抜粋.md §3.4.5 / 同期方針計画書.html §4.3
//     誤差   = 実受信時刻 - 予測時刻
//     次小節長 = 理論値   - 誤差 × Kp
//     |誤差| > 50ms の場合は補正ではなく強制スナップで位相を合わせ直す
//
// 段階1からの主な差分:
//   - 公称長を ppm補正済み(calcBarUsCorrected)に置き換え
//   - Kp を 0.3 → 0.15 に半減(ppm が定常分を既に消しているので保守的に)
//   - PLL補正値の絶対値上限を ±100ms → ±50ms に絞る(同上)
//   - Ki は段階1と同じく 0(積分項なし。実機で発振確認できない以上は入れない)
//
// drift_us 列の意味 (★ 解析時の注意):
//   drift_measure_3         → drift_us = interval - 公称小節長              (生ドリフト)
//   drift_measure_3_ppm     → drift_us = interval - ppm補正済み公称長        (ppm後残差)
//   drift_measure_bpm_pll     → drift_us = interval - PLL予測間隔              (PLL後残差)
//   drift_measure_bpm_ppm_pll → drift_us = interval - (ppm公称長 + PLL補正)    (★ppm+PLL残差)
//   CSVのヘッダ・列数は同じなので集計スクリプトは無改修で動く。
//
// 使い方:
//   1. 下の MY_I2C_ADDRESS を測定するボードに合わせて変更して書き込む
//        flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13
//        ※ ppm補正値は #if で自動選択されるのでここだけ変えればよい
//   2. master は sing_bpm/master/master.ino をそのまま使う
//   3. シリアルモニタ(115200)で目視、または端末でCSVにリダイレクトして収集
//        例(mac): cat /dev/cu.usbmodemXXXX > flute_drift_singbpm_ppm_pll.csv
//
// 出力フォーマット(CSV) ※ drift_measure_3 と同一:
//   addr,bar,bpm_x10,interval_us,drift_us
// ─────────────────────────────────────────────────────────────

#include <Wire.h>

// ─── I2Cアドレス(★ボードごとに変更)───────────────────────────
#define MY_I2C_ADDRESS 0x13   // flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13

// ─── クロックドリフト補正(ppm) ───────────────────────────────
// drift_measure_ppm.ino / drift_measure_3_ppm.ino と同一の値。
// 各 *_slave.ino の calcBarUsCorrected() に焼き込んだ値と必ず同じにすること。
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

// ─── PLL係数(★後から数値だけ書き換えて再ビルド可)─────────────
// 設計書(システム設計抜粋.md §3.4.5 / 同期方針計画書.html §4.3)に
// 1:1 対応する最小構成。段階1 と完全同一値で運用する。
//
// ppmとPLLの整理.txt §10-3 で確認した通り、P 単体 PLL は定常オフセットを
// 消す機能を原理的に持たないので、ppm 補正と取り合わない(役割が綺麗に分担される)。
// したがって ppm 併用でも段階1 と同じ Kp=0.3 で問題ない。
// ppmとPLLの整理.txt §9-4 の理論計算でも Kp ≤ 0.818 が MOP1(30ms) 合格範囲。
//
// PLL_KP_PERMIL          : 比例ゲイン Kp を 1000 倍した整数。300 = 0.3。
//                          浮動小数を避けるため permil(千分率) 表現。
//                          設計書推奨値。
// PLL_SNAP_THRESHOLD_US  : この値を超える残差が出たら強制スナップ(補正リセット)。
//                          設計書通り 50ms。
const int32_t  PLL_KP_PERMIL         = 300;
const long     PLL_SNAP_THRESHOLD_US = 50000L;

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

// ─── PLL状態 ─────────────────────────────────────────────────
// pllCorrectionUs:
//   直前ステップで算出した「次回SYNC予測間隔」への補正量(符号付き)。
//   設計式 「次小節長 = 公称長 - 誤差 × Kp」 の右辺の "-誤差 × Kp" 部分。
//   havePrev == true のときは前回の残差を見て更新する。
//   havePrev == false (= 起動直後・初回SYNC受信時) は 0 のまま使われる。
long          pllCorrectionUs = 0;

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
      long interval  = (long)(t - prevRxUs);                   // 符号付き差分でoverflow安全
      long nominal   = (long)calcBarUsCorrected(prevBpmX10);   // ★直前BPM × ppm補正
      long predicted = nominal + pllCorrectionUs;              // ppm補正済み + PLL予測補正
      long drift     = interval - predicted;                   // ppm + PLL 後の残差(µs)

      Serial.print(MY_I2C_ADDRESS, HEX); Serial.print(',');
      Serial.print(bar);                 Serial.print(',');
      Serial.print(bpm);                 Serial.print(',');
      Serial.print(interval);            Serial.print(',');
      Serial.println(drift);

      // ── PLL更新(次小節への補正を算出) ───────────────────────
      // |drift| がSNAP閾値を超えたら補正リセット(強制スナップ)。
      // drift > 0 = SYNCが予測より遅く来た = 実際の小節が予測より長かった
      // → pllCorrectionUs を増やして次回の予測を長くする(負帰還)。
      long absDrift = drift < 0 ? -drift : drift;
      if (absDrift > PLL_SNAP_THRESHOLD_US) {
        pllCorrectionUs = 0;
      } else {
        pllCorrectionUs = (drift * PLL_KP_PERMIL) / 1000L;
      }
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
// *_slave.ino / drift_measure_ppm.ino / drift_measure_3_ppm.ino の
// calcBarUsCorrected() と同一の補正式。
unsigned long calcBarUsCorrected(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  unsigned long nominalUs = 60000000UL * 4UL * 10UL / bpm_x10;
  // ppm補正(AVRのint32オーバーフロー回避のため /1000 してから掛ける)
  long correction = ((long)nominalUs / 1000L) * CLOCK_DRIFT_PPM / 1000L;
  return (unsigned long)((long)nominalUs + correction);
}
