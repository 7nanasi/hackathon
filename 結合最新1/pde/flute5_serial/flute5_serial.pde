// flute5_serial.pde
// ArduinoからバイナリSerialでNOTE_EVENTを受信し、
// Minimライブラリでフルート音を即時再生する
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
final String SERIAL_PORT = "/dev/cu.usbmodem1101";
final int BAUD_RATE = 115200;

// ─── バイナリプロトコル定数 ───────────────────────────────────
final int SERIAL_MARKER = 0xAA;
final int INST_FLUTE    = 0x01;

// ─── Minim関連 ────────────────────────────────────────────────
Minim minim;
AudioOutput out;
Waveform currentWaveform;

// ─── Serial関連 ───────────────────────────────────────────────
Serial myPort;

// バイナリパケット受信ステートマシン
// 状態0: マーカー(0xAA)待ち
// 状態1: 楽器種別バイト待ち
// 状態2: ペイロード3バイト受信中 (pitch, velocity, duration_8ms)
int  rxState   = 0;
int  rxInstId  = 0;  // 受信した楽器種別
int  rxCount   = 0;
int[] rxBuf    = new int[3];  // pitch, velocity, duration_8ms

// ─── デバッグ表示用 ──────────────────────────────────────────
int    noteCount  = 0;
int    lastPitch  = 0;
int    lastDurMs  = 0;
String statusMsg  = "Waiting for Arduino...";

// ─── setup() ──────────────────────────────────────────────────
void setup() {
  size(512, 280);
  textSize(14);

  minim = new Minim(this);
  out   = minim.getLineOut();
  out.setTempo(120);

  currentWaveform = makeFluteWaveform();

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

  // 左チャンネル波形
  stroke(0, 200, 100);
  noFill();
  for (int i = 0; i < out.bufferSize() - 1; i++) {
    line(i,     80 - out.left.get(i)     * 60,
         i + 1, 80 - out.left.get(i + 1) * 60);
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
// instId: 楽器種別 (1=フルート)
// pitch:  MIDIノート番号
// vel:    velocity
// dur8ms: 演奏時間 ÷ 8 (×8してmsに戻す)
void processNoteEvent(int instId, int pitch, int vel, int dur8ms) {
  // pitch=0 または vel=0 は休符として無視
  if (pitch == 0 || vel == 0) return;

  int durationMs = dur8ms * 8;
  float durationSec = durationMs / 1000.0;
  float freqHz = midiToHz(pitch);
  float amp    = (vel / 127.0) * 0.30;

  // デバッグ記録
  lastPitch  = pitch;
  lastDurMs  = durationMs;
  noteCount++;
  statusMsg = "Playing MIDI " + pitch + " (" + midiName(pitch) + ")  " + durationMs + "ms";

  // Minimで即時再生 (startTime=0 → 即時)
  out.pauseNotes();
  if (instId == INST_FLUTE) {
    out.playNote(0.0, durationSec,
      new FluteInstrument(freqHz, amp, currentWaveform));
  }
  out.resumeNotes();
}

// ─── フルート音声合成クラス（flute5.pdeから移植）─────────────
class FluteInstrument implements Instrument {
  ADSR toneEnv;
  ADSR breathEnv;
  ADSR breathEnv2;

  FluteInstrument(float frequency, float maxAmp, Waveform wf) {
    float cutoff = (float)Math.min(9000.0, frequency * 9.0);

    Oscil tone = new Oscil(frequency, 1.0f, wf);

    float vibrateSpeed = 4.3f;
    float vibratoSize  = 15.0f;
    float vibratoNow   = frequency * (pow(2, vibratoSize / 1200.0f) - 1.0f);

    Oscil vibrato = new Oscil(vibrateSpeed, vibratoNow, Waves.SINE);
    vibrato.offset.setLastValue(frequency);
    vibrato.patch(tone.frequency);

    MoogFilter toneLPF    = new MoogFilter(cutoff, 0.0f, MoogFilter.Type.LP);
    Noise      breath     = new Noise(1.0f, Noise.Tint.WHITE);
    MoogFilter breathBand = new MoogFilter(2000.0f, 0.15f, MoogFilter.Type.BP);

    toneEnv   = new ADSR(maxAmp,         0.08f, 0.14f, 0.82f, 0.16f);
    breathEnv = new ADSR(maxAmp * 0.14f, 0.015f, 0.12f, 0.04f, 0.10f);

    Noise      breath2     = new Noise(1.0f, Noise.Tint.WHITE);
    MoogFilter breathBand2 = new MoogFilter(700.0f, 0.2f, MoogFilter.Type.BP);
    breathEnv2 = new ADSR(maxAmp * 0.11f, 0.015f, 0.12f, 0.07f, 0.10f);

    tone.patch(toneLPF);
    toneLPF.patch(toneEnv);
    breath.patch(breathBand);
    breathBand.patch(breathEnv);
    breath2.patch(breathBand2);
    breathBand2.patch(breathEnv2);
  }

  void noteOn(float duration) {
    toneEnv.patch(out);
    breathEnv.patch(out);
    breathEnv2.patch(out);

    toneEnv.unpatchAfterRelease(out);
    breathEnv.unpatchAfterRelease(out);
    breathEnv2.unpatchAfterRelease(out);

    toneEnv.noteOn();
    breathEnv.noteOn();
    breathEnv2.noteOn();
  }

  void noteOff() {
    toneEnv.noteOff();
    breathEnv.noteOff();
    breathEnv2.noteOff();
  }
}

// ─── フルート波形生成（flute5.pdeから移植）───────────────────
Wavetable makeFluteWaveform() {
  Wavetable wf = WavetableGenerator.gen10(
    8192,
    new float[] {
      1.00f, 0.35f, 0.14f, 0.12f,
      0.08f, 0.04f, 0.025f, 0.02f, 0.015f, 0.010f
    }
  );
  wf.normalize();
  wf.scale(0.75f);
  return wf;
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
