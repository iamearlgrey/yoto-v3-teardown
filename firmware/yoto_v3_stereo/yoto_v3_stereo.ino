/*
  Yoto Player V3 true stereo test
  -------------------------------
  Same amp start-up as the left/right test, but instead of muting one amp,
  each amp is told which half of the stereo signal to play:
    0x34 (left speaker)  -> I2S left channel   (I2SCTRL 0x05 = 0x0408)
    0x37 (right speaker) -> I2S right channel  (I2SCTRL 0x05 = 0x0808)
  Both amps stay unmuted the whole time; only the audio decides what you hear.

  AW88194 I2SCTRL 0x05: CHSEL bits 11:10 (1 = left, 2 = right, 3 = mono mix),
  I2SMD 9:8 = 0 (I2S), I2SFS 7:6 = 0 (16-bit), I2SBCK 5:4 = 0 (32fs),
  I2SSR 3:0 = 8 (48 kHz).

  Pattern, repeating:
    A. Low tone in the LEFT channel only           (expect: left speaker)
    B. High tone in the RIGHT channel only         (expect: right speaker)
    C. Sweep: one tone slides from left to right   (expect: sound moves)
    D. Low tone left AND high tone right together  (expect: low left, high right)

  Board: "ESP32 Wrover Module", esp32 core 3.x.
  Speakers connected, programmer unplugged; log goes to stereo_test_log.txt.
  Quiet: -24 dB amp volume, 30% level. To stop: unplug USB-C and the battery.
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
const uint8_t AMP_L = 0x34;   // left speaker, confirmed Nov 9
const uint8_t AMP_R = 0x37;   // right speaker, confirmed Nov 9

const uint16_t I2SCTRL_LEFT  = 0x0408;
const uint16_t I2SCTRL_RIGHT = 0x0808;

const int SAMPLE_RATE    = 48000;
const float LOW_HZ       = 400.0;
const float HIGH_HZ      = 800.0;
const float LEVEL        = 0.30;
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
  File f = SD_MMC.open("/stereo_test_log.txt", FILE_APPEND);
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

// ---------- Amp start-up ----------
bool ampConfigure(uint8_t amp, uint16_t i2sctrl, const char *side) {
  int32_t id = read16(amp, 0x00);
  if (id != 0x1806) { logf("  0x%02X: chip ID 0x%04X, not an AW88194\n", amp, (unsigned)(id & 0xFFFF)); return false; }
  write16(amp, 0x05, i2sctrl);
  int32_t back = read16(amp, 0x05);
  logf("  0x%02X I2SCTRL set to 0x%04X (%s channel), reads back 0x%04X %s\n",
       amp, i2sctrl, side, (unsigned)(back & 0xFFFF), back == i2sctrl ? "OK" : "MISMATCH");
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
  int32_t p = read16(amp, 0x08);
  if (p >= 0) write16(amp, 0x08, (uint16_t)(p & ~1));   // unmute
  return sw;
}

// ---------- Stereo audio ----------
// Frames are [left, right] pairs. Phases carry over between chunks so tones stay smooth.
// Sine comes from a lookup table (fast); pan gains are worked out once per chunk,
// so the ESP32 never falls behind the I2S output (that caused the stutter).
const int CHUNK = 256;            // 5.3 ms at 48 kHz
const int TABLE_BITS = 10;
const int TABLE_SIZE = 1 << TABLE_BITS;
float sineTable[TABLE_SIZE];
int16_t buf[CHUNK * 2];
uint32_t phaseL = 0, phaseR = 0;  // 32-bit phase accumulators (top bits index the table)

void buildSineTable() {
  for (int i = 0; i < TABLE_SIZE; i++) sineTable[i] = sinf(2.0f * PI * i / TABLE_SIZE);
}

inline float sineAt(uint32_t phase) { return sineTable[phase >> (32 - TABLE_BITS)]; }

// Play for ms. gain functions take t = 0..1 across the segment.
void playSegment(int ms, float freqL, float freqR,
                 float (*gainL)(float), float (*gainR)(float)) {
  long total = (long)SAMPLE_RATE * ms / 1000;
  long done = 0;
  const uint32_t stepL = (uint32_t)(freqL / SAMPLE_RATE * 4294967296.0);
  const uint32_t stepR = (uint32_t)(freqR / SAMPLE_RATE * 4294967296.0);
  const float scale = LEVEL * 32767.0f;
  while (done < total) {
    int n = (total - done) < CHUNK ? (total - done) : CHUNK;
    // gains at the start and end of this chunk, blended across it
    float t0 = (float)done / total, t1 = (float)(done + n) / total;
    float gL0 = gainL(t0), gL1 = gainL(t1);
    float gR0 = gainR(t0), gR1 = gainR(t1);
    for (int i = 0; i < n; i++) {
      float f = (float)i / n;
      float gL = gL0 + (gL1 - gL0) * f;
      float gR = gR0 + (gR1 - gR0) * f;
      // short fade in/out to avoid clicks
      long pos = done + i;
      float edge = 1.0f;
      if (pos < 480) edge = pos / 480.0f;
      if (total - pos < 480) edge = (total - pos) / 480.0f;
      buf[i * 2]     = (int16_t)(sineAt(phaseL) * gL * edge * scale);
      buf[i * 2 + 1] = (int16_t)(sineAt(phaseR) * gR * edge * scale);
      phaseL += stepL;
      phaseR += stepR;
    }
    i2s.write((const uint8_t *)buf, n * 2 * sizeof(int16_t));
    done += n;
  }
}

float on(float)  { return 1.0f; }
float off(float) { return 0.0f; }
// Equal-power pan: left fades out as right fades in.
float panL(float t) { return cosf(t * PI / 2); }
float panR(float t) { return sinf(t * PI / 2); }

void quiet(int ms) { playSegment(ms, LOW_HZ, LOW_HZ, off, off); }

bool okL = false, okR = false;

void setup() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  bool ioxOk = setupExpanders();
  Serial.begin(115200);
  delay(300);
  sdOk = SD_MMC.begin("/sdcard", true);

  logf("\n=== Yoto V3 stereo test (boot at %lu ms) ===\n", millis());
  logf("SD log: %s, expanders: %s\n", sdOk ? "OK" : "not available", ioxOk ? "OK" : "WRITE FAILED");
  delay(10);

  buildSineTable();
  i2s.setPins(PIN_BCLK, PIN_LRCLK, PIN_DOUT, -1, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    logf("I2S start FAILED\n");
    saveLog();
    return;
  }
  quiet(20);

  okL = ampConfigure(AMP_L, I2SCTRL_LEFT, "left");
  okR = ampConfigure(AMP_R, I2SCTRL_RIGHT, "right");
  if (okL) okL = ampStart(AMP_L);
  if (okR) okR = ampStart(AMP_R);

  logf("Pattern: A low tone left only, B high tone right only, C sweep left to right, D low left + high right\n");
  saveLog();
}

int cycles = 0;

void loop() {
  if (!okL && !okR) { delay(1000); return; }
  bool log = cycles < 2;

  if (log) logf("Cycle %d: A left channel only\n", cycles + 1);
  playSegment(1500, LOW_HZ, HIGH_HZ, on, off);
  quiet(1000);

  if (log) logf("Cycle %d: B right channel only\n", cycles + 1);
  playSegment(1500, LOW_HZ, HIGH_HZ, off, on);
  quiet(1000);

  if (log) logf("Cycle %d: C sweep left to right\n", cycles + 1);
  playSegment(4000, 600, 600, panL, panR);   // same pitch both sides so only position changes
  quiet(1000);

  if (log) logf("Cycle %d: D low left + high right\n", cycles + 1);
  playSegment(2000, LOW_HZ, HIGH_HZ, on, on);
  quiet(3000);

  if (log) { saveLog(); cycles++; }
}
