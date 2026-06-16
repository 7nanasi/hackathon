#include <Wire.h>

const byte NUM_SLAVES = 3;
const byte slaveADRs[NUM_SLAVES] = {0x10, 0x11, 0x12};

const byte CMD_SYNC   = 0x01;
const byte CMD_START  = 0x10;
const byte CMD_CONFIG = 0x11;

uint16_t global_bar = 0;
uint16_t bpmX10 = 1200;

unsigned long nextSyncUs = 0;

void setup() {
  Wire.begin();
  Serial.begin(115200);

  delay(500);

  for (byte i = 0; i < NUM_SLAVES; i++) {
    sendConfig(slaveADRs[i], i * 2, 16, i + 1);
  }

  delay(100);

  sendStartToAll();

  nextSyncUs = micros() + 500000UL;
}

void loop() {
  unsigned long now = micros();

  if ((long)(now - nextSyncUs) >= 0) {
    sendSyncToAll(global_bar, bpmX10);

    global_bar++;
    nextSyncUs += calcBarUs(bpmX10);
  }
}

unsigned long calcBarUs(uint16_t bpm_x10) {
  if (bpm_x10 == 0) return 2000000UL;
  return 60000000UL * 4UL * 10UL / bpm_x10;
}

void sendStartToAll() {
  for (byte i = 0; i < NUM_SLAVES; i++) {
    sendStart(slaveADRs[i]);
  }
}

void sendSyncToAll(uint16_t song_bar, uint16_t bpm_x10) {
  for (byte i = 0; i < NUM_SLAVES; i++) {
    sendSync(slaveADRs[i], song_bar, bpm_x10);
  }
}

void sendSync(byte targetADR, uint16_t song_bar, uint16_t bpm_x10) {
  Wire.beginTransmission(targetADR);
  Wire.write(CMD_SYNC);
  Wire.write(highByte(song_bar));
  Wire.write(lowByte(song_bar));
  Wire.write(highByte(bpm_x10));
  Wire.write(lowByte(bpm_x10));
  Wire.endTransmission();
}

void sendStart(byte targetADR) {
  Wire.beginTransmission(targetADR);
  Wire.write(CMD_START);
  Wire.endTransmission();
}

void sendConfig(byte targetADR, byte entry_offset, byte loop_length, byte part_id) {
  Wire.beginTransmission(targetADR);
  Wire.write(CMD_CONFIG);
  Wire.write(entry_offset);
  Wire.write(loop_length);
  Wire.write(part_id);
  Wire.endTransmission();
}
