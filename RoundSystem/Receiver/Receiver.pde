// ============================================================================
//  Receiver — 輪唱システム Processing 側（クラリネット担当PC）
// ----------------------------------------------------------------------------
//  クラリネットの楽器 Arduino が USB シリアルで送る 5バイトパケットを受け取り、
//  クラリネットの音色で鳴らす。Serial は楽器ごとに1対1。
//  （フルート・オルガンはそれぞれ別PC・別スケッチで鳴らす）
//
//  パケット（通信方式説明書 §4・5バイト固定）:
//   [0] 0xAA  / [1] 種別(2=クラリネット)
//   [2] pitch(MIDI) / [3] velocity / [4] duration_8ms(×8でms)
//
//  使い方: SERIAL_PORT を自分の PC につないだ Arduino のポート名にして Run する。
//   （例: macOS "/dev/tty.usbmodem1101" / Windows "COM3"）
// ============================================================================

import ddf.minim.*;
import ddf.minim.ugens.*;
import processing.serial.*;

// ---- 接続設定 ----
final String SERIAL_PORT = "/dev/tty.usbmodem0000"; // 自分の Arduino のポート名に書き換える
final int    SERIAL_BAUD = 115200;

// ---- パケット定数 ----
final int MARKER        = 0xAA;
final int PKT_LEN       = 5;
final int INST_CLARINET = 2;

Minim       minim;
AudioOutput out;
Serial      port;
PacketParser parser;

String lastNoteInfo = "(まだ受信なし)";

// ============================================================================
//  パーサ：0xAA でフレーム同期し、5バイト揃ったら dispatchPacket を呼ぶ。
//  途中のズレは idx==0 に戻ったあと次の 0xAA で自動復帰する。
// ============================================================================
class PacketParser {
  int[] frame = new int[PKT_LEN];
  int   idx = 0;
  int   okCount = 0;
  int   discardCount = 0;

  void feed(int b) {
    b &= 0xFF;
    if (idx == 0) {
      if (b != MARKER) { discardCount++; return; }
      frame[0] = b; idx = 1;
    } else {
      frame[idx++] = b;
      if (idx == PKT_LEN) {
        idx = 0;
        okCount++;
        dispatchPacket(frame[1], frame[2], frame[3], frame[4]);
      }
    }
  }
}

// ============================================================================
//  受信パケットを楽器種別で振り分けて発音する
// ============================================================================
void dispatchPacket(int inst, int pitch, int vel, int dur8) {
  float freq   = midiToFreq(pitch);
  int   durMs  = dur8 * 8;
  float durSec = durMs / 1000.0;
  float amp    = constrain(vel / 127.0, 0, 1) * 0.6;

  lastNoteInfo = instName(inst) + " / " + noteName(pitch)
               + " (" + nf(freq, 0, 1) + "Hz) / " + durMs + "ms";

  if (inst == INST_CLARINET) {
    out.playNote(0, durSec, new ClarinetInstrument(freq, amp, durSec));
  } else {
    println("[スキップ] クラリネット以外の種別 0x" + hex(inst, 2));
  }
}

float midiToFreq(int midi) { return 440.0 * pow(2, (midi - 69) / 12.0); }

String instName(int inst) {
  return (inst == INST_CLARINET) ? "クラリネット" : "不明";
}
String noteName(int midi) {
  String[] n = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  return n[midi % 12] + (midi / 12 - 1);
}

// ============================================================================
//  音色: クラリネット — 奇数倍音が支配的、LPFで丸め、息ノイズとビブラート少々
// ============================================================================
class ClarinetInstrument implements Instrument {
  ADSR env, breathEnv;
  ClarinetInstrument(float freq, float amp, float durSec) {
    float atk = min(0.10, max(0.02, durSec * 0.30));
    float rel = min(0.20, max(0.03, durSec * 0.30));

    // 奇数倍音中心（クラリネットの特徴。h3 を基音より少し強めに）
    float[] h = { 1.0, 0.02, 1.05, 0.03, 0.4, 0.02, 0.15 };
    Waveform wf = WavetableGenerator.gen10(8192, h);
    Oscil osc = new Oscil(freq, amp, wf);

    float depth = freq * (pow(2, 10.0 / 1200.0) - 1.0); // 10セント
    Oscil vib = new Oscil(4.5, depth, Waves.SINE);
    Summer fsum = new Summer();
    new Constant(freq).patch(fsum);
    vib.patch(fsum);
    fsum.patch(osc.frequency);

    float cutoff = constrain(freq * 8.0, 800, 8000);
    MoogFilter lpf = new MoogFilter(cutoff, 0.1, MoogFilter.Type.LP);
    osc.patch(lpf);
    env = new ADSR(amp, atk, 0.03, 0.92, rel);
    lpf.patch(env);

    Noise reed = new Noise(0.0006, Noise.Tint.WHITE);
    MoogFilter hp = new MoogFilter(1500, 0.0, MoogFilter.Type.HP);
    reed.patch(hp);
    breathEnv = new ADSR(0.0006, min(0.01, atk), 0.05, 0.1, min(0.08, rel));
    hp.patch(breathEnv);
  }
  void noteOn(float d) {
    env.patch(out); breathEnv.patch(out);
    env.noteOn();   breathEnv.noteOn();
  }
  void noteOff() {
    env.noteOff(); breathEnv.noteOff();
    env.unpatchAfterRelease(out); breathEnv.unpatchAfterRelease(out);
  }
}

// ============================================================================
//  Processing ライフサイクル
// ============================================================================
void setup() {
  size(620, 240);
  textFont(createFont("Arial", 13));

  minim = new Minim(this);
  out   = minim.getLineOut();  // setTempo は使わない → playNote の引数は「秒」

  parser = new PacketParser();

  try {
    port = new Serial(this, SERIAL_PORT, SERIAL_BAUD);
    println("接続: " + SERIAL_PORT + " @ " + SERIAL_BAUD);
  } catch (Exception e) {
    port = null;
    println("接続失敗: " + SERIAL_PORT + " → " + e.getMessage());
    println("利用可能なポート: " + join(Serial.list(), ", "));
  }
}

void draw() {
  // シリアルから来たバイトを1個ずつパーサへ渡す
  if (port != null) {
    while (port.available() > 0) parser.feed(port.read() & 0xFF);
  }

  background(22, 24, 34);
  fill(150, 200, 255);
  text("Receiver — 輪唱システム（クラリネット）", 16, 28);
  fill(150, 150, 175);
  text("接続: " + (port != null ? SERIAL_PORT : "未接続（SERIAL_PORT を確認）"), 16, 54);

  fill(220, 220, 240);
  text("受信パケット: " + parser.okCount
     + "   読み捨て(ズレ復帰): " + parser.discardCount, 16, 96);
  text("直近の音符: " + lastNoteInfo, 16, 120);
}

void stop() {
  out.close();
  minim.stop();
  super.stop();
}
