import processing.sound.*;

WhiteNoise noise;
BandPass bp;

float startTime = -1;
boolean hit = false;

void setup() {
  size(400, 200);

  noise = new WhiteNoise(this);

  // 常時再生
  noise.play();

  bp = new BandPass(this);

  // 高域だけ通す
  bp.process(noise, 7000, 4000);

  // 最初は無音
  noise.amp(0);
}

void draw() {

  background(0);

  if (hit) {

    // 経過時間
    float t = (millis() - startTime) / 1000.0;

    // ===== Attack =====
    // 最初の5msで立ち上がる
    float attack = min(1.0, t * 200.0);

    // ===== Decay =====
    // 徐々に減衰
    float decay = exp(-20 * t);

    // 最終音量
    float amp = attack * decay;

    // 音量適用
    noise.amp(amp * 5);

    // フィルタ変化
    float freq = lerp(8000, 5000, t * 2.0);
    bp.freq(freq);

    // 終了
    if (amp < 0.001) {
      noise.amp(0);
      hit = false;
    }
  }

  fill(255);
  textAlign(CENTER, CENTER);
  text("CLICK", width/2, height/2);
}

void mousePressed() {

  startTime = millis();

  hit = true;
}