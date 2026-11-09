/*
  Yoto Player V3 battery watch
  ----------------------------
  Wakes the CW2015 fuel gauge (0x62) and prints battery voltage and charge
  every 2 seconds, alongside the SGM41513 charger's status (0x1A).

  Board: "ESP32 Wrover Module", esp32 core 3.x. Serial Monitor at 115200.

  The only writes: the stock expander values (power hold), and the gauge's
  mode register 0x0A = 0x00 (sleep -> normal). The charger is only read.

  Voltage is accurate straight away. The charge percentage starts as a rough
  guess, because the gauge hasn't been given this battery's profile.

  To stop: unplug USB-C and the battery, or tap EN. Nothing can be harmed.
*/

#include <Wire.h>

const int PIN_SDA = 21;
const int PIN_SCL = 25;

const uint8_t IOX_A   = 0x20;
const uint8_t IOX_B   = 0x21;
const uint8_t GAUGE   = 0x62;   // CW2015
const uint8_t CHARGER = 0x1A;   // SGM41513

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

bool setupExpanders() {
  bool ok = true;
  ok &= write8(IOX_A, 0x02, 0xFF);
  ok &= write8(IOX_A, 0x03, 0xFF);
  ok &= write8(IOX_B, 0x02, 0x5F);
  ok &= write8(IOX_B, 0x03, 0xDF);   // IOX.3.1 power hold
  ok &= write8(IOX_A, 0x06, 0xFE);
  ok &= write8(IOX_A, 0x07, 0xFF);
  ok &= write8(IOX_B, 0x06, 0x00);
  ok &= write8(IOX_B, 0x07, 0xD4);
  return ok;
}

const char *inputName(int v) {
  switch ((v >> 5) & 7) {
    case 0: return "no USB input";
    case 1: return "USB host, 500 mA";
    case 2: return "adapter, 2.4 A";
    case 7: return "OTG";
    default: return "other";
  }
}

const char *chargeName(int v) {
  switch ((v >> 3) & 3) {
    case 0: return "not charging";
    case 1: return "pre-charge";
    case 2: return "fast charging";
    default: return "charge done";
  }
}

const char *ntcName(int f) {
  switch (f & 7) {
    case 0: return "normal";
    case 2: return "warm";
    case 3: return "cool";
    case 5: return "cold";
    case 6: return "hot";
    default: return "?";
  }
}

void setup() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  bool ioxOk = setupExpanders();
  Serial.begin(115200);
  delay(300);

  Serial.printf("\n=== Yoto V3 battery watch ===\nExpanders: %s\n", ioxOk ? "OK" : "WRITE FAILED");

  int mode = read8(GAUGE, 0x0A);
  Serial.printf("Gauge mode before: 0x%02X %s\n", mode & 0xFF, mode == 0xC0 ? "(asleep)" : "");
  bool woke = write8(GAUGE, 0x0A, 0x00);
  delay(1000);   // give it a moment to take its first reading
  mode = read8(GAUGE, 0x0A);
  Serial.printf("Wake write: %s, mode now 0x%02X %s\n\n", woke ? "OK" : "FAILED",
                mode & 0xFF, mode == 0x00 ? "(awake)" : "");
}

void loop() {
  int hi  = read8(GAUGE, 0x02);
  int lo  = read8(GAUGE, 0x03);
  int soc = read8(GAUGE, 0x04);
  float volts = (hi >= 0 && lo >= 0) ? (((hi << 8) | lo) & 0x3FFF) * 0.000305 : -1;

  int st = read8(CHARGER, 0x08);
  read8(CHARGER, 0x09);              // first read returns old faults
  int ft = read8(CHARGER, 0x09);     // second read is the current state

  Serial.printf("Battery %.3f V  charge %3d%%  |  %s, %s, battery temp %s\n",
                volts, soc, inputName(st), chargeName(st), ntcName(ft));
  delay(2000);
}
