// drum.pde
// ArduinoからバイナリSerialでドラムイベントを受信し、
// processing.soundライブラリでキック・ハイハットを再生する
//
// 起動するとコンソールに利用可能なシリアルポート一覧が表示される。
// 番号を確認して SERIAL_PORT 定数を変更し再起動すること。
// ─────────────────────────────────────────────────────────────

import processing.sound.*;
import processing.serial.*;

// ─── シリアルポート設定（環境に合わせて変更する）──────────────
final String SERIAL_PORT = "/dev/cu.usbmodem34B7DA636BDC2";
final int BAUD_RATE = 115200;

// ─── バイナリプロトコル定数 ───────────────────────────────────
final int SERIAL_MARKER = 0xAA;
final int INST_DRUM     = 0x04;
final int DRUM_KICK     = 36;
final int DRUM_HIHAT    = 42;

// ─── Serial関連 ───────────────────────────────────────────────
Serial myPort;

// バイナリパケット受信ステートマシン
// 状態0: マーカー(0xAA)待ち
// 状態1: 楽器種別バイト待ち
// 状態2: ペイロード3バイト受信中 (pitch, velocity, duration_8ms)
int   rxState  = 0;
int   rxInstId = 0;
int   rxCount  = 0;
int[] rxBuf    = new int[3];

// ─── デバッグ表示用 ──────────────────────────────────────────
int    hitCount  = 0;
String statusMsg = "Waiting for Arduino...";
String lastHit   = "";

// ─── キック =====
SinOsc     kickOsc;
WhiteNoise kickNoise;
boolean    kickHit       = false;
float      kickStart     = -1;
float      kickStartFreq = 240.0;
float      kickEndFreq   = 55.0;
float      kickEnvLen    = 0.5;

// ─── ハイハット =====
WhiteNoise hihatNoise;
BandPass   hihatBP;
boolean    hihatHit   = false;
float      hihatStart = -1;

// ─── setup() ──────────────────────────────────────────────────
void setup() {
  size(800, 260);
  textSize(14);

  // キック
  kickOsc = new SinOsc(this);
  kickOsc.play();
  kickOsc.amp(0);

  kickNoise = new WhiteNoise(this);
  kickNoise.play();
  kickNoise.amp(0);

  // ハイハット
  hihatNoise = new WhiteNoise(this);
  hihatNoise.play();
  hihatNoise.amp(0);

  hihatBP = new BandPass(this);
  hihatBP.process(hihatNoise, 7000, 4000);

  // 利用可能なシリアルポートをコンソールに表示
  println("=== 利用可能なシリアルポート ===");
  String[] ports = Serial.list();
  for (int i = 0; i < ports.length; i++) {
    println("[" + i + "] " + ports[i]);
  }
  println("================================");
  println("SERIAL_PORT 定数を上記から選んで書き換えてください");

  // シリアルポートを開く
  try {
    myPort   = new Serial(this, SERIAL_PORT, BAUD_RATE);
    statusMsg = "接続済み: " + SERIAL_PORT;
  } catch (Exception e) {
    statusMsg = "Serial エラー: " + e.getMessage();
    println("シリアル接続失敗: " + e.getMessage());
  }
}

// ─── draw() ───────────────────────────────────────────────────
void draw() {
  background(0);

  // ─── キック処理 ─────────────────────────────────────────────
  if (kickHit) {
    float t = millis() / 1000.0 - kickStart;
    float x = constrain(t / kickEnvLen, 0, 1);

    float pitchEnv = exp(-18.0 * x);
    float freq = kickEndFreq + (kickStartFreq - kickEndFreq) * pitchEnv;
    kickOsc.freq(freq);

    float bodyAmp = 0.9 * exp(-4.0 * x);
    kickOsc.amp(bodyAmp);

    float clickAmp = (t < 0.003) ? 1.0 * (1.0 - t / 0.003) : 0;
    kickNoise.amp(clickAmp);

    if (bodyAmp < 0.001) {
      kickOsc.amp(0);
      kickNoise.amp(0);
      kickHit = false;
    }
  }

  // ─── ハイハット処理 ──────────────────────────────────────────
  if (hihatHit) {
    float t = (millis() - hihatStart) / 1000.0;

    float attack = min(1.0, t * 200.0);
    float decay  = exp(-20 * t);
    float amp    = attack * decay;
    hihatNoise.amp(amp * 5);

    float freq = lerp(8000, 5000, t * 2.0);
    hihatBP.freq(freq);

    if (amp < 0.001) {
      hihatNoise.amp(0);
      hihatHit = false;
    }
  }

  // ─── 表示 ──────────────────────────────────────────────────
  stroke(80);
  line(width / 2, 0, width / 2, 200);
  noStroke();

  fill(255);
  textSize(20);
  textAlign(CENTER, CENTER);
  text("KICK",   width / 4,     100);
  text("HI-HAT", width * 3 / 4, 100);

  fill(180);
  textSize(14);
  textAlign(LEFT, CENTER);
  text(statusMsg, 10, 220);
  text("受信ヒット数: " + hitCount, 10, 240);
  if (!lastHit.equals("")) {
    text("最終ヒット: " + lastHit, 10, 255);  // ← 追加
  }
}

// ─── serialEvent()：Processingのシリアルコールバック ─────────
void serialEvent(Serial port) {
  while (port.available() > 0) {
    int b = port.read() & 0xFF;
    parseSerialByte(b);
  }
}

// ─── バイナリパケット解析ステートマシン ──────────────────────
void parseSerialByte(int b) {
  switch (rxState) {

    case 0:  // マーカー(0xAA)待ち
      if (b == SERIAL_MARKER) {
        rxState = 1;
      }
      break;

    case 1:  // 楽器種別バイト
      rxInstId = b;
      rxCount  = 0;
      rxState  = 2;
      break;

    case 2:  // ペイロード3バイト受信中 (pitch=ドラム種別, vel, dur8ms)
      rxBuf[rxCount++] = b;
      if (rxCount >= 3) {
        if (rxInstId == INST_DRUM) {
          processDrumEvent(rxBuf[0], rxBuf[1]);
        }
        rxState = 0;
      }
      break;

    default:
      rxState = 0;
  }
}

// ─── ドラムイベント処理 ──────────────────────────────────────
// drumType: ドラム種別 (36=KICK, 42=HIHAT)
// vel:      velocity (未使用、将来の音量制御用)
void processDrumEvent(int drumType, int vel) {
  if (vel == 0) return;

  hitCount++;

  if (drumType == DRUM_KICK) {
    kickStart = millis() / 1000.0;
    kickHit   = true;
    lastHit   = "KICK";
  } else if (drumType == DRUM_HIHAT) {
    hihatStart = millis();
    hihatHit   = true;
    lastHit    = "HI-HAT";
  }
}

// ─── マウスクリックでも鳴らせる（テスト用）─────────────────
void mousePressed() {
  if (mouseX < width / 2) {
    kickStart = millis() / 1000.0;
    kickHit   = true;
  } else {
    hihatStart = millis();
    hihatHit   = true;
  }
}

// ─── stop() ───────────────────────────────────────────────────
void stop() {
  if (myPort != null) myPort.stop();
  super.stop();
}
