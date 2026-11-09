/*
  Yoto Player V3 left/right speaker test
  --------------------------------------
  Same amp start-up as yoto_v3_amp_test.ino, then plays beeps in groups:
    1 beep  = amp 0x34 only
    2 beeps = amp 0x37 only
    3 beeps = both amps
  then a longer pause, and repeats. Listen for which speaker plays each group.

  Each amp is silenced with its own mute bit (register 0x08, bit 0), so this
  doesn't depend on channel-select settings.

  Board: "ESP32 Wrover Module", esp32 core 3.x.
  Run with speakers connected and the programmer unplugged; results go to
  lr_test_log.txt on the SD card (and Serial, if attached).
  Quiet: -24 dB amp volume, 30% sine. To stop: unplug USB-C and the battery.
*/

#include <Wire.h>
#include <ESP_I2S.h>
#include <SD_MMC.h>
#include <stdarg.h>

const int PIN_SDA   = 21;
const int PIN_SCL   = 25;
const int PIN_MCLK  = 0;
const int PIN_BCLK  = 5;
const int PIN_LRCLK = 18;
const int PIN_DOUT  = 19;

const uint8_t IOX_A = 0x20;
const uint8_t IOX_B = 0x21;
const uint8_t AMP_L = 0x34;   // "left" per the V3-E config; this test checks it
const uint8_t AMP_R = 0x37;

const int SAMPLE_RATE    = 48000;
const int TONE_HZ        = 480;
const float TONE_LEVEL   = 0.30;
const int VOLUME_DB_DOWN = 24;

I2SClass i2s;

// ---------- Logging ----------
bool sdOk = false;
String logBuf;

void logf(const char *fmt, ...) {
  char b[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  Serial.print(b);
  logBuf += b;
}

void saveLog() {
  if (!sdOk || logBuf.length() == 0) return;
  File f = SD_MMC.open("/lr_test_log.txt", FILE_APPEND);
  if (f) { f.print(logBuf); f.close(); logBuf = ""; }
}

// ---------- I2C ----------
bool write8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool write16(uint8_t addr, uint8_t reg, uint16_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val >> 8);
  Wire.write(val & 0xFF);
  return Wire.endTransmission() == 0;
}

int32_t read16(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)addr, 2) != 2) return -1;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  return (hi << 8) | lo;
}

bool waitBit(uint8_t amp, int bit, int timeoutMs) {
  for (int t = 0; t < timeoutMs; t += 5) {
    int32_t s = read16(amp, 0x01);
    if (s >= 0 && ((s >> bit) & 1)) return true;
    delay(5);
  }
  return false;
}

bool setupExpanders() {
  bool ok = true;
  ok &= write8(IOX_A, 0x02, 0xFF);
  ok &= write8(IOX_A, 0x03, 0xFF);
  ok &= write8(IOX_B, 0x02, 0x5F);   // IOX.2.4 amp release
  ok &= write8(IOX_B, 0x03, 0xDF);   // IOX.3.1 power hold
  ok &= write8(IOX_A, 0x06, 0xFE);
  ok &= write8(IOX_A, 0x07, 0xFF);
  ok &= write8(IOX_B, 0x06, 0x00);
  ok &= write8(IOX_B, 0x07, 0xD4);
  return ok;
}

// ---------- Amp start-up (same sequence as the working amp test) ----------
bool ampConfigure(uint8_t amp) {
  int32_t id = read16(amp, 0x00);
  if (id != 0x1806) { logf("  0x%02X: chip ID 0x%04X, not an AW88194\n", amp, (unsigned)(id & 0xFFFF)); return false; }
  write16(amp, 0x05, 0x0C08);            // mono (L+R)/2, I2S, 16-bit, 32fs, 48 kHz
  int32_t s = read16(amp, 0x04);
  if (s < 0) return false;
  s |= (1 << 6);                         // I2S on
  s &= ~1;                               // power-down off
  write16(amp, 0x04, (uint16_t)s);
  bool locked = waitBit(amp, 0, 300);
  logf("  0x%02X PLL lock: %s\n", amp, locked ? "yes" : "NO");
  return locked;
}

bool ampStart(uint8_t amp) {
  int32_t s = read16(amp, 0x04);
  if (s < 0) return false;
  s &= ~(1 << 1);                        // amp on
  write16(amp, 0x04, (uint16_t)s);
  bool sw = waitBit(amp, 8, 300);
  logf("  0x%02X switching: %s\n", amp, sw ? "yes" : "NO");
  int32_t v = read16(amp, 0x0F);
  if (v >= 0) {
    uint8_t six = VOLUME_DB_DOWN / 6;
    uint8_t half = ((VOLUME_DB_DOWN % 6) * 2) & 0x0F;
    v = (v & 0x00FF) | ((uint16_t)((six << 4) | half) << 8);
    write16(amp, 0x0F, (uint16_t)v);
  }
  return sw;
}

void setMute(uint8_t amp, bool mute) {
  int32_t p = read16(amp, 0x08);
  if (p < 0) return;
  p = mute ? (p | 1) : (p & ~1);
  write16(amp, 0x08, (uint16_t)p);
}

// ---------- Tone ----------
const int FRAMES = SAMPLE_RATE / TONE_HZ * 4;
int16_t toneBuf[FRAMES * 2];
int16_t silenceBuf[256 * 2];

void playFor(const int16_t *buf, size_t frames, int ms) {
  unsigned long end = millis() + ms;
  while (millis() < end) i2s.write((const uint8_t *)buf, frames * 2 * sizeof(int16_t));
}

void beeps(int n) {
  for (int i = 0; i < n; i++) {
    playFor(toneBuf, FRAMES, 400);
    playFor(silenceBuf, 256, 300);
  }
}

bool okL = false, okR = false;

void setup() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  bool ioxOk = setupExpanders();
  Serial.begin(115200);
  delay(300);
  sdOk = SD_MMC.begin("/sdcard", true);

  logf("\n=== Yoto V3 left/right test (boot at %lu ms) ===\n", millis());
  logf("SD log: %s, expanders: %s\n", sdOk ? "OK" : "not available", ioxOk ? "OK" : "WRITE FAILED");
  delay(10);

  i2s.setPins(PIN_BCLK, PIN_LRCLK, PIN_DOUT, -1, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    logf("I2S start FAILED\n");
    saveLog();
    return;
  }
  memset(silenceBuf, 0, sizeof(silenceBuf));
  playFor(silenceBuf, 256, 20);

  okL = ampConfigure(AMP_L);
  okR = ampConfigure(AMP_R);
  if (okL) okL = ampStart(AMP_L);
  if (okR) okR = ampStart(AMP_R);

  for (int i = 0; i < FRAMES; i++) {
    int16_t s = (int16_t)(sin(2.0 * PI * TONE_HZ * i / SAMPLE_RATE) * TONE_LEVEL * 32767);
    toneBuf[i * 2] = s;
    toneBuf[i * 2 + 1] = s;
  }
  logf("Pattern: 1 beep = 0x34 only, 2 beeps = 0x37 only, 3 beeps = both\n");
  saveLog();
}

int cycles = 0;

void loop() {
  if (!okL && !okR) { delay(1000); return; }

  setMute(AMP_L, false); setMute(AMP_R, true);
  if (cycles < 2) logf("Cycle %d: 0x34 only (1 beep)\n", cycles + 1);
  beeps(1);
  playFor(silenceBuf, 256, 1500);

  setMute(AMP_L, true); setMute(AMP_R, false);
  if (cycles < 2) logf("Cycle %d: 0x37 only (2 beeps)\n", cycles + 1);
  beeps(2);
  playFor(silenceBuf, 256, 1500);

  setMute(AMP_L, false); setMute(AMP_R, false);
  if (cycles < 2) logf("Cycle %d: both (3 beeps)\n", cycles + 1);
  beeps(3);
  playFor(silenceBuf, 256, 3000);

  if (cycles < 2) { saveLog(); cycles++; }
}
