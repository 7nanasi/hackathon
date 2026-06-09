import ddf.minim.*;          // 音声合成ライブラリ Minim 本体を読み込む
import ddf.minim.ugens.*;    // Oscil・ADSR・MoogFilter・Noise・Summer・Line などの部品を使えるようにする

Minim       minim;           // Minim ライブラリ全体を管理するオブジェクト
AudioOutput out;             // 実際にスピーカーへ音を出す出力ライン

// 「きらきら星」のクラリネットパート
String[] melody = {
  "C4","C4","G4","G4","A4","A4","G4",              // きらきらひかる（フレーズ末に休符）
  "F4","F4","E4","E4","D4","D4","C4",              // おそらのほしよ
  "G4","G4","F4","F4","E4","E4","D4",              // まばたきしては
  "G4","G4","F4","F4","E4","E4","D4",              // みんなをみてる
  "C4","C4","G4","G4","A4","A4","G4",              // きらきらひかる
  "F4","F4","E4","E4","D4","D4","C4"               // おそらのほしよ
};

// 各音の長さ（拍）
float[] beats = {
  1,1,1,1,1,1,2,                                   // 1行目（最後のソは2拍）
  1,1,1,1,1,1,2,                                   // 2行目
  1,1,1,1,1,1,2,                                   // 3行目
  1,1,1,1,1,1,2,                                   // 4行目
  1,1,1,1,1,1,2,                                   // 5行目
  1,1,1,1,1,1,2                                    // 6行目（最後のドは2拍）
};

// 仕様書 表2.3 のADSR値（現状仮値）
final float ATTACK  = 0.400f; // アタック: 0.4秒かけて音量が上がる
final float SUSTAIN = 0.95f;  // サスティン: 持続中はピーク音量の95%を保つ
final float RELEASE = 0.200f; // リリース: 0.3秒かけて音量がゼロになる*******0.1じゃないと無理かも********

// 1音分のクラリネット音色（仕様書要素＋AIブラッシュアップで追加した表現要素）
class ClarinetInstrument implements Instrument {
  ADSR mainEnv;   // メイン波形の音量エンベロープ
  ADSR reedEnv;   // リードノイズの音量エンベロープ
  ADSR breathEnv; // ブレスノイズの音量エンベロープ
  float freq;     // この音の基本周波数。noteOn後も参照するためフィールドに保存

  ClarinetInstrument(float freq, float amp) { // 引数:周波数, 振幅
    this.freq = freq;

    // ── 波形・発振器 ──
    float[] harmonicAmp = {
      1.000f, 0.013f, 1.216f, 0.023f,    // h1(基音), h2, h3, h4
      0.447f, 0.011f, 0.132f, 0.039f,    // h5, h6, h7, h8
    };
    Waveform wf  = WavetableGenerator.gen10(8192, harmonicAmp);
    Oscil    osc = new Oscil(freq, amp, wf);

    // ── ビブラート（周波数合成）──
    float  wobbleDepth = freq * (pow(2, 5.0f / 1200.0f) - 1.0f); // 5セントをHz幅に変換
    Oscil  wobble      = new Oscil(5.0f, wobbleDepth, Waves.SINE);
    Summer freqSum     = new Summer();
    new Constant(freq).patch(freqSum); // ベース周波数
    wobble.patch(freqSum);             // ビブラート
    freqSum.patch(osc.frequency);

    // ── トーン系 ──
    float      cutoff = min(3000.0f, freq * 8.0f); // 音程連動: C4≈2090Hz，上限3000Hz（実測ベース）
    MoogFilter lpf    = new MoogFilter(cutoff, 0.0f, MoogFilter.Type.LP);
    mainEnv           = new ADSR(amp, ATTACK, 0.040f, SUSTAIN, RELEASE);

    // ── リードノイズ系（1200〜2800Hz）はじめの息の音──
    Noise      noise = new Noise(0.1f, Noise.Tint.WHITE);
    MoogFilter hpf   = new MoogFilter(1200.0f, 0.0f, MoogFilter.Type.HP);
    MoogFilter nlpf  = new MoogFilter(2800.0f, 0.0f, MoogFilter.Type.LP);
    reedEnv          = new ADSR(amp * 0.4f, 0.15f, 0.6f, 0.3f, 0.150f);

    // ── ブレスノイズ系（100Hz以下・1段LPF）低周波域の雑音調整──
    Noise      breathNoise = new Noise(0.02f, Noise.Tint.PINK);
    MoogFilter blpf        = new MoogFilter(100.0f, 0.25f, MoogFilter.Type.LP);
    breathEnv              = new ADSR(0.007f, ATTACK, 0.040f, SUSTAIN * 1.0f, RELEASE);

    // ── 配線 ──
    osc.patch(lpf);                    lpf.patch(mainEnv);
    noise.patch(hpf);   hpf.patch(nlpf);   nlpf.patch(reedEnv);
    breathNoise.patch(blpf);           blpf.patch(breathEnv);
  }

  void noteOn(float duration) {                          // 発音開始時にMinimが呼ぶ
    mainEnv.patch(out);   reedEnv.patch(out);   breathEnv.patch(out); // 各系統を出力につなぐ
    mainEnv.noteOn();   reedEnv.noteOn();   breathEnv.noteOn();       // 各系統のアタック開始
  }

  void noteOff() {                                       // 発音終了時にMinimが呼ぶ
    mainEnv.noteOff();   reedEnv.noteOff();   breathEnv.noteOff();          // 各系統のリリース開始
    mainEnv.unpatchAfterRelease(out);   reedEnv.unpatchAfterRelease(out);   // リリース後に自動切断（仕様書外・追加）
    breathEnv.unpatchAfterRelease(out);                                     // リリース後に自動切断
  }
}

// テンポ設定（ここを変えると曲全体の速さが変わる）
final float BPM = 100.0f;                                // 1分間の拍数。小さくすると遅くなる

void setup() {                                           // 起動時に一度だけ実行される初期化処理
  size(400, 120);                                        // 動作確認用の小さなウィンドウを作る
  textFont(createFont("Arial", 14));                     // 表示フォントを設定する
  minim = new Minim(this);                               // Minimを初期化する
  out   = minim.getLineOut();                            // 音声出力ラインを取得する
  out.setTempo(BPM);                                     // テンポをBPMに設定する
}

void draw() {                                            // 毎フレーム繰り返される描画処理
  background(24, 22, 32);                                // 背景を塗りつぶす
  fill(160, 130, 255);                                   // 文字色を薄紫に設定する
  text("p キーで再生", 20, 44);                          // 操作方法を表示する
  fill(100, 90, 140);                                    // 文字色を暗めにする
  text("BPM: " + int(BPM) + "  |  きらきら星", 20, 70); // 状態を表示する
}

void playSong() {                                        // メロディを再生する関数
  out.pauseNotes();                                      // まとめて予約登録するあいだ再生を止める
  float t = 0.0f;                                        // 現在の再生位置（拍）
  for (int i = 0; i < melody.length; i++) {              // melody配列を順番に処理する
    float dur = beats[i];                                // この音の長さ（拍）
    out.playNote(t, dur,                                 // 再生位置t（拍）から長さdur（拍）で発音予約する
      new ClarinetInstrument(Frequency.ofPitch(melody[i]).asHz(), 0.5f)); // 音名→周波数に変換して発音予約
    t += dur;                                            // 長さの分だけ再生位置を進める
  }
  out.resumeNotes();                                     // 予約した音の再生を開始する
}

void keyPressed() {                                      // キーが押されたとき
  if (key == 'p') playSong();                            // 「p」キーで再生する
}

void stop() {                                            // 終了時の後始末
  out.close();                                           // 音声出力ラインを閉じる
  minim.stop();                                          // Minimを停止する
  super.stop();                                          // Processing本体の終了処理を呼ぶ
}
