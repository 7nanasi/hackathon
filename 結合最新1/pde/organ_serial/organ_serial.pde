// organ_serial.pde
// ArduinoからバイナリSerialでNOTE_EVENTを受信し、
// Minimライブラリでオルガン音を即時再生する
//
// 起動するとコンソールに利用可能なシリアルポート一覧が表示される。
// 番号を確認して SERIAL_PORT 定数を変更し再起動すること。
// ─────────────────────────────────────────────────────────────

import ddf.minim.*;
import ddf.minim.ugens.*;
import processing.serial.*;

// ─── シリアルポート設定（環境に合わせて変更する）──────────────
// Mac: "/dev/cu.usbmodem****"
// 起動時にコンソールに番号が出るので確認して書き換える
final String SERIAL_PORT = "/dev/cu.usbmodem34B7DA636BDC2";
final int BAUD_RATE = 115200;

// ─── バイナリプロトコル定数 ───────────────────────────────────
final int SERIAL_MARKER = 0xAA;
final int INST_ORGAN    = 0x03;

// ─── Minim関連 ────────────────────────────────────────────────
Minim minim;
AudioOutput out;

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
int    noteCount = 0;
int    lastPitch = 0;
int    lastDurMs = 0;
String statusMsg = "Waiting for Arduino...";

// ─── setup() ──────────────────────────────────────────────────
void setup() {
  size(512, 280);
  textSize(14);

  minim = new Minim(this);
  out   = minim.getLineOut();
  out.setTempo(120);

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

// ─── draw()：波形表示 + ステータス ────────────────────────────
void draw() {
  background(20);

  stroke(0, 200, 100);
  noFill();
  for (int i = 0; i < out.bufferSize() - 1; i++) {
    line(i,     80  - out.left.get(i)     * 60,
         i + 1, 80  - out.left.get(i + 1) * 60);
    line(i,     170 - out.right.get(i)     * 60,
         i + 1, 170 - out.right.get(i + 1) * 60);
  }

  fill(255);
  noStroke();
  text(statusMsg, 10, 210);
  text("受信音符数: " + noteCount, 10, 230);

  if (lastPitch > 0) {
    text("最終音符: MIDI=" + lastPitch + "  " + midiName(lastPitch)
       + "  dur=" + lastDurMs + "ms", 10, 250);
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

    case 2:  // ペイロード3バイト受信中 (pitch, vel, dur8ms)
      rxBuf[rxCount++] = b;
      if (rxCount >= 3) {
        processNoteEvent(rxInstId, rxBuf[0], rxBuf[1], rxBuf[2]);
        rxState = 0;
      }
      break;

    default:
      rxState = 0;
  }
}

// ─── NOTE_EVENTパケット処理 ───────────────────────────────────
// instId: 楽器種別 (3=オルガン)
// pitch:  MIDIノート番号
// vel:    velocity
// dur8ms: 演奏時間 ÷ 8 (×8してmsに戻す)
void processNoteEvent(int instId, int pitch, int vel, int dur8ms) {
  if (pitch == 0 || vel == 0) return;

  int   durationMs  = dur8ms * 8;
  float durationSec = durationMs / 1000.0;
  float freqHz      = midiToHz(pitch);
  float amp         = (vel / 127.0) * 0.30;

  lastPitch = pitch;
  lastDurMs = durationMs;
  noteCount++;
  statusMsg = "Playing MIDI " + pitch + " (" + midiName(pitch) + ")  " + durationMs + "ms";

  out.pauseNotes();
  if (instId == INST_ORGAN) {
    out.playNote(0.0, durationSec,
      new OrganInstrument(freqHz, amp));
  }
  out.resumeNotes();
}

// ─── オルガン音声合成クラス ────────────────────────────────────
// 4オシレーターをわずかにデチューンして重ねるコーラス方式
class OrganInstrument implements Instrument {
  Summer mix;
  ADSR   adsr;

  OrganInstrument(float frequency, float amp) {
    mix = new Summer();

    float f1 = frequency * pow(2, -6.0f / 1200.0f);
    float f2 = frequency * pow(2, -2.0f / 1200.0f);
    float f3 = frequency * pow(2,  2.0f / 1200.0f);
    float f4 = frequency * pow(2,  6.0f / 1200.0f);

    Waveform wf = WavetableGenerator.gen10(8192,
      new float[] {0.5f, 0.8f, 0.5f, 0.8f, 0.5f, 0.65f, 0.5f, 0.9f});

    Oscil osc1 = new Oscil(f1, amp / 4.0f, wf);
    Oscil osc2 = new Oscil(f2, amp / 4.0f, wf);
    Oscil osc3 = new Oscil(f3, amp / 4.0f, wf);
    Oscil osc4 = new Oscil(f4, amp / 4.0f, wf);

    osc1.patch(mix);
    osc2.patch(mix);
    osc3.patch(mix);
    osc4.patch(mix);

    adsr = new ADSR(amp, 0.08f, 0.0f, 1.0f, 0.3f);
    mix.patch(adsr);
  }

  void noteOn(float duration) {
    adsr.patch(out);
    adsr.unpatchAfterRelease(out);
    adsr.noteOn();
  }

  void noteOff() {
    adsr.noteOff();
  }
}

// ─── MIDIノート番号 → 周波数(Hz) 変換 ──────────────────────
float midiToHz(int midiNote) {
  // A4=440Hz = MIDIノート69
  return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

// ─── MIDIノート番号 → 音名文字列（デバッグ表示用）───────────
String midiName(int n) {
  String[] names = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  return names[n % 12] + (n / 12 - 1);
}

// ─── stop() ───────────────────────────────────────────────────
void stop() {
  if (myPort != null) myPort.stop();
  minim.stop();
  super.stop();
}
