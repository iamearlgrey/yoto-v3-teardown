/*
  Yoto Player V3 (V3-E board) speaker amp test
  -------------------------------------------
  Wakes the two AW88194 smart amps and plays a quiet 480 Hz beep.

  Board setting in Arduino IDE: "ESP32 Wrover Module" (ESP32 Arduino core 3.x)
  Results go to the Serial Monitor (115200) AND to amp_test_log.txt on the SD card,
  so the test can run with the programmer disconnected.

  Pin and chip info comes from Yoto's own V3-E board config (in the stock
  firmware) plus the Awinic AW88194 datasheet. Tested Nov 1, 2026: test tone from both
  original speakers.

  Safety: volume starts at -24 dB with a 30% sine. Raise VOLUME_DB_DOWN later.
*/

#include <Wire.h>
#include <ESP_I2S.h>
#include <SD_MMC.h>
#include <stdarg.h>

// ---- Pins (V3-E config) ----
const int PIN_SDA   = 21;
const int PIN_SCL   = 25;
const int PIN_MCLK  = 0;
const int PIN_BCLK  = 5;
const int PIN_LRCLK = 18;
const int PIN_DOUT  = 19;
// SD card (1-bit mode) uses the ESP32's fixed SDMMC pins: CLK 14, CMD 15, D0 2

// ---- I2C addresses ----
const uint8_t IOX_A = 0x20;   // ports p0, p1
const uint8_t IOX_B = 0x21;   // ports p2, p3  (IOX.2.4 = amp control lives here)
const uint8_t AMP_L = 0x34;
const uint8_t AMP_R = 0x37;

// ---- Audio ----
const int SAMPLE_RATE    = 48000;
const int TONE_HZ        = 480;    // divides 48 kHz evenly, so the loop is click-free
const float TONE_LEVEL   = 0.30;   // fraction of full scale
const int VOLUME_DB_DOWN = 24;     // amp attenuation in dB (multiples of 6 are simplest)

I2SClass i2s;

// ---------- Logging: Serial + SD card ----------
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
  File f = SD_MMC.open("/amp_test_log.txt", FILE_APPEND);
  if (f) {
    f.print(logBuf);
    f.close();
    logBuf = "";
  }
}

// ---------- I2C helpers ----------
bool write8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// AW88194 registers are 16-bit, high byte first
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

void scanI2C(const char *label) {
  logf("\nI2C scan (%s):", label);
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) logf(" 0x%02X", a);
  }
  logf("\n");
}

void printStatus(uint8_t amp) {
  int32_t s = read16(amp, 0x01);
  if (s < 0) { logf("  0x%02X status: read failed\n", amp); return; }
  logf("  0x%02X status 0x%04X  PLL locked:%d  clocks stable:%d  no ref clock:%d  switching:%d  boost started:%d  undervoltage:%d\n",
       amp, (unsigned)s, (int)(s & 1), (int)((s >> 4) & 1), (int)((s >> 5) & 1),
       (int)((s >> 8) & 1), (int)((s >> 9) & 1), (int)((s >> 14) & 1));
}

bool waitBit(uint8_t amp, int bit, int timeoutMs) {
  for (int t = 0; t < timeoutMs; t += 5) {
    int32_t s = read16(amp, 0x01);
    if (s >= 0 && ((s >> bit) & 1)) return true;
    delay(5);
  }
  return false;
}

// ---------- Step 1: IO expanders (copy of Yoto's stock startup values) ----------
bool setupExpanders() {
  // Output values first, then directions (0 = output), so pins don't glitch.
  bool ok = true;
  ok &= write8(IOX_A, 0x02, 0xFF);  // p0 out
  ok &= write8(IOX_A, 0x03, 0xFF);  // p1 out
  ok &= write8(IOX_B, 0x02, 0x5F);  // p2 out: bit4 (IOX.2.4, amp control) = 1
  ok &= write8(IOX_B, 0x03, 0xDF);  // p3 out: bit1 (IOX.3.1, power hold) = 1
  ok &= write8(IOX_A, 0x06, 0xFE);  // p0 dir
  ok &= write8(IOX_A, 0x07, 0xFF);  // p1 dir
  ok &= write8(IOX_B, 0x06, 0x00);  // p2 dir: all outputs
  ok &= write8(IOX_B, 0x07, 0xD4);  // p3 dir
  return ok;
}

// ---------- Step 3: identify amps, set audio format, power up to PLL lock ----------
bool ampConfigure(uint8_t amp) {
  int32_t id = read16(amp, 0x00);
  if (id < 0) { logf("  0x%02X: no answer\n", amp); return false; }
  logf("  0x%02X chip ID 0x%04X %s\n", amp, (unsigned)id, id == 0x1806 ? "(AW88194, as expected)" : "(UNEXPECTED)");
  if (id != 0x1806) return false;

  // I2SCTRL 0x05: mono (L+R)/2, standard I2S, 16-bit, BCLK = 32fs, 48 kHz
  write16(amp, 0x05, 0x0C08);

  // SYSCTRL 0x04: I2S on (bit6), power-down off (bit0), amp still off (bit1)
  int32_t s = read16(amp, 0x04);
  if (s < 0) return false;
  s |= (1 << 6);
  s &= ~1;
  write16(amp, 0x04, (uint16_t)s);

  bool locked = waitBit(amp, 0, 300);
  logf("  0x%02X PLL lock: %s\n", amp, locked ? "yes" : "NO");
  printStatus(amp);
  return locked;
}

// ---------- Step 4: amp on, volume, unmute ----------
bool ampStart(uint8_t amp) {
  int32_t s = read16(amp, 0x04);
  if (s < 0) return false;
  s &= ~(1 << 1);                       // clear AMPPD
  write16(amp, 0x04, (uint16_t)s);
  bool sw = waitBit(amp, 8, 300);
  logf("  0x%02X amplifier switching: %s\n", amp, sw ? "yes" : "NO");

  // Volume 0x0F, upper byte: [7:4] x 6 dB, [3:0] x 0.5 dB
  int32_t v = read16(amp, 0x0F);
  if (v >= 0) {
    uint8_t six = VOLUME_DB_DOWN / 6;
    uint8_t half = ((VOLUME_DB_DOWN % 6) * 2) & 0x0F;
    v = (v & 0x00FF) | ((uint16_t)((six << 4) | half) << 8);
    write16(amp, 0x0F, (uint16_t)v);
  }

  // PWMCTRL 0x08: release hardware mute (bit0)
  int32_t p = read16(amp, 0x08);
  if (p >= 0) write16(amp, 0x08, (uint16_t)(p & ~1));

  printStatus(amp);
  return sw;
}

// ---------- Tone ----------
const int FRAMES = SAMPLE_RATE / TONE_HZ * 4;   // 4 whole cycles
int16_t toneBuf[FRAMES * 2];
int16_t silenceBuf[256 * 2];

void playFor(const int16_t *buf, size_t frames, int ms) {
  unsigned long end = millis() + ms;
  while (millis() < end) i2s.write((const uint8_t *)buf, frames * 2 * sizeof(int16_t));
}

void setup() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  bool ioxOk = setupExpanders();    // first, so the power hold is set early

  Serial.begin(115200);
  delay(300);

  sdOk = SD_MMC.begin("/sdcard", true);   // 1-bit mode
  logf("\n=== Yoto V3-E amp test (boot at %lu ms) ===\n", millis());
  logf("SD card log: %s\n", sdOk ? "OK" : "not available");
  logf("Step 1: expanders configured: %s\n", ioxOk ? "OK" : "WRITE FAILED");

  delay(10);                        // datasheet: >5 ms after reset release
  scanI2C("after expander setup; looking for 0x34 and 0x37");
  saveLog();

  // Step 2: start I2S clocks before configuring the amps (they lock to BCLK)
  i2s.setPins(PIN_BCLK, PIN_LRCLK, PIN_DOUT, -1, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    logf("Step 2: I2S start FAILED\n");
    saveLog();
    return;
  }
  memset(silenceBuf, 0, sizeof(silenceBuf));
  playFor(silenceBuf, 256, 20);     // keep clocks running >5 ms
  logf("Step 2: I2S clocks running\n");

  logf("Step 3: configure amps\n");
  bool l = ampConfigure(AMP_L);
  bool r = ampConfigure(AMP_R);

  logf("Step 4: start amps\n");
  if (l) ampStart(AMP_L);
  if (r) ampStart(AMP_R);

  for (int i = 0; i < FRAMES; i++) {
    int16_t s = (int16_t)(sin(2.0 * PI * TONE_HZ * i / SAMPLE_RATE) * TONE_LEVEL * 32767);
    toneBuf[i * 2] = s;
    toneBuf[i * 2 + 1] = s;
  }
  logf("Step 5: beeping every 3 seconds. Listen!\n");
  saveLog();
}

int loops = 0;

void loop() {
  playFor(toneBuf, FRAMES, 1000);       // 1 s beep
  playFor(silenceBuf, 256, 2000);       // 2 s quiet
  if (loops < 3) {                      // log status for the first few beeps only
    logf("After beep %d:\n", loops + 1);
    printStatus(AMP_L);
    printStatus(AMP_R);
    saveLog();
    loops++;
  }
}
