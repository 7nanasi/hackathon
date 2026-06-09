// I²C ライブラリを読み込む
#include <Wire.h>

void setup() {
  // アドレス 8 のスレーブとして I²C を開始
  // マスターはこのアドレス宛てにデータを送ってくる
  Wire.begin(8);

  // データを受信したときに呼び出す関数を登録する
  Wire.onReceive(receiveEvent);

  // シリアルモニターを開始（9600bps）
  Serial.begin(9600);
}

void loop() {
  // 受信の処理は receiveEvent に任せるので、ここは何もしない
}

// マスターからデータが届くと自動的に呼び出される関数
// bytes: 受信したバイト数
void receiveEvent(int bytes) {
  // 受信バッファにデータが残っている間、1文字ずつ読み出す
  while (Wire.available()) {
    char c = Wire.read();   // 1バイト読み込む
    Serial.print(c);        // シリアルモニターに表示
  }
  // 1回分の受信が終わったら改行して見やすくする
  Serial.println();
}
