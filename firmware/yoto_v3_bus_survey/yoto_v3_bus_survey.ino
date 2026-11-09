/*
  Yoto Player V3 bus survey (read-only)
  -------------------------------------
  Answers several open hardware questions in one run:
    - Which fuel gauge answers: 0x62 (CW2015) or 0x64 (CW2215B)?
    - Is 0x18 the LIS2DH12? (WHO_AM_I register 0x0F should read 0x33)
    - What are the unknown addresses (0x75 etc.)? Dumps 16 registers twice,
      2 seconds apart, and marks which ones changed.
    - Live readings of GPIO36 (D300 light sensor) and GPIO39 (temperature?).

  Board: "ESP32 Wrover Module", esp32 core 3.x. Serial Monitor at 115200.
  The survey part is also saved to bus_survey_log.txt on the SD card.

  Only reads from chips, except the expander values from the amp test
  (needed to keep the power hold on when running from battery).
*/

#include <Wire.h>
#include <SD_MMC.h>
#include <stdarg.h>

const int PIN_SDA   = 21;
const int PIN_SCL   = 25;
const int PIN_LIGHT = 36;   // D300 light sensor (confirmed by continuity)
const int PIN_TEMP  = 39;   // temperature sensor, location unknown

const uint8_t IOX_A = 0x20;
const uint8_t IOX_B = 0x21;

// ---------- Logging: Serial + SD ----------
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
  File f = SD_MMC.open("/bus_survey_log.txt", FILE_APPEND);
  if (f) { f.print(logBuf); f.close(); logBuf = ""; }
}

// ---------- I2C helpers ----------
bool write8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

int read8(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)addr, 1) != 1) return -1;
  return Wire.read();
}

bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Same stock values as the amp test: keeps power hold (IOX.3.1) on.
bool setupExpanders() {
  bool ok = true;
  ok &= write8(IOX_A, 0x02, 0xFF);
  ok &= write8(IOX_A, 0x03, 0xFF);
  ok &= write8(IOX_B, 0x02, 0x5F);
  ok &= write8(IOX_B, 0x03, 0xDF);
  ok &= write8(IOX_A, 0x06, 0xFE);
  ok &= write8(IOX_A, 0x07, 0xFF);
  ok &= write8(IOX_B, 0x06, 0x00);
  ok &= write8(IOX_B, 0x07, 0xD4);
  return ok;
}

// ---------- Survey ----------
uint8_t found[40];
int nFound = 0;

void scanBus() {
  logf("\nI2C scan:");
  nFound = 0;
  for (uint8_t a = 1; a < 127; a++) {
    if (present(a)) {
      logf(" 0x%02X", a);
      if (nFound < 40) found[nFound++] = a;
    }
  }
  logf("\n");
}

void checkGauges() {
  logf("\nFuel gauge:\n");
  if (present(0x62)) {
    int ver  = read8(0x62, 0x00);
    int hi   = read8(0x62, 0x02);
    int lo   = read8(0x62, 0x03);
    int soc  = read8(0x62, 0x04);
    int mode = read8(0x62, 0x0A);
    logf("  0x62 answers (CW2015 address). Version reg 0x%02X, mode reg 0x%02X\n", ver & 0xFF, mode & 0xFF);
    if (hi >= 0 && lo >= 0) {
      float volts = (((hi << 8) | lo) & 0x3FFF) * 0.000305;
      logf("  0x62 cell voltage reads %.3f V, charge %d%%\n", volts, soc);
    }
  } else {
    logf("  0x62 silent\n");
  }
  if (present(0x64)) {
    logf("  0x64 answers (CW2215B address). Reg 0x00 = 0x%02X\n", read8(0x64, 0x00) & 0xFF);
  } else {
    logf("  0x64 silent\n");
  }
}

void checkAccel() {
  logf("\nAccelerometer:\n");
  if (!present(0x18)) { logf("  0x18 silent\n"); return; }
  int who = read8(0x18, 0x0F);
  logf("  0x18 WHO_AM_I = 0x%02X %s\n", who & 0xFF,
       who == 0x33 ? "(LIS2DH12, as expected)" : "(not 0x33, so not a LIS2DH12)");
}

bool skipDump(uint8_t a) {
  // expanders and the amps (amps use 16-bit registers) are already known
  return a == IOX_A || a == IOX_B || a == 0x34 || a == 0x37;
}

int snapA[40][16], snapB[40][16];

void dumpUnknowns() {
  logf("\nRegister dumps (regs 0x00-0x0F, read twice 2 s apart):\n");
  for (int i = 0; i < nFound; i++)
    if (!skipDump(found[i]))
      for (int r = 0; r < 16; r++) snapA[i][r] = read8(found[i], r);
  delay(2000);
  for (int i = 0; i < nFound; i++)
    if (!skipDump(found[i]))
      for (int r = 0; r < 16; r++) snapB[i][r] = read8(found[i], r);

  for (int i = 0; i < nFound; i++) {
    if (skipDump(found[i])) continue;
    logf("  0x%02X:", found[i]);
    for (int r = 0; r < 16; r++) {
      if (snapA[i][r] < 0) logf(" --"); else logf(" %02X", snapA[i][r]);
    }
    logf("\n        changed:");
    bool any = false;
    for (int r = 0; r < 16; r++) {
      if (snapA[i][r] != snapB[i][r]) {
        logf(" reg0x%02X(%02X->%02X)", r, snapA[i][r] & 0xFF, snapB[i][r] & 0xFF);
        any = true;
      }
    }
    if (!any) logf(" none");
    logf("\n");
  }
}

void setup() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  bool ioxOk = setupExpanders();   // first, so the power hold is set early

  Serial.begin(115200);
  delay(300);
  sdOk = SD_MMC.begin("/sdcard", true);   // 1-bit mode

  logf("\n=== Yoto V3 bus survey (boot at %lu ms) ===\n", millis());
  logf("SD card log: %s\n", sdOk ? "OK" : "not available");
  logf("Expanders: %s\n", ioxOk ? "OK" : "WRITE FAILED");

  scanBus();
  checkGauges();
  checkAccel();
  dumpUnknowns();

  logf("\nLive sensor readings follow (Serial only).\n");
  logf("Cover D300 with a finger, then shine a light on it.\n");
  logf("Then warm parts near the battery connector with a fingertip and watch GPIO39.\n\n");
  saveLog();
}

void loop() {
  int light = analogReadMilliVolts(PIN_LIGHT);
  int temp  = analogReadMilliVolts(PIN_TEMP);
  Serial.printf("GPIO36 light: %4d mV    GPIO39 temp: %4d mV\n", light, temp);
  delay(500);
}
