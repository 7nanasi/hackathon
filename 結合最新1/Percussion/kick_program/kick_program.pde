import processing.sound.*;

SinOsc osc;
WhiteNoise noise;

float startFreq = 240.0;
float endFreq   = 55.0;

float startTime;

float envLength = 0.5;

void setup() {

  size(400, 200);

  osc = new SinOsc(this);
  osc.play();

  noise = new WhiteNoise(this);
  noise.play();

  trigger();
}

void draw() {

  float t = millis()/1000.0 - startTime;

  float x = constrain(t / envLength, 0, 1);

  // =========================
  // 超高速ピッチ降下
  // =========================

  float pitchEnv = exp(-18.0 * x);

  float freq =
    endFreq +
    (startFreq - endFreq) * pitchEnv;

  osc.freq(freq);

  // =========================
  // 音量エンベロープ
  // =========================

  float bodyAmp =
    0.9 * exp(-4.0 * x);

  osc.amp(bodyAmp);

  // =========================
  // クリック
  // =========================

  float clickAmp = 0;

  // 3msだけ
  if (t < 0.003) {

    clickAmp =
      1.0 * (1.0 - t / 0.003);
  }

  noise.amp(clickAmp);

  background(0);

  fill(255);

  text("freq = " + nf(freq, 1, 1), 20, 20);
}

void mousePressed() {
  trigger();
}

void trigger() {
  startTime = millis()/1000.0;
}