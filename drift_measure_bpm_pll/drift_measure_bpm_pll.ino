// drift_measure_bpm_pll.ino
// drift_measure_3(BPM 可変・無補正)に、設計書の PLL 補正(Kp 一次ローパス
// ＋ 50ms 強制スナップ)だけを載せた版。ppm 補正は入れていない。
// ─────────────────────────────────────────────────────────────
// 役割:
//   - I2C で master からの SYNC(0x01) を受信するだけ
//   - 連続する SYNC の受信間隔と「PLL が予測した間隔」との残差を
//     ASCII の CSV で Serial に 1 行ずつ出力する
//   - 音は鳴らさない / バイナリパケットも送らない
//
// 元ファイルとの関係:
//   drift_measure         : BPM 固定 ・ 無補正
//   drift_measure_ppm     : BPM 固定 ・ ppm 補正済み
//   drift_measure_3       : BPM 可変 ・ 無補正
//   drift_measure_3_ppm   : BPM 可変 ・ ppm 補正済み
//   drift_measure_bpm_pll   : BPM 可変 ・ PLL 補正のみ (★本ファイル)
//   drift_measure_bpm_ppm_pll: BPM 可変 ・ ppm + PLL 併用 (段階2で別フォルダ作成予定)
//
// PLL 補正の出典:
//   システム設計抜粋.md §3.4.5 / 同期方針計画書.html §4.3
//     誤差   = 実受信時刻 - 予測時刻
//     次小節長 = 理論値   - 誤差 × Kp     (Kp = 0.3 が設計推奨)
//     |誤差| > 50ms の場合は補正ではなく強制スナップで位相を合わせ直す
//
//   本ファイルは「音符は鳴らさず計測のみ」なので、
//   - 「次小節長」は「次 SYNC の予測間隔」として使う
//   - drift_us 列には「PLL が予測した間隔と実測の差(=残差)」を吐く
//   ことで、PLL がどの程度追従できているかを残差で観測できるようにしている。
//
// drift_us 列の意味の変化 (★ drift_measure_3 との差):
//   drift_measure_3      → drift_us = interval - 公称小節長        (生ドリフト)
//   drift_measure_bpm_pll  → drift_us = interval - PLL 予測間隔        (補正後残差)
//   CSV のヘッダ・列数は同じなので集計スクリプトは無改修で動く。
//   ただし「drift_us が何を意味するか」は本ファイル特有なので解析時に注意。
//
// 使い方 (drift_measure_3 と同じ):
//   1. 下の MY_I2C_ADDRESS を測定するボードに合わせて変更して書き込む
//        flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13
//   2. master は sing_bpm/master/master.ino をそのまま使う
//   3. シリアルモニタ(115200)で目視、または端末で CSV にリダイレクトして収集
//        例(mac): cat /dev/cu.usbmodemXXXX > flute_drift_singbpm_pll.csv
//
// 出力フォーマット(CSV) ※ drift_measure_3 と同一:
//   addr,bar,bpm_x10,interval_us,drift_us
// ─────────────────────────────────────────────────────────────

#include <Wire.h>

// ─── I2Cアドレス(★ボードごとに変更)───────────────────────────
#define MY_I2C_ADDRESS 0x13   // flute=0x10 / clarinet=0x11 / organ=0x12 / drum=0x13

// ─── PLL 係数(★後から数値だけ書き換えて再ビルド可)─────────────
// 設計書(システム設計抜粋.md §3.4.5 / 同期方針計画書.html §4.3)に
// 1:1 対応する最小構成。
//
// PLL_KP_PERMIL          : 比例ゲイン Kp を 1000 倍した整数。300 = 0.3。
//                          浮動小数を避けるため permil(千分率) 表現。
//                          設計書推奨値。
// PLL_SNAP_THRESHOLD_US  : この値を超える残差が出たら補正ではなく強制
//                          スナップ(補正状態を 0 にリセット)。設計書の 50ms。
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

// ─── PLL 状態 ─────────────────────────────────────────────────
// pllCorrectionUs:
//   直前ステップで算出した「次回 SYNC 予測間隔」への補正量(符号付き)。
//   設計式 「次小節長 = 公称長 - 誤差 × Kp」 の右辺の "-誤差 × Kp" 部分。
//   havePrev == true のときは前回の残差を見て更新する。
//   havePrev == false (= 起動直後・初回 SYNC 受信時) は 0 のまま使われる。
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
      long interval  = (long)(t - prevRxUs);             // 符号付き差分で overflow 安全
      long nominal   = (long)calcBarUs(prevBpmX10);      // ★直前BPMで算出
      long predicted = nominal + pllCorrectionUs;        // PLL 予測間隔
      long drift     = interval - predicted;             // PLL 補正後の残差(µs)

      Serial.print(MY_I2C_ADDRESS, HEX); Serial.print(',');
      Serial.print(bar);                 Serial.print(',');
      Serial.print(bpm);                 Serial.print(',');
      Serial.print(interval);            Serial.print(',');
      Serial.println(drift);

      // ── PLL 更新(次小節への補正を算出) ───────────────────────
      // |drift| が SNAP 閾値を超えたら補正リセット(強制スナップ)。
      // それ以外は 設計式 「次小節長 = 公称長 - 誤差 × Kp」 に従って
      // pllCorrectionUs = -drift × Kp を次回予測に持ち越す。
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

// ─── ユーティリティ:BPM×10 → 1小節の長さ(µs)──────────────
unsigned long calcBarUs(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  return 60000000UL * 4UL * 10UL / bpm_x10;
}
