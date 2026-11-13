/*
  yoto_light.h: the night light ring (AW2028H, I2C 0x65), included by yoto_base.h
  ------------------------------------------------------------------------------
  AW2028H datasheet: chip ID 0xB1 in register 0x00 (writing 0x55 there resets it),
  GCR 0x01 bit 0 = chip enable, IMAX 0x03 (00 = 9 mA, 01 = 18 mA, 10 = 37 mA,
  11 = 75 mA), LCFG1-3 0x04-0x06 (0 = manual mode), LEDEN 0x07 (bits 0-2),
  LEDCTR 0x08 (bit 3 sync RGB, bits 1:0 = 11 linear dimming), ILED1-3 0x10-0x12
  (current, IMAX x n/255), PWM1-3 0x1C-0x1E (duty).
  Used in independent mode: ILED sets overall brightness, PWM sets the colour.
  Current is kept at the chip default (18 mA max) until the LED board is known.

  A small task fades and breathes the light ~50 times a second, so animations keep
  running while the main loop is busy (for example joining WiFi).
*/
#pragma once

namespace yoto {
namespace light {

const uint8_t ADDR = 0x65;
uint8_t CHANNEL[3] = {0, 1, 2};   // red, green, blue -> AW2028H LED1-3. Check with the channel test.

bool ok = false;
uint8_t level = 0x80;             // overall brightness (ILED), 0-255 of the 18 mA maximum
SemaphoreHandle_t lock = nullptr;

// What the light is doing. All colours 0-255 per channel, before gamma.
enum Mode { STEADY, FADE, BREATHE, TEST, PULSE };
Mode mode = STEADY;
// A one-off pulse remembers what the light was doing, and goes back to it after.
Mode savedMode = STEADY;
uint8_t savedTo[3] = {0, 0, 0}, pulseColour[3] = {0, 0, 0};
uint32_t savedSpan = 1;
uint8_t from[3] = {0, 0, 0}, to[3] = {0, 0, 0}, now[3] = {0, 0, 0};
uint32_t startMs = 0, spanMs = 1;
uint8_t written[3] = {1, 1, 1};   // forces the first write
volatile uint8_t pendingLevel = 0;

// Colour balance per channel, 0-100 %. Green LEDs look much brighter than red at the
// same power, so mixes lean green (yellow came out lime, Nov 12). Tuned from the web
// page and saved to /light.txt on the SD card. Defaults are Zack's calibration
// by eye on his V3 (Nov 12): red 100 %, green 33 %, blue 27 %.
uint8_t balance[3] = {100, 33, 27};
uint8_t gammaTable[256];

void buildGamma() {
  for (int i = 0; i < 256; i++) {
    float v = powf(i / 255.0f, 2.2f) * 255.0f;      // standard 2.2 curve, like a screen
    gammaTable[i] = (i && v < 1.0f) ? 1 : (uint8_t)(v + 0.5f);
  }
}

inline uint8_t gamma8(uint8_t v, int ch) {
  if (!v) return 0;
  int d = (gammaTable[v] * balance[ch] + 50) / 100;
  return d < 1 ? 1 : d;
}

void writeOut() {
  for (int i = 0; i < 3; i++) {
    uint8_t g = gamma8(now[i], i);
    if (g != written[i]) {
      write8(ADDR, 0x1C + CHANNEL[i], g);
      written[i] = g;
    }
  }
}

void setLevelNow(uint8_t l) {
  for (int i = 0; i < 3; i++) write8(ADDR, 0x10 + i, l);
}

void task(void *) {
  int testStep = -1;
  for (;;) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (pendingLevel) { level = pendingLevel; setLevelNow(level); pendingLevel = 0; }
    uint32_t t = millis() - startMs;
    switch (mode) {
      case STEADY:
        memcpy(now, to, 3);
        break;
      case FADE: {
        float f = min(1.0f, (float)t / spanMs);
        for (int i = 0; i < 3; i++) now[i] = from[i] + (int)((to[i] - from[i]) * f);
        if (f >= 1.0f) mode = STEADY;
        break;
      }
      case BREATHE: {
        float f = (1.0f - cosf(2.0f * PI * (float)(t % spanMs) / spanMs)) * 0.5f;
        for (int i = 0; i < 3; i++) now[i] = (uint8_t)(to[i] * (0.08f + 0.92f * f));
        break;
      }
      case PULSE: {                     // 0.4 s up, 0.6 s down, then back to before
        float f = t < 400 ? t / 400.0f : 1.0f - (t - 400) / 600.0f;
        if (t >= 1000) {
          memcpy(to, savedTo, 3);
          spanMs = savedSpan;
          startMs = millis();
          mode = savedMode == FADE ? STEADY : savedMode;
          memset(now, 0, 3);
          break;
        }
        for (int i = 0; i < 3; i++) now[i] = (uint8_t)(pulseColour[i] * f);
        break;
      }
      case TEST: {
        int step = t / 2000;            // 2 s per channel
        if (step != testStep) {
          testStep = step;
          if (step < 3) log("Night light test: channel %d only. What colour is it?\n", step + 1);
          else log("Night light test done.\n");
        }
        memset(now, 0, 3);
        if (step < 3) {
          for (int i = 0; i < 3; i++) if (CHANNEL[i] == step) now[i] = 255;   // drive that raw channel
        } else {
          memset(to, 0, 3);
          mode = STEADY;
          testStep = -1;
        }
        break;
      }
    }
    writeOut();
    xSemaphoreGive(lock);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setBalance(int r, int g, int b) {
  balance[0] = constrain(r, 1, 100);
  balance[1] = constrain(g, 1, 100);
  balance[2] = constrain(b, 1, 100);
  written[0] = written[1] = written[2] = 0xFE;   // rewrite with the new balance
}

void setBrightness(uint8_t l);

// /light.txt: one line "R G B" balance in percent, then optionally a brightness line.
void loadSettings() {
  File f = SD_MMC.open("/light.txt");
  if (!f) return;
  String line = f.readStringUntil('\n');
  int r, g, b;
  if (sscanf(line.c_str(), "%d %d %d", &r, &g, &b) == 3) setBalance(r, g, b);
  String lv = f.readStringUntil('\n');
  if (lv.length() && lv.toInt() > 0) setBrightness((uint8_t)constrain(lv.toInt(), 1, 255));
  f.close();
  log("Night light balance R%d G%d B%d %%, brightness %d (from /light.txt)\n", balance[0], balance[1], balance[2],
      pendingLevel ? pendingLevel : level);
}

bool saveSettings() {
  File f = SD_MMC.open("/light.txt", FILE_WRITE);
  if (!f) return false;
  f.printf("%d %d %d\n%d\n", balance[0], balance[1], balance[2], pendingLevel ? pendingLevel : level);
  f.close();
  log("Night light settings saved: balance R%d G%d B%d %%, brightness %d\n", balance[0], balance[1], balance[2],
      pendingLevel ? pendingLevel : level);
  return true;
}

bool begin() {
  buildGamma();
  write8(ADDR, 0x00, 0x55);           // reset
  delay(2);
  int id = read8(ADDR, 0x00);
  if (id != 0xB1) {
    log("Night light: chip ID 0x%02X, expected 0xB1 (AW2028H). Light disabled.\n", id & 0xFF);
    return false;
  }
  write8(ADDR, 0x01, 0x01);           // chip enable
  write8(ADDR, 0x03, 0x01);           // IMAX 18 mA (chip default)
  write8(ADDR, 0x04, 0x00);           // LED1-3 manual mode
  write8(ADDR, 0x05, 0x00);
  write8(ADDR, 0x06, 0x00);
  write8(ADDR, 0x08, 0x03);           // independent channels, linear dimming
  setLevelNow(level);
  for (int i = 0; i < 3; i++) write8(ADDR, 0x1C + i, 0);
  write8(ADDR, 0x07, 0x07);           // LED1-3 on (dark until PWM is set)
  lock = xSemaphoreCreateMutex();
  ok = true;
  xTaskCreatePinnedToCore(task, "light", 3072, nullptr, 1, nullptr, 0);
  log("Night light ready (AW2028H).\n");
  return true;
}

// Fade to a colour over ms (0 = at once).
void set(uint8_t r, uint8_t g, uint8_t b, uint32_t ms = 300) {
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(from, now, 3);
  to[0] = r; to[1] = g; to[2] = b;
  startMs = millis();
  spanMs = max((uint32_t)1, ms);
  mode = ms ? FADE : STEADY;
  xSemaphoreGive(lock);
}

void off(uint32_t ms = 300) { set(0, 0, 0, ms); }

// Breathe a colour, one breath per periodMs, until set() or off().
void breathe(uint8_t r, uint8_t g, uint8_t b, uint32_t periodMs) {
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  to[0] = r; to[1] = g; to[2] = b;
  startMs = millis();
  spanMs = max((uint32_t)200, periodMs);
  mode = BREATHE;
  xSemaphoreGive(lock);
}

void setBrightness(uint8_t l) { if (ok) pendingLevel = max((uint8_t)1, l); }

// One soft pulse of a colour, then the light carries on as before.
void pulse(uint8_t r, uint8_t g, uint8_t b) {
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (mode != TEST && mode != PULSE) {
    savedMode = mode;
    memcpy(savedTo, to, 3);
    savedSpan = spanMs;
    pulseColour[0] = r; pulseColour[1] = g; pulseColour[2] = b;
    startMs = millis();
    mode = PULSE;
  }
  xSemaphoreGive(lock);
}

void channelTest() {
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  startMs = millis();
  mode = TEST;
  xSemaphoreGive(lock);
}

String hex() {
  char b[8];
  snprintf(b, sizeof(b), "#%02x%02x%02x", to[0], to[1], to[2]);
  return String(b);
}

// Writes all channels dark right away (used just before switching off).
void blackout() {
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  memset(to, 0, 3);
  memset(now, 0, 3);
  mode = STEADY;
  writeOut();
  write8(ADDR, 0x07, 0x00);
  xSemaphoreGive(lock);
}

// Status colours
void amber(uint32_t ms = 300)        { set(255, 150, 30, ms); }
void startupGlow()                   { breathe(255, 150, 30, 2000); }
void safeModeGlow()                  { breathe(255, 20, 10, 2500); }
void hotspotGlow()                   { breathe(30, 80, 255, 2000); }
void chargingGlow()                  { breathe(40, 255, 60, 4000); }
void updateGlow()                    { breathe(30, 80, 255, 600); }

}  // namespace light
}  // namespace yoto
