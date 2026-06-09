// 楽器 Arduino — スレーブ（1台確認用）
// I²C アドレス: 0x10
//
// CONFIG 受信 → パート設定を保存
// START  受信 → SYNC 待機モードへ
// SYNC   受信 → my_local_bar を計算し、演奏タイミングを判定
//               （現段階ではシリアル出力で動作確認）

// I²C 通信ライブラリ
#include <Wire.h>

// このスレーブの I²C アドレス（マスターはこの番号宛てに送ってくる）
#define MY_ADDR 0x10

// ---- コマンド定数 ----
// 受信した最初の1バイトを見て、どのコマンドか判断する
#define CMD_SYNC   0x01
#define CMD_START  0x10
#define CMD_CONFIG 0x11

// ---- 受信バッファ ----
// I²C の受信は割り込み（ISR）で処理される。
// ISR とメインループが同じ変数を同時に触ると値が壊れることがあるため、
// volatile をつけて「最適化しないでそのまま読め」とコンパイラに伝える。
#define RX_BUF_SIZE 8
volatile uint8_t rxBuf[RX_BUF_SIZE];  // 受信データの一時置き場
volatile uint8_t rxLen   = 0;          // 受信したバイト数
volatile bool    newData = false;       // 新しいデータが届いたことを知らせるフラグ

// ---- パート設定（CONFIG で上書きされる）----
uint8_t entry_offset = 0;   // 何小節目から演奏を始めるか
uint8_t loop_length  = 16;  // 1ループの小節数
uint8_t part_id      = 0;   // パート番号

// ---- 演奏状態 ----
bool started = false;  // START を受け取ったか
bool playing = false;  // 演奏を開始済みか

// I²C 受信割り込み関数（ISR）
// マスターからデータが届くと自動的に呼ばれる。
// ここでは受信バッファに詰めてフラグを立てるだけにする。
// Serial.print などの時間のかかる処理を ISR 内で行うと
// 他の処理がブロックされて誤動作するため禁止。
void receiveEvent(int numBytes) {
  rxLen = 0;
  // 受信バッファからデータを読み出して rxBuf に保存
  while (Wire.available() && rxLen < RX_BUF_SIZE) {
    rxBuf[rxLen++] = Wire.read();
  }
  // バッファが溢れた分は読み捨てる
  while (Wire.available()) { Wire.read(); }
  // メインループに「データが来たよ」と知らせる
  newData = true;
}

void setup() {
  Wire.begin(MY_ADDR);          // このアドレスのスレーブとして I²C 開始
  Wire.onReceive(receiveEvent); // 受信時に呼ぶ関数を登録
  Serial.begin(115200);
  delay(500);
  Serial.println("=== Slave start (addr=0x10) ===");
}

void loop() {
  // 新しいデータが届いていなければ何もしない
  if (!newData) return;

  // ISR が書き込み中にメインループが読み出すと値が壊れる可能性がある。
  // noInterrupts() で割り込みを一時停止してから安全にコピーする。
  uint8_t buf[RX_BUF_SIZE];
  uint8_t len;
  noInterrupts();                                    // 割り込み停止
  len = rxLen;
  for (uint8_t i = 0; i < len; i++) buf[i] = rxBuf[i];
  newData = false;
  interrupts();                                      // 割り込み再開

  if (len == 0) return;

  // 受信データの1バイト目がコマンドの種別
  uint8_t cmd = buf[0];

  // ---- CONFIG ----
  // マスターから「このパート設定で動け」と個別に送られてくる（起動時1回）
  if (cmd == CMD_CONFIG && len >= 4) {
    entry_offset = buf[1];  // 何小節目から演奏開始するか
    loop_length  = buf[2];  // 1ループの小節数
    part_id      = buf[3];  // パート番号
    Serial.print("[CONFIG] offset=");
    Serial.print(entry_offset);
    Serial.print(" loop=");
    Serial.print(loop_length);
    Serial.print(" part=");
    Serial.println(part_id);

  // ---- START ----
  // 「SYNC を受け取ったら演奏していい」という許可
  } else if (cmd == CMD_START) {
    started = true;
    playing = false;
    Serial.println("[START]  waiting for SYNC...");

  // ---- SYNC ----
  // 「今ちょうど第 global_bar 小節の頭だ」という定期通知
  } else if (cmd == CMD_SYNC && len >= 5) {
    // 2バイトに分けて送られてきた bar と bpm を元の値に戻す
    uint16_t global_bar = ((uint16_t)buf[1] << 8) | buf[2];
    uint16_t bpm_x10    = ((uint16_t)buf[3] << 8) | buf[4];

    // 自分が今何小節目を演奏すべきかを計算する
    // 例: global_bar=5, entry_offset=2 → local_bar=3（自分の3小節目）
    // 負の値のとき（local_bar < 0）はまだ自分の出番ではない
    int16_t local_bar = (int16_t)global_bar - (int16_t)entry_offset;

    Serial.print("[SYNC]   global=");
    Serial.print(global_bar);
    Serial.print(" local=");
    Serial.print(local_bar);
    Serial.print(" bpm=");
    Serial.print(bpm_x10 / 10);
    Serial.print(".");
    Serial.print(bpm_x10 % 10);

    if (!started) {
      Serial.println("  <- START not received yet");
    } else if (local_bar < 0) {
      // entry_offset に達していないのでまだ待機
      Serial.println("  <- waiting");
    } else {
      // ループ内の何小節目かを計算（loop_length で割った余り）
      uint8_t pos = (uint8_t)(local_bar % loop_length);
      if (!playing) {
        playing = true;
        Serial.println("  <- playing start!");
      } else {
        Serial.print("  <- loop bar ");
        Serial.println(pos);
      }
    }

  // ---- 不明コマンド ----
  } else {
    Serial.print("[UNKNOWN] cmd=0x");
    Serial.print(cmd, HEX);
    Serial.print(" len=");
    Serial.println(len);
  }
}
