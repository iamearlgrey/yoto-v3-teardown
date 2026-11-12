/*
  Yoto Player V3 close-up firmware
  --------------------------------
  Everything needed to close the case, plus a stereo WAV player:
    - yoto_base.h: WiFi, wireless updates, web page (log, SD files, battery),
      safe mode, power-button off
    - this file: both amps in true stereo, playing WAV files from the SD card

  Arduino IDE settings (Tools menu):
    Board:            ESP32 Wrover Module
    Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)
                      (needed for wireless updates; keep it for every future upload)

  First start: with no /wifi.txt on the SD card, the Yoto makes a WiFi hotspot
  called "CAL-Yoto-Setup". Join it, open http://192.168.4.1, enter your WiFi.
  After that: http://cal-yoto.local, and the "cal-yoto" network port in Arduino IDE.

  WAV files: 16-bit PCM, mono or stereo, any sample rate (resampled to 48 kHz).

  Optional sounds in the top folder of the SD card (in sd-card/ next to this sketch):
    startup.wav   plays as soon as the amps start, before WiFi
    ready.wav     plays once WiFi is up and the web page is reachable
    shutdown.wav  plays when the power button has been held long enough: let go
                  and it switches off
*/

#include "yoto_base.h"
#include <ESP_I2S.h>

using yoto::log;
using yoto::server;

// ---------- Audio hardware ----------
const int PIN_MCLK  = 0;
const int PIN_BCLK  = 5;
const int PIN_LRCLK = 18;
const int PIN_DOUT  = 19;
const uint8_t AMP_L = 0x34;   // left speaker
const uint8_t AMP_R = 0x37;   // right speaker
const int OUT_RATE  = 48000;

// WAV file details. Defined up here, before any function, because Arduino IDE
// adds function declarations above the first function and they need this type.
struct Wav {
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t rate = 0, dataStart = 0, dataLen = 0;
};

I2SClass i2s;
bool ampsOk = false;
int volumeDown = 24;          // amp attenuation in dB, 0 = loudest

// ---------- AW88194 (16-bit registers, high byte first) ----------
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

void setVolume(int down) {
  volumeDown = constrain(down, 0, 48);
  for (uint8_t amp : {AMP_L, AMP_R}) {
    int32_t v = read16(amp, 0x0F);
    if (v < 0) continue;
    uint8_t six = volumeDown / 6;
    uint8_t half = ((volumeDown % 6) * 2) & 0x0F;
    write16(amp, 0x0F, (uint16_t)((v & 0x00FF) | ((uint16_t)((six << 4) | half) << 8)));
  }
}

// I2SCTRL 0x05: 0x0408 = left channel, 0x0808 = right channel (16-bit, 32fs, 48 kHz)
bool ampStart(uint8_t amp, uint16_t i2sctrl, const char *side) {
  int32_t id = read16(amp, 0x00);
  if (id != 0x1806) { log("Amp 0x%02X: chip ID 0x%04X, not an AW88194\n", amp, (unsigned)(id & 0xFFFF)); return false; }
  write16(amp, 0x05, i2sctrl);
  int32_t s = read16(amp, 0x04);
  if (s < 0) return false;
  s |= (1 << 6);                      // I2S on
  s &= ~1;                            // power-down off
  write16(amp, 0x04, (uint16_t)s);
  if (!waitBit(amp, 0, 300)) { log("Amp 0x%02X: PLL didn't lock\n", amp); return false; }
  s = read16(amp, 0x04);
  s &= ~(1 << 1);                     // amplifier on
  write16(amp, 0x04, (uint16_t)s);
  if (!waitBit(amp, 8, 300)) { log("Amp 0x%02X: not switching\n", amp); return false; }
  int32_t p = read16(amp, 0x08);
  if (p >= 0) write16(amp, 0x08, (uint16_t)(p & ~1));   // unmute
  log("Amp 0x%02X running (%s speaker)\n", amp, side);
  return true;
}

// ---------- WAV playback (runs in its own task) ----------
QueueHandle_t playQueue;
volatile bool stopRequest = false;
volatile bool playing = false;
char nowPlaying[160] = "";

uint32_t le32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
uint16_t le16(const uint8_t *b) { return b[0] | (b[1] << 8); }

// Walks the RIFF chunks to find "fmt " and "data".
bool parseWav(File &f, Wav &w) {
  uint8_t h[12];
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
  bool haveFmt = false;
  while (true) {
    uint8_t c[8];
    if (f.read(c, 8) != 8) return false;
    uint32_t sz = le32(c + 4);
    if (!memcmp(c, "fmt ", 4)) {
      uint8_t b[16];
      if (sz < 16 || f.read(b, 16) != 16) return false;
      w.format = le16(b);
      w.channels = le16(b + 2);
      w.rate = le32(b + 4);
      w.bits = le16(b + 14);
      f.seek(f.position() + (sz - 16) + (sz & 1));
      haveFmt = true;
    } else if (!memcmp(c, "data", 4)) {
      w.dataStart = f.position();
      w.dataLen = min(sz, (uint32_t)(f.size() - w.dataStart));
      return haveFmt;
    } else {
      f.seek(f.position() + sz + (sz & 1));
    }
  }
}

const int IN_FRAMES  = 1024;
const int OUT_FRAMES = 256;
int16_t inBuf[IN_FRAMES * 2];      // stereo frames from the file
uint8_t rawBuf[IN_FRAMES * 4];
int16_t outBuf[OUT_FRAMES * 2];
int16_t silence[OUT_FRAMES * 2];

File wavFile;
Wav wav;
uint32_t bytesLeft = 0;

// Reads frames into inBuf starting at frame 'at'; returns frames read.
int refill(int at) {
  int frameBytes = wav.channels * 2;
  uint32_t want = (uint32_t)(IN_FRAMES - at) * frameBytes;
  uint32_t bytes = min(want, bytesLeft - bytesLeft % frameBytes);
  if (!bytes) return 0;
  int got = wavFile.read(rawBuf, bytes);
  if (got <= 0) return 0;
  bytesLeft -= got;
  int frames = got / frameBytes;
  const int16_t *s = (const int16_t *)rawBuf;
  for (int i = 0; i < frames; i++) {
    int16_t l = s[i * wav.channels];
    int16_t r = wav.channels == 2 ? s[i * 2 + 1] : l;
    inBuf[(at + i) * 2] = l;
    inBuf[(at + i) * 2 + 1] = r;
  }
  return frames;
}

void playWav(const char *path) {
  wavFile = SD_MMC.open(path);
  if (!wavFile) { log("Can't open %s\n", path); return; }
  wav = Wav();
  if (!parseWav(wavFile, wav)) { log("%s isn't a WAV file I can read\n", path); wavFile.close(); return; }
  if ((wav.format != 1 && wav.format != 0xFFFE) || wav.bits != 16 || wav.channels < 1 || wav.channels > 2 ||
      wav.rate < 8000 || wav.rate > 96000) {
    log("%s: needs 16-bit PCM, mono or stereo (this is %u-bit, %u channel, %lu Hz, format %u)\n",
        path, wav.bits, wav.channels, (unsigned long)wav.rate, wav.format);
    wavFile.close();
    return;
  }
  wavFile.seek(wav.dataStart);
  bytesLeft = wav.dataLen;
  float seconds = (float)wav.dataLen / (wav.rate * wav.channels * 2);
  log("Playing %s (%s, %lu Hz, %.1f s)\n", path, wav.channels == 2 ? "stereo" : "mono",
      (unsigned long)wav.rate, seconds);

  strlcpy(nowPlaying, path, sizeof(nowPlaying));
  playing = true;
  stopRequest = false;

  // Linear-interpolation resampler, 16.16 fixed point. Integer maths only, so it keeps up.
  const uint32_t step = (uint32_t)(((uint64_t)wav.rate << 16) / OUT_RATE);
  uint32_t pos = 0;
  int inFrames = refill(0);
  bool done = inFrames < 2;
  while (!done && !stopRequest) {
    int n = 0;
    while (n < OUT_FRAMES) {
      uint32_t idx = pos >> 16;
      if (idx + 1 >= (uint32_t)inFrames) {
        // keep the last frame, move it to the front, read more behind it
        inBuf[0] = inBuf[(inFrames - 1) * 2];
        inBuf[1] = inBuf[(inFrames - 1) * 2 + 1];
        pos -= (uint32_t)(inFrames - 1) << 16;
        int got = refill(1);
        if (got == 0) { done = true; break; }
        inFrames = 1 + got;
        continue;
      }
      int32_t frac = (pos & 0xFFFF) >> 1;           // 15-bit, so the multiply can't overflow
      int32_t l0 = inBuf[idx * 2], l1 = inBuf[idx * 2 + 2];
      int32_t r0 = inBuf[idx * 2 + 1], r1 = inBuf[idx * 2 + 3];
      outBuf[n * 2] = (int16_t)(l0 + (((l1 - l0) * frac) >> 15));
      outBuf[n * 2 + 1] = (int16_t)(r0 + (((r1 - r0) * frac) >> 15));
      n++;
      pos += step;
    }
    if (n) i2s.write((const uint8_t *)outBuf, n * 4);
  }
  wavFile.close();
  log(stopRequest ? "Stopped.\n" : "Finished.\n");
  nowPlaying[0] = 0;
  playing = false;
}

void audioTask(void *) {
  char path[160];
  for (;;) {
    if (xQueueReceive(playQueue, path, 0) == pdTRUE) {
      playWav(path);
      continue;
    }
    // Silence between files keeps the clocks running and stops the I2S
    // hardware from repeating its last buffer as a buzz.
    i2s.write((const uint8_t *)silence, sizeof(silence));
  }
}

void stopAudio() {
  if (!playQueue) return;
  xQueueReset(playQueue);
  if (!playing) return;
  stopRequest = true;
  for (int i = 0; i < 100 && playing; i++) delay(10);
}

// Before switching off: amplifier off, then chip power-down (SYSCTRL bits 1 and 0).
void ampsOff() {
  for (uint8_t amp : {AMP_L, AMP_R}) {
    int32_t s = read16(amp, 0x04);
    if (s >= 0) write16(amp, 0x04, (uint16_t)(s | 0x0003));
  }
}

// ---------- Web routes for the player ----------
void setupPlayerRoutes() {
  server.on("/api/play", HTTP_POST, []() {
    String p = yoto::cleanPath(server.arg("path"));
    if (!ampsOk) { server.send(503, "text/plain", "The amps didn't start; see the log"); return; }
    if (p.length() < 2) { server.send(400, "text/plain", "Bad path"); return; }
    stopAudio();
    char path[160];
    strlcpy(path, p.c_str(), sizeof(path));
    xQueueSend(playQueue, path, 0);
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/stop", HTTP_POST, []() {
    stopAudio();
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/volume", HTTP_POST, []() {
    setVolume(server.arg("down").toInt());
    log("Volume -%d dB\n", volumeDown);
    server.send(200, "text/plain", "OK");
  });
}

String playerStatus() {
  return "\"playing\":\"" + yoto::jsonEscape(String(nowPlaying)) + "\",\"volume_down\":" + String(volumeDown) +
         ",\"amps\":" + String(ampsOk ? "true" : "false");
}

// ---------- Sounds for start-up, ready and switch-off ----------
// Optional files in the top folder of the SD card. Missing files are skipped.
bool playIfExists(const char *path) {
  if (!ampsOk || !playQueue || !yoto::sdOk || !SD_MMC.exists(path)) return false;
  char p[160];
  strlcpy(p, path, sizeof(p));
  return xQueueSend(playQueue, p, 0) == pdTRUE;
}

// Plays /shutdown.wav to the end (up to 4 s) before the board switches off.
void goodbye() {
  stopAudio();
  if (!playIfExists("/shutdown.wav")) return;
  for (int i = 0; i < 50 && !playing; i++) delay(10);     // wait for it to start
  for (int i = 0; i < 400 && playing; i++) delay(10);     // then to finish
}

// Runs inside yoto::begin(), before WiFi, so the start-up sound comes right away.
void startAudio() {
  memset(silence, 0, sizeof(silence));
  i2s.setPins(PIN_BCLK, PIN_LRCLK, PIN_DOUT, -1, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, OUT_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    log("I2S start FAILED\n");
    return;
  }
  for (int i = 0; i < 4; i++) i2s.write((const uint8_t *)silence, sizeof(silence));   // clocks before amps

  bool l = ampStart(AMP_L, 0x0408, "left");
  bool r = ampStart(AMP_R, 0x0808, "right");
  ampsOk = l || r;
  setVolume(volumeDown);

  playQueue = xQueueCreate(2, 160);
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 1);
  playIfExists("/startup.wav");
}

// ---------- Main ----------
void setup() {
  yoto::earlyHook = startAudio;
  bool safe = yoto::begin();
  if (safe) return;                   // safe mode: base only
  if (!playQueue) return;             // audio didn't start; the log says why

  yoto::stopHook = stopAudio;
  yoto::shutdownHook = ampsOff;
  yoto::goodbyeHook = goodbye;
  yoto::statusHook = playerStatus;
  setupPlayerRoutes();
  log("Player ready. Volume -%d dB.\n", volumeDown);
  playIfExists("/ready.wav");         // the web page is reachable now
}

void loop() {
  yoto::loop();
  delay(2);
}
