// clarinet_serial.pde
// ArduinoからバイナリSerialでNOTE_EVENTを受信し、
// Minimライブラリでクラリネット音を即時再生する
//
// 通信プロトコルは flute5_serial.pde に揃えている
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
final int SERIAL_MARKER  = 0xAA;
final int INST_CLARINET  = 0x02; // フルート=0x01、クラリネット=0x02
final int INST_DEBUG     = 0x00;

// ─── ClarinetSerialSynth から引き継いだ音色設計用ADSR定数 ──────
final float ATTACK  = 0.400f; // アタック上限: 最大0.4秒かけて音量が上がる
final float SUSTAIN = 0.95f;  // サスティン: 持続中はピーク音量の95%を保つ
final float RELEASE = 0.300f; // リリース上限: 最大0.3秒かけて音量がゼロになる

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
int[] rxBuf    = new int[3]; // pitch, velocity, duration_8ms

// ─── デバッグ表示用 ──────────────────────────────────────────
int    noteCount = 0;
int    lastPitch = 0;
int    lastDurMs = 0;
String statusMsg = "Waiting for Arduino...";
int    lastDriftUs  = 0;
int    lastDriftBar = 0;

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

  // 左右チャンネル波形
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
  text("SYNC誤差: bar=" + lastDriftBar + "  drift=" + lastDriftUs + "μs", 10, 270);
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
// instId: 楽器種別 (2=クラリネット)
// pitch:  MIDIノート番号
// vel:    velocity
// dur8ms: 演奏時間 ÷ 8 (×8してmsに戻す)
void processNoteEvent(int instId, int pitch, int vel, int dur8ms) {
  if (instId == INST_DEBUG) {
    lastDriftUs  = (short)((pitch << 8) | vel);
    lastDriftBar = dur8ms;
    println("bar:" + lastDriftBar + " drift_us:" + lastDriftUs);
    return;
  }
  // pitch=0 または vel=0 は休符として無視
  if (pitch == 0 || vel == 0) return;

  int   durationMs  = dur8ms * 8;
  float durationSec = durationMs / 1000.0;
  float freqHz      = midiToHz(pitch);
  float amp         = map(constrain(vel, 1, 127), 1, 127, 0.08, 0.52); // ClarinetSerialSynthと同じ音量レンジ

  // デバッグ記録
  lastPitch  = pitch;
  lastDurMs  = durationMs;
  noteCount++;
  statusMsg = "Playing MIDI " + pitch + " (" + midiName(pitch) + ")  " + durationMs + "ms";

  // Minimで即時再生 (startTime=0 → 即時)
  out.pauseNotes();
  if (instId == INST_CLARINET) {
    out.playNote(0.0, durationSec,
      new ClarinetInstrument(freqHz, amp));
  }
  out.resumeNotes();
}

// ─── クラリネット音声合成クラス（clarinet2から移植）────────────
class ClarinetInstrument implements Instrument {
  ADSR mainEnv;   // メイン波形の音量エンベロープ
  ADSR reedEnv;   // リードノイズの音量エンベロープ
  ADSR breathEnv; // ブレスノイズの音量エンベロープ
  Line pitchDrop; // アタック直後のピッチ変動を時間で制御するUGen（仕様書外・追加）
  float freq;     // この音の基本周波数。noteOn後も参照するためフィールドに保存

  ClarinetInstrument(float freq, float amp) { // 引数:周波数(Hz), 振幅(0.0〜1.0)
    this.freq = freq;

    // FFT実測（仕様書 表2.4）に基づく倍音テーブル（奇数倍音が支配的でh3>h1）
    float[] harmonicAmp = {
      1.000f, 0.013f, 1.216f, 0.023f,    // h1(基音), h2, h3(基音より強い), h4
      0.447f, 0.011f, 0.132f, 0.039f,    // h5, h6, h7, h8（奇数倍音が支配的・FFT実測値）
    };
    Waveform wf  = WavetableGenerator.gen10(8192, harmonicAmp); // 倍音配列から8192点の合成波形を作る
    Oscil    osc = new Oscil(freq, amp, wf);                    // その波形で発振する基本発振器

    // ビブラート: 4.5Hz / 5セント（仕様書外・追加）
    float  wobbleDepth = freq * (pow(2, 5.0f / 1200.0f) - 1.0f); // 5セント分のHz幅（セント→Hz変換）
    Oscil  wobble      = new Oscil(4.5f, wobbleDepth, Waves.SINE); // 4.5Hzのサイン波でビブラートを生成

    // ピッチドロップ＋ビブラートをSummerでベース周波数に合成する（仕様書外・追加）
    pitchDrop      = new Line();    // 直線的に値が変化するUGen。noteOn()で起動してピッチを落とす
    Summer freqSum = new Summer();  // 複数のUGenの出力を加算して最終的な周波数を合成する
    new Constant(freq).patch(freqSum); // ① ベース周波数（常に一定）
    pitchDrop.patch(freqSum);          // ② ピッチドロップのオフセット（アタック後に0へ収束）
    wobble.patch(freqSum);             // ③ ビブラート（±wobbleDepthで揺れる）
    freqSum.patch(osc.frequency);      // ①②③の合計を発振器の周波数入力に接続する

    // 音域連動LPF: C4付近は暗め・A4付近は明るめ（仕様書のLPFを音域連動でさらに作り込み）
    float      cutoff    = constrain(map(freq, 261.0f, 880.0f, freq * 6.0f, freq * 16.0f), 600.0f, 11000.0f); // C4=基音の6倍・A4=16倍で線形補間、600〜11000Hzに制限
    float      resonance = map(freq, 261.0f, 880.0f, 0.0f, 0.15f);               // 高音域ほど共鳴を強くする（最大0.15）
    MoogFilter lpf       = new MoogFilter(cutoff, resonance, MoogFilter.Type.LP); // ローパスフィルタで倍音の上限を削る
    osc.patch(lpf);                                                                // 発振器 → LPFへ
    mainEnv = new ADSR(amp, ATTACK, 0.040f, SUSTAIN, RELEASE);                    // メイン音のエンベロープ（A/D/S/R）
    lpf.patch(mainEnv);                                                            // LPF → メインエンベロープへ

    // リードノイズ: 仕様書どおり 1200〜2800Hz に絞ったホワイトノイズ
    Noise      noise = new Noise(0.0003f, Noise.Tint.WHITE);               // 振幅0.0003の極小ホワイトノイズ（リードの擦れ感）
    MoogFilter hpf   = new MoogFilter(1200.0f, 0.0f, MoogFilter.Type.HP);  // 1200Hz以下を削るハイパスフィルタ
    MoogFilter nlpf  = new MoogFilter(2800.0f, 0.0f, MoogFilter.Type.LP);  // 2800Hz以上を削るローパスフィルタ
    noise.patch(hpf); hpf.patch(nlpf);                                     // ノイズ → HPF → LPF（1200〜2800Hzだけ残る）
    reedEnv = new ADSR(0.0003f, 0.010f, 0.150f, 0.05f, 0.080f);            // A0.010s, D0.150s, S5%, R0.080s
    nlpf.patch(reedEnv);                                                    // 帯域制限ノイズ → リードエンベロープへ

    // ブレスノイズ: 仕様書どおり 80Hz以下のピンクノイズ
    Noise      breathNoise = new Noise(0.002f, Noise.Tint.PINK);               // 低域が強いピンクノイズ（息の空気感）
    MoogFilter blpf        = new MoogFilter(80.0f, 0.0f, MoogFilter.Type.LP);  // 80Hzより上を削るローパスフィルタ
    breathNoise.patch(blpf);                                                    // ノイズ → LPFへ
    breathEnv = new ADSR(0.002f, ATTACK, 0.040f, SUSTAIN * 0.8f, RELEASE);     // サスティンはメインの80%
    blpf.patch(breathEnv);                                                      // LPF → ブレスエンベロープへ
  }

  void noteOn(float duration) {                          // 発音開始時にMinimが呼ぶ
    pitchDrop.activate(0.06f, freq * 0.15f, 0.0f); // (継続秒, 開始オフセットHz, 終了オフセットHz)
    mainEnv.patch(out);   reedEnv.patch(out);   breathEnv.patch(out); // 各系統を出力につなぐ
    mainEnv.noteOn();   reedEnv.noteOn();   breathEnv.noteOn();       // 各系統のアタック開始
  }

  void noteOff() {                                       // 発音終了時にMinimが呼ぶ
    mainEnv.noteOff();   reedEnv.noteOff();   breathEnv.noteOff();          // 各系統のリリース開始
    mainEnv.unpatchAfterRelease(out);   reedEnv.unpatchAfterRelease(out);   // リリース後に自動切断
    breathEnv.unpatchAfterRelease(out);                                     // リリース後に自動切断
  }
}

// ─── MIDIノート番号 → 周波数(Hz) 変換 ──────────────────────
float midiToHz(int midiNote) {
  // A4=440Hz = MIDIノート69
  return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

// ─── MIDIノート番号 → 音名文字列（デバッグ表示用）───────────
String midiName(int n) {
  String[] names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return names[n % 12] + (n / 12 - 1);
}

// ─── stop() ───────────────────────────────────────────────────
void stop() {
  if (myPort != null) myPort.stop();
  minim.stop();
  super.stop();
}
