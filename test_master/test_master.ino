// I²C ライブラリを読み込む
// SDA(A4)・SCL(A5) の2本でデータをやり取りできるようになる
#include <Wire.h>

void setup() {
  // マスターとして I²C を開始
  // マスターはアドレスを指定しない（スレーブだけアドレスが必要）
  Wire.begin();

  // シリアルモニターを開始（9600bps）
  Serial.begin(9600);
}

void loop() {
  // アドレス 8 のスレーブへ送信を開始
  Wire.beginTransmission(8);

  // 送りたいデータを書き込む（ここでは文字列 "hello"）
  Wire.write("hello");

  // 送信を確定して I²C バスを解放する
  Wire.endTransmission();

  // シリアルモニターで送信したことを確認
  Serial.println("Sent: hello");

  // 0.5秒待ってから繰り返す
  delay(500);
}
