import ddf.minim.*;          // 音声合成ライブラリ Minim 本体を読み込む
import ddf.minim.ugens.*;    // Oscil・ADSR・MoogFilter・Noise などの音作り部品(UGen)を使えるようにする

Minim       minim;           // Minim ライブラリ全体を管理するオブジェクト
AudioOutput out;             // 実際にスピーカーへ音を出す出力ライン

// 「きらきら星」のクラリネットパート（全曲）
String[] melody = {
  "C4","C4","G4","G4","A4","A4","G4",                  // きらきらひかる
  "F4","F4","E4","E4","D4","D4","C4",                  // おそらのほしよ
  "G4","G4","F4","F4","E4","E4","D4",                  // まばたきしては
  "G4","G4","F4","F4","E4","E4","D4",                  // みんなをみてる
  "C4","C4","G4","G4","A4","A4","G4",                  // きらきらひかる
  "F4","F4","E4","E4","D4","D4","C4"                   // おそらのほしよ
};

// ADSR値
final float ATTACK  = 0.400f; // アタック: 0.4秒かけて音量が上がる
final float SUSTAIN = 0.95f;  // サスティン: 持続中はピーク音量の95%
final float RELEASE = 0.300f; // リリース: 0.3秒かけて音量がゼロになる

// 1音分のクラリネット音色を作るクラス（ADSR: mainEnv / reedEnv / breathEnv）
class ClarinetInstrument implements Instrument {
  ADSR mainEnv;              // メイン波形の音量エンベロープ（mainEnv）
  ADSR reedEnv;              // リードノイズの音量エンベロープ（reedEnv）
  ADSR breathEnv;            // ブレスノイズ（低域空気感）の音量エンベロープ（breathEnv）

  ClarinetInstrument(float freq, float amp) {            // 引数:周波数(Hz), 振幅(0.0〜1.0)
    // FFT実測に基づく倍音テーブル（奇数倍音が支配的）
    float[] harmonicAmp = {
      1.000f, 0.013f, 1.216f, 0.023f,                    // h1(基音), h2, h3, h4
      0.447f, 0.011f, 0.132f, 0.039f,                    // h5, h6, h7, h8
    };
    Waveform wf  = WavetableGenerator.gen10(8192, harmonicAmp); // 倍音配列から8192点の合成波形を作る（加算合成）
    Oscil    osc = new Oscil(freq, amp, wf);             // その波形で発振する基本発振器を作る

    // トーン用LPF: カットオフを音程(ノート周波数)に連動させる（freq*18・上限12000Hz）
    MoogFilter lpf = new MoogFilter(min(12000.0f, freq * 18.0f), 0.0f, MoogFilter.Type.LP);
    osc.patch(lpf);                                       // 発振器 → ローパスフィルタへ送る
    mainEnv = new ADSR(amp, ATTACK, 0.040f, SUSTAIN, RELEASE); // メイン音の音量エンベロープ
    lpf.patch(mainEnv);                                  // フィルタ後の信号 → エンベロープへ送る

    // リードノイズ: 1200〜2800Hz に絞ったホワイトノイズでリードの擦れ感を出す
    Noise      noise = new Noise(0.0003f, Noise.Tint.WHITE);        // 小さなホワイトノイズを発生させる
    MoogFilter hpf   = new MoogFilter(1200.0f, 0.0f, MoogFilter.Type.HP); // 1200Hz以下を削るハイパスフィルタ
    MoogFilter nlpf  = new MoogFilter(2800.0f, 0.0f, MoogFilter.Type.LP); // 2800Hz以上を削るローパスフィルタ
    noise.patch(hpf);                                    // ノイズ → ハイパスへ
    hpf.patch(nlpf);                                     // ハイパス → ローパスへ（1200〜2800Hzだけ残る）
    reedEnv = new ADSR(0.0003f, 0.010f, 0.130f, 0.05f, 0.080f); // リード用ADSR（A0.010,S0.05,R0.080）
    nlpf.patch(reedEnv);                                 // 帯域制限したノイズ → エンベロープへ送る

    // ブレスノイズ: 80Hz以下のピンクノイズで息のふくらみを出す
    Noise      breathNoise = new Noise(0.002f, Noise.Tint.PINK);    // 低域が強いピンクノイズを発生させる
    MoogFilter blpf        = new MoogFilter(80.0f, 0.0f, MoogFilter.Type.LP); // 80Hz以上を削るローパスフィルタ
    breathNoise.patch(blpf);                             // ノイズ → ローパスフィルタへ
    breathEnv = new ADSR(0.002f, ATTACK, 0.040f, 0.6f, RELEASE); // ブレス用ADSR（A/RはmainEnvと同じ）
    blpf.patch(breathEnv);                               // フィルタ後のノイズ → エンベロープへ送る
  }

  void noteOn(float duration) {                          // 発音開始時にMinimが呼ぶ
    mainEnv.patch(out);   mainEnv.noteOn();              // メイン音を出力につなぎ、アタック開始
    reedEnv.patch(out);   reedEnv.noteOn();              // リードノイズを出力につなぎ、アタック開始
    breathEnv.patch(out); breathEnv.noteOn();            // ブレスノイズを出力につなぎ、アタック開始
  }

  void noteOff() {                                       // 発音終了時にMinimが呼ぶ
    mainEnv.noteOff();   mainEnv.unpatchAfterRelease(out);   // メイン音のリリース後、自動で切り離す
    reedEnv.noteOff();   reedEnv.unpatchAfterRelease(out);   // リードノイズのリリース後、切り離す
    breathEnv.noteOff(); breathEnv.unpatchAfterRelease(out); // ブレスノイズのリリース後、切り離す
  }
}

void setup() {                                           // 起動時に一度だけ実行される初期化処理
  size(400, 120);                                        // 動作確認用の小さなウィンドウを作る
  textFont(createFont("Arial", 14));                     // 画面に表示する文字のフォントを設定する
  minim = new Minim(this);                               // Minimを初期化する（thisはこのスケッチ自身）
  out   = minim.getLineOut();                            // 音声出力ラインを取得する
  out.setTempo(100);                                     // テンポを100BPMに設定する（playNoteの拍計算用）
}

void draw() {                                            // 毎フレーム繰り返し実行される描画処理
  background(24, 22, 32);                                // 背景を濃い紫がかった色で塗りつぶす
  fill(160, 130, 255);                                   // 文字色を薄紫に設定する
  text("p キーで再生", 20, 44);                          // 操作方法を画面に表示する
  fill(100, 90, 140);                                    // 文字色を暗めに変える
  text("BPM: 100  |  きらきら星 )", 20, 70); // 状態を画面に表示する
}

void playSong() {                                        // メロディを再生する関数（授業の枠組みを流用）
  out.pauseNotes();                                      // 複数音をまとめて予約登録するあいだ再生を止める（授業の作法）
  for (int i = 0; i < melody.length; i++) {              // melody配列を順番に処理する
    out.playNote(i * 1.3f, 1.0f,                        // i番目を0.5拍ごとに、長さ0.45拍で発音予約する
      new ClarinetInstrument(Frequency.ofPitch(melody[i]).asHz(), 0.5f)); // 音名→周波数に変換、振幅0.5で発音
  }
  out.resumeNotes();                                     // 予約した音の再生を開始する
}

void keyPressed() {                                      // キーが押されたときに呼ばれる
  if (key == 'p') playSong();                            // 「p」キーで再生する
}

void stop() {                                            // スケッチ終了時の後始末
  out.close();                                           // 音声出力ラインを閉じる
  minim.stop();                                          // Minimを停止する
  super.stop();                                          // Processing本体の終了処理を呼ぶ
}
