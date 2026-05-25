import ddf.minim.*;          // 音声合成ライブラリ Minim 本体を読み込む
import ddf.minim.ugens.*;    // Oscil・ADSR・MoogFilter などの音作り部品(UGen)を使えるようにする

Minim       minim;           // Minim ライブラリ全体を管理するオブジェクト
AudioOutput out;             // 実際にスピーカーへ音を出す出力ライン

String[] melody = { "C4" };  // 今週は動作確認用に単音(ド)だけを鳴らす

// 1音分のクラリネット音色を作るクラス（まずはトーン用のmainEnvのみ）
class ClarinetInstrument implements Instrument {
  ADSR mainEnv;              // メイン波形の音量エンベロープ（仕様書のmainEnvに相当）

  ClarinetInstrument(float freq, float amp) {            // 引数:周波数(Hz), 振幅(0.0〜1.0)
    // 仕様書 表2.4 の実測FFT値に基づく倍音テーブル（まずは第8倍音まで）
    float[] harmonicAmp = {
      1.000f, 0.013f, 1.216f, 0.023f,                    // 1f(基音), 2f, 3f, 4f
      0.447f, 0.011f, 0.132f, 0.039f                     // 5f, 6f, 7f, 8f（奇数倍音が大きい）
    };
    Waveform wf  = WavetableGenerator.gen10(8192, harmonicAmp); // 倍音配列から8192点の合成波形を作る（加算合成）
    Oscil    osc = new Oscil(freq, amp, wf);             // その波形で発振する基本発振器を作る

    // 高域の耳障りな成分を削るローパスフィルタ（カットオフは音程に連動、上限1400Hz）
    MoogFilter lpf = new MoogFilter(min(1400.0f, freq * 5.0f), 0.5f, MoogFilter.Type.LP);
    osc.patch(lpf);                                       // 発振器の出力をローパスフィルタへ送る

    // メイン音の音量エンベロープ（Attack, Decay, Sustain, Release）
    mainEnv = new ADSR(amp, 0.180f, 0.060f, 0.92f, 0.260f); // 立ち上がり0.18秒・余韻0.26秒の暫定値
    lpf.patch(mainEnv);                                  // フィルタ後の信号を音量エンベロープへ送る
  }

  void noteOn(float duration) {                          // 発音開始時にMinimがdurationをつけて呼ぶ
    mainEnv.patch(out); mainEnv.noteOn();                // メイン音を出力につなぎ、アタックを開始する
  }

  void noteOff() {                                       // 発音終了時にMinimが自動で呼ぶ
    mainEnv.noteOff(); mainEnv.unpatchAfterRelease(out); // リリース後、出力から自動で切り離す（後始末）
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
  text("BPM: 100  |  C4 単音テスト (mainEnvのみ)", 20, 70); // 状態を画面に表示する
}

void playSong() {                                        // メロディ（今週は単音）を再生する関数
  out.pauseNotes();                                      // いったん発音予約を止める（まとめて登録するため）
  for (int i = 0; i < melody.length; i++) {              // melody配列の音を順番に処理する
    out.playNote(i * 1.0f, 2.0f,                         // 開始時刻(i拍目), 長さ2拍 で発音予約する
      new ClarinetInstrument(Frequency.ofPitch(melody[i]).asHz(), 0.5f)); // 音名→周波数に変換、振幅0.5固定で発音
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
