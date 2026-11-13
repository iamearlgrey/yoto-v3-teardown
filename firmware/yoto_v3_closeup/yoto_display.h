/*
  yoto_display.h: the 16x16 RGB LED matrix, included by yoto_base.h
  -----------------------------------------------------------------
  Four Holtek HT16D35A chips (3-wire SPI), chip selects on IO expander 0x21
  port 2 bits 0-3 (IOX.2.0-2.3, active low). Data on GPIO22, clock on GPIO23.
  GPIO26 (MISO in Yoto's config) is left alone: it's also the left knob.

  Found Nov 12: the matrix is full colour (an R, G and B LED per pixel). Each chip
  lights a band 16 pixels wide and 4 pixels tall: 64 pixels x 3 = 192 LEDs.

  HT16D35A datasheet (v1.20): MSB first, data latched on the rising clock edge,
  up to ~4 MHz. Commands used:
    CCh reset | 41h COM enable (1 byte) | 42h ROW enable (4 bytes, ROW27..ROW0)
    43h direct pins (0 = all ROW) | 31h 0 = gray mode (64 levels per dot)
    32h 07h = COM0-7 | 33h blink | 34h 00h = single chip | 38h mode control
    35h bit1 oscillator, bit0 display | 36h current ratio (0 = 100% ... 7 = 9/16)
    37h global brightness 0-64 | 80h addr data... write RAM, auto-increment
  The datasheet gives gray RAM as COM x 20h + ROW, ROW 0-27. On this board the
  "unused" bytes 1Ch-1Fh of each 20h block light LEDs too (Raw fill lights
  everything, All on with 28 per block left a stair-step of missing LEDs), and
  addresses 00h-1Bh light nothing. So the tools here work on raw RAM addresses
  00h-FBh, and the map is address -> pixel and colour.

  Starts dim on purpose: lowest current ratio (9/16), global brightness 16/64.
*/
#pragma once

#include <SPI.h>

namespace yoto {
namespace display {

const int PIN_MOSI = 22;
const int PIN_SCLK = 23;
const int CHIPS = 4;
const int RAM_BYTES = 0xFC;        // 00h-FBh

SPIClass spi(HSPI);
bool ok = false;
uint8_t ram[CHIPS][RAM_BYTES];
uint8_t brightness = 16;           // global, 0-64
uint8_t currentRatio = 7;          // 7 = 9/16 of the board's maximum
uint8_t p2Out = 0x5F;              // expander port 2 outputs (stock value; CS bits 0-3 high)
SemaphoreHandle_t lock = nullptr;

void select(int chip) {            // chip -1 = none
  uint8_t v = (p2Out & 0xF0) | 0x0F;
  if (chip >= 0) v &= ~(1 << chip);
  write8(IOX_B, 0x02, v);
}

// The datasheet's timing diagrams show ~2 us between bytes, so bytes go out one at
// a time with a short gap rather than as one continuous burst.
void send(int chip, const uint8_t *data, size_t len) {
  select(chip);
  spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  for (size_t i = 0; i < len; i++) {
    spi.transfer(data[i]);
    delayMicroseconds(3);
  }
  spi.endTransaction();
  select(-1);
}

void cmd(int chip, uint8_t c) { send(chip, &c, 1); }
void cmd(int chip, uint8_t c, uint8_t d) { uint8_t b[2] = {c, d}; send(chip, b, 2); }

void flushChip(int chip) {
  static uint8_t buf[2 + RAM_BYTES];
  buf[0] = 0x80;
  buf[1] = 0x00;
  memcpy(buf + 2, ram[chip], RAM_BYTES);
  send(chip, buf, sizeof(buf));
}

void flush() {
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int c = 0; c < CHIPS; c++) flushChip(c);
  xSemaphoreGive(lock);
}

void clear() { memset(ram, 0, sizeof(ram)); }

// One LED by chip and raw RAM address (00h-FBh). Level 0-63.
void led(int chip, int addr, uint8_t level) {
  if (chip < 0 || chip >= CHIPS || addr < 0 || addr >= RAM_BYTES) return;
  ram[chip][addr] = level > 63 ? 63 : level;
}

void setBrightness(uint8_t b) {
  brightness = b > 64 ? 64 : b;
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int c = 0; c < CHIPS; c++) cmd(c, 0x37, brightness);
  xSemaphoreGive(lock);
}

void buildGamma();

bool begin() {
  buildGamma();
  spi.begin(PIN_SCLK, -1, PIN_MOSI, -1);
  lock = xSemaphoreCreateMutex();
  delay(2);
  clear();
  for (int c = 0; c < CHIPS; c++) {
    cmd(c, 0xCC);                    // reset
    delay(1);
    flushChip(c);                    // blank RAM before the display comes on
    cmd(c, 0x41, 0xFF);              // COM0-7 on
    uint8_t rows[5] = {0x42, 0xFF, 0xFF, 0xFF, 0x0F};   // ROW0-27 on
    send(c, rows, 5);
    cmd(c, 0x43, 0x00);              // ROW24-27 as matrix rows, not direct outputs
    cmd(c, 0x31, 0x00);              // gray mode: 64 levels per dot
    cmd(c, 0x32, 0x07);              // scan COM0-7
    cmd(c, 0x33, 0x00);              // no blinking
    cmd(c, 0x34, 0x00);              // single chip, own oscillator
    cmd(c, 0x38, 0x00);              // no fade, masking or blink groups
    cmd(c, 0x36, currentRatio);      // LED current, lowest step
    cmd(c, 0x37, brightness);        // global brightness
    cmd(c, 0x35, 0x02);              // oscillator on
    delay(1);
    cmd(c, 0x35, 0x03);              // display on
  }
  ok = true;
  log("Display: 4 chips set up (gray mode, current 9/16, brightness %d/64).\n", brightness);
  return true;
}

// Switch the display off before power off (keeps RAM).
void sleep() {
  if (!ok) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int c = 0; c < CHIPS; c++) cmd(c, 0x35, 0x00);
  xSemaphoreGive(lock);
}

// ---- Pixels ----
// Mapped by clicking through chip 0 (Nov 12): LEDs sit in runs of 24 addresses
// (8 pixels x R, G, B), one run every 28 addresses (1Ch, 38h, 54h ... E0h). The
// last 4 of each 28 are unused, and 00h-1Bh is dark. Within a chip's band of 4
// rows: run 1 = row 0 right half, 2 = row 1 left, 3 = row 1 right, 4 = row 2 left,
// 5 = row 2 right, 6 = row 3 left, 7 = row 3 right, 8 = row 0 left.
// Chip c covers rows 4c to 4c+3 (assumed from the whole-chip test; check with the
// test pattern).
uint8_t gamma63[256];

void buildGamma() {
  for (int i = 0; i < 256; i++) {
    float v = powf(i / 255.0f, 2.2f) * 63.0f;
    gamma63[i] = (i && v < 1.0f) ? 1 : (uint8_t)(v + 0.5f);
  }
}

inline int pixelAddr(int x, int y) {   // address of the pixel's red LED
  int j = (y & 3) * 2 + (x >> 3);
  return 28 * (j ? j : 8) + (x & 7) * 3;
}

// Colour 0-255 per channel (gamma-corrected to the chip's 64 levels).
void pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || x > 15 || y < 0 || y > 15) return;
  uint8_t *p = &ram[y >> 2][pixelAddr(x, y)];
  p[0] = gamma63[r];
  p[1] = gamma63[g];
  p[2] = gamma63[b];
}

// A whole frame: 256 pixels, row by row from the top left, as "rrggbb" hex each.
bool frameFromHex(const String &hex) {
  if (hex.length() < 256 * 6) return false;
  clear();
  for (int i = 0; i < 256; i++) {
    char buf[7];
    memcpy(buf, hex.c_str() + i * 6, 6);
    buf[6] = 0;
    long v = strtol(buf, nullptr, 16);
    pixel(i & 15, i >> 4, (v >> 16) & 255, (v >> 8) & 255, v & 255);
  }
  flush();
  return true;
}

// Corners red / green / blue / white, a rainbow across, brightness rising down,
// and a 1-pixel white border on the top band only, so swapped chips are obvious.
void showPattern() {
  clear();
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      float h = x / 16.0f * 6.0f;
      int i = (int)h;
      float f = h - i;
      float r = 0, g = 0, b = 0;
      switch (i) {
        case 0: r = 1; g = f; break;
        case 1: r = 1 - f; g = 1; break;
        case 2: g = 1; b = f; break;
        case 3: g = 1 - f; b = 1; break;
        case 4: r = f; b = 1; break;
        default: r = 1; b = 1 - f; break;
      }
      float l = 0.15f + 0.85f * y / 15.0f;
      pixel(x, y, r * l * 255, g * l * 255, b * l * 255);
    }
  for (int x = 0; x < 16; x++) pixel(x, 0, 255, 255, 255);
  pixel(0, 0, 255, 0, 0);
  pixel(15, 0, 0, 255, 0);
  pixel(0, 15, 0, 0, 255);
  pixel(15, 15, 255, 255, 255);
  flush();
  log("Display test pattern: top row white, corners red (top left), green (top right), blue (bottom left), white (bottom right); rainbow left to right, dim at top to bright at bottom.\n");
}

// ---- Test tools (web page). A "line" here is one 20h block of addresses. ----
void showChip(int chip) {
  clear();
  for (int a = 0; a < RAM_BYTES; a++) led(chip, a, 63);
  flush();
  log("Display test: chip %d, every address on.\n", chip);
}

void showAddr(int chip, int addr) {
  clear();
  led(chip, addr, 63);
  flush();
  log("Display test: chip %d, address %02Xh.\n", chip, addr);
}

void showBlock(int chip, int block) {   // addresses block x 20h .. +1Fh
  clear();
  for (int a = block * 0x20; a < block * 0x20 + 0x20; a++) led(chip, a, 63);
  flush();
  log("Display test: chip %d, block %d (%02Xh-%02Xh).\n", chip, block, block * 0x20,
      min(block * 0x20 + 0x1F, RAM_BYTES - 1));
}

void showRamp(int chip) {               // brightness rises with the address
  clear();
  for (int a = 0; a < RAM_BYTES; a++) led(chip, a, (uint8_t)((a % 0x20) * 63 / 31));
  flush();
  log("Display test: chip %d brightness ramp.\n", chip);
}

void showAll(bool on) {
  if (on) memset(ram, 63, sizeof(ram));
  else clear();
  flush();
  log("Display test: all %s.\n", on ? "on" : "off");
}

// ---- Click-mapping: light one address at a time, the web page records where it is ----
// Result per address of the chip being mapped: -2 = not done, -1 = nothing lit,
// otherwise x | y << 4 | colour << 8 (colour 0 red, 1 green, 2 blue).
int mapChip = -1, mapAddr = 0;
int16_t mapResult[RAM_BYTES];

void mapShowCurrent() {
  clear();
  if (mapChip >= 0 && mapAddr < RAM_BYTES) led(mapChip, mapAddr, 63);
  flush();
}

void mapSave() {
  if (mapChip < 0) return;
  char path[32];
  snprintf(path, sizeof(path), "/display_map_chip%d.txt", mapChip);
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return;
  f.printf("# HT16D35A chip %d: RAM address (hex) -> x y colour (r/g/b), or 'none'\n", mapChip);
  for (int a = 0; a < RAM_BYTES; a++) {
    int16_t v = mapResult[a];
    if (v == -2) continue;
    if (v == -1) f.printf("%02X none\n", a);
    else f.printf("%02X %d %d %c\n", a, v & 15, (v >> 4) & 15, "rgb"[(v >> 8) & 3]);
  }
  f.close();
}

void mapStart(int chip) {
  mapChip = constrain(chip, 0, CHIPS - 1);
  for (int a = 0; a < RAM_BYTES; a++) mapResult[a] = a < 0x1C ? -1 : -2;   // 00h-1Bh known dark
  mapAddr = 0x1C;
  log("Mapping chip %d from address 1Ch (00h-1Bh are dark): click where each dot lights up.\n", mapChip);
  mapShowCurrent();
}

void mapAdvance() {
  mapAddr++;
  if (mapAddr >= RAM_BYTES) {
    log("Mapping chip %d finished. Saved to /display_map_chip%d.txt\n", mapChip, mapChip);
    mapSave();
  }
  mapShowCurrent();
}

// x, y, colour 0-2, or colour -1 for "nothing lit"
void mapRecord(int x, int y, int colour) {
  if (mapChip < 0 || mapAddr >= RAM_BYTES) return;
  mapResult[mapAddr] = colour < 0 ? -1 : (int16_t)((x & 15) | ((y & 15) << 4) | (colour << 8));
  if (mapAddr % 8 == 0) mapSave();
  mapAdvance();
}

void mapSkipBlock() {                   // the rest of this block of 20h is dark
  if (mapChip < 0 || mapAddr >= RAM_BYTES) return;
  int end = min((mapAddr / 0x20 + 1) * 0x20, RAM_BYTES);
  for (int a = mapAddr; a < end; a++) mapResult[a] = -1;
  mapAddr = end - 1;
  mapSave();
  mapAdvance();
}

void mapBack() {
  if (mapChip < 0 || mapAddr == 0) return;
  mapAddr--;
  mapResult[mapAddr] = -2;
  mapShowCurrent();
}

String mapState() {
  String s = "{\"chip\":" + String(mapChip) + ",\"addr\":" + String(mapAddr) + ",\"total\":" + String(RAM_BYTES) +
             ",\"found\":[";
  bool first = true;
  if (mapChip >= 0)
    for (int a = 0; a < RAM_BYTES; a++) {
      int16_t v = mapResult[a];
      if (v < 0) continue;
      if (!first) s += ",";
      first = false;
      s += "[" + String(v & 15) + "," + String((v >> 4) & 15) + "," + String((v >> 8) & 3) + "]";
    }
  return s + "]}";
}

}  // namespace display
}  // namespace yoto
