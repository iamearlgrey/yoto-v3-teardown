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

  Knobs (matching Yoto's own layout for turning):
    Left knob   turn = volume (clockwise louder; ticks when nothing is playing)
                press = pause / resume
    Right knob  turn = next / previous WAV in the current folder (/music if it exists)
                press = play / stop

  Charging mode: if plugging in USB-C switched the Yoto on, the amps and sounds stay
  off (WiFi and the web page still work). Press a knob or the power button to wake.

  Optional sounds in the top folder of the SD card (in sd-card/ next to this sketch):
    startup.wav   plays as soon as the amps start, before WiFi
    ready.wav     plays once WiFi is up and the web page is reachable
    shutdown.wav  plays when the power button has been held long enough: let go
                  and it switches off
*/

#include "yoto_base.h"
#include <ESP_I2S.h>
#include <vector>
#include <algorithm>

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

int readBoostMode();          // defined further down, used when the amps start
const int BOOST_DEFAULT = 6;  // smart boost 2
int boostFound = -1;          // boost mode the amps started in
int boostMode = -1;           // boost mode now

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
  // Boost mode (BSTCTRL2 0x61 bits 6:4): the amps power up in mode 2, which the
  // datasheet lists as reserved and which hisses. Smart boost 2 (6) was the quietest
  // by ear (Nov 12), ahead of mode 2 and force boost.
  int32_t b = read16(amp, 0x61);
  if (b >= 0) {
    if (boostFound < 0) boostFound = (b >> 4) & 7;
    write16(amp, 0x61, (uint16_t)((b & ~0x0070) | (BOOST_DEFAULT << 4)));
  }
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
  log("Amp 0x%02X running (%s speaker). Boost settings: 0x60 %04X, 0x61 %04X, 0x62 %04X\n", amp, side,
      (unsigned)(read16(amp, 0x60) & 0xFFFF), (unsigned)(read16(amp, 0x61) & 0xFFFF),
      (unsigned)(read16(amp, 0x62) & 0xFFFF));
  return true;
}

// ---------- WAV playback (runs in its own task) ----------
QueueHandle_t playQueue;
volatile bool stopRequest = false;
volatile bool playing = false;
volatile bool paused = false;
volatile bool tickRequest = false;
char nowPlaying[160] = "";
char lastPath[160] = "";

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
  strlcpy(lastPath, path, sizeof(lastPath));
  playing = true;
  paused = false;
  stopRequest = false;

  // Linear-interpolation resampler, 16.16 fixed point. Integer maths only, so it keeps up.
  const uint32_t step = (uint32_t)(((uint64_t)wav.rate << 16) / OUT_RATE);
  uint32_t pos = 0;
  int inFrames = refill(0);
  bool done = inFrames < 2;
  while (!done && !stopRequest) {
    if (paused) {                                   // hold position, keep the clocks going
      i2s.write((const uint8_t *)silence, sizeof(silence));
      continue;
    }
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
    if (n) {
      int peak = 0;                                 // for the face's speaking mouth
      for (int i = 0; i < n * 2; i++) { int v = abs(outBuf[i]); if (v > peak) peak = v; }
      yoto::face::level = peak / 32768.0f;
      i2s.write((const uint8_t *)outBuf, n * 4);
    }
  }
  yoto::face::level = 0;
  wavFile.close();
  log(stopRequest ? "Stopped.\n" : "Finished.\n");
  nowPlaying[0] = 0;
  paused = false;
  playing = false;
}

// A short tick at the current volume, so you can hear the level while turning
// the volume knob with nothing playing.
void playTick() {
  const int frames = OUT_RATE * 60 / 1000;          // 60 ms
  const float stepPhase = 2.0f * PI * 880.0f / OUT_RATE;
  float phase = 0;
  for (int done = 0; done < frames;) {
    int n = min(OUT_FRAMES, frames - done);
    for (int i = 0; i < n; i++) {
      int t = done + i;
      float env = min(1.0f, min(t, frames - t) / 240.0f);   // 5 ms fade in and out
      int16_t s = (int16_t)(sinf(phase) * env * 0.3f * 32767);
      outBuf[i * 2] = s;
      outBuf[i * 2 + 1] = s;
      phase += stepPhase;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    i2s.write((const uint8_t *)outBuf, n * 4);
    done += n;
  }
}

void audioTask(void *) {
  char path[160];
  for (;;) {
    if (xQueueReceive(playQueue, path, 0) == pdTRUE) {
      playWav(path);
      continue;
    }
    if (tickRequest) {
      tickRequest = false;
      playTick();
      continue;
    }
    // Silence between files keeps the clocks running and stops the I2S
    // hardware from repeating its last buffer as a buzz.
    i2s.write((const uint8_t *)silence, sizeof(silence));
  }
}

// ---------- Idle mute ----------
// With nothing playing, the amps' own noise is audible as static. They're muted
// (PWMCTRL 0x08 bit 0) after a moment of silence and unmuted just before any sound.
// Only called from the main loop task, so I2C use stays in one place.
bool ampsMuted = false;
uint32_t lastSoundMs = 0;
const uint32_t IDLE_MUTE_MS = 1500;

// One log line per amp: status (SYSST 0x01), control (SYSCTRL 0x04),
// mute (PWMCTRL 0x08 bit 0) and volume (0x0F upper byte), plus any fault flags.
void logAmps(const char *why) {
  for (uint8_t amp : {AMP_L, AMP_R}) {
    int32_t st = read16(amp, 0x01), ctl = read16(amp, 0x04), pwm = read16(amp, 0x08), vol = read16(amp, 0x0F);
    String faults;
    if (st >= 0) {
      if (st & (1 << 14)) faults += " low-voltage";
      if (st & (1 << 11)) faults += " boost-overcurrent";
      if (st & (1 << 10)) faults += " boost-overvoltage";
      if (st & (1 << 7))  faults += " clipping";
      if (st & (1 << 6))  faults += " dsp-watchdog";
      if (st & (1 << 5))  faults += " no-clock";
      if (st & (1 << 3))  faults += " amp-overcurrent";
      if (st & (1 << 1))  faults += " overheat";
      if (!(st & 1))      faults += " pll-unlocked";
    }
    log("  amp 0x%02X %s: status %04X ctrl %04X mute %s vol %04X%s\n", amp, why,
        (unsigned)(st & 0xFFFF), (unsigned)(ctl & 0xFFFF), pwm < 0 ? "?" : (pwm & 1 ? "on" : "off"),
        (unsigned)(vol & 0xFFFF), faults.c_str());
  }
}

// Sets one bit of a 16-bit amp register, checking it took (up to 3 tries).
bool setAmpBit(uint8_t amp, uint8_t reg, int bit, bool on) {
  for (int attempt = 0; attempt < 3; attempt++) {
    int32_t v = read16(amp, reg);
    if (v < 0) continue;
    write16(amp, reg, (uint16_t)(on ? (v | (1 << bit)) : (v & ~(1 << bit))));
    int32_t back = read16(amp, reg);
    if (back >= 0 && (((back >> bit) & 1) == (on ? 1 : 0))) return true;
  }
  return false;
}

// Idle: mute (PWMCTRL 0x08 bit 0), then power the amplifier stage down
// (SYSCTRL 0x04 bit 1, AMPPD). Powering down also stops the smart-boost converter,
// whose switching noise is the static heard after a loud setting (Nov 12 log:
// SYSST bit 13, smart boost, stayed set after the volume came back down).
// Wake: power up, wait for the amp to report switching (SYSST bit 8), then unmute.
void setAmpsMuted(bool mute) {
  bool ok = true;
  for (uint8_t amp : {AMP_L, AMP_R}) {
    bool done;
    if (mute) {
      done = setAmpBit(amp, 0x08, 0, true) && setAmpBit(amp, 0x04, 1, true);
    } else {
      done = setAmpBit(amp, 0x04, 1, false);
      uint32_t t0 = millis();
      if (done && !waitBit(amp, 8, 100)) log("Amp 0x%02X slow to restart.\n", amp);
      uint32_t took = millis() - t0;
      done = done && setAmpBit(amp, 0x08, 0, false);
      if (took > 20) log("Amp 0x%02X took %lu ms to wake.\n", amp, (unsigned long)took);
    }
    if (!done) {
      ok = false;
      log("Amp 0x%02X didn't %s.\n", amp, mute ? "go idle" : "wake");
    }
  }
  ampsMuted = mute;
  logAmps(mute ? "idle (powered down)" : "awake");
  if (!ok) {                           // so the next check tries again, 1.5 s from now
    ampsMuted = !mute;
    lastSoundMs = millis();
  }
}

void wakeAmps() {
  lastSoundMs = millis();
  if (ampsMuted && ampsOk) setAmpsMuted(false);
}

// While muted, look at the amps every 3 s and log if their state changes
// (a fault flag, or a mute that came undone), so the static can be traced.
uint32_t lastAmpWatch = 0;
int32_t lastWatched[2] = {-1, -1};

void watchAmps() {
  if (!ampsOk || !ampsMuted || millis() - lastAmpWatch < 3000) return;
  lastAmpWatch = millis();
  bool changed = false;
  int i = 0;
  for (uint8_t amp : {AMP_L, AMP_R}) {
    int32_t st = read16(amp, 0x01), pwm = read16(amp, 0x08);
    int32_t key = ((st & 0xFFFF) << 1) | (pwm & 1);
    if (key != lastWatched[i]) changed = true;
    lastWatched[i++] = key;
    int32_t ctl = read16(amp, 0x04);
    if ((pwm >= 0 && !(pwm & 1)) || (ctl >= 0 && !(ctl & 2))) {
      log("Amp 0x%02X came out of idle on its own; idling again.\n", amp);
      ampsMuted = false;
    }
  }
  if (changed) logAmps("idle");
}

void idleMuteCheck() {
  watchAmps();
  if (!ampsOk || ampsMuted) return;
  if (playing && !paused) { lastSoundMs = millis(); return; }
  if (tickRequest) return;
  if (millis() - lastSoundMs > IDLE_MUTE_MS) setAmpsMuted(true);
}

void stopAudio() {
  if (!playQueue) return;
  xQueueReset(playQueue);
  if (!playing) return;
  stopRequest = true;
  paused = false;
  for (int i = 0; i < 100 && playing; i++) delay(10);
}

// ---------- Starting the audio (now, or later in charging mode) ----------
bool audioTried = false;

bool playIfExists(const char *path) {
  if (!ampsOk || !playQueue || !yoto::sdOk || !SD_MMC.exists(path)) return false;
  wakeAmps();
  char p[160];
  strlcpy(p, path, sizeof(p));
  return xQueueSend(playQueue, p, 0) == pdTRUE;
}

// Starts I2S, both amps and the audio task, then plays /startup.wav.
// Runs inside yoto::begin() before WiFi, so the start-up sound comes right away,
// unless USB-C switched the Yoto on (charging mode): then it waits for a button.
void startAudio() {
  if (audioTried) return;
  audioTried = true;
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
  boostMode = readBoostMode();

  playQueue = xQueueCreate(2, 160);
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 1);
  playIfExists("/startup.wav");
}

void earlyStart() {
  if (yoto::wokeByUsb) {
    log("Charging mode: amps and sounds stay off. Press a knob or the power button, or play something, to wake.\n");
    return;
  }
  startAudio();
}

// Wakes the audio if charging mode left it off. Returns true when audio is ready.
bool ensureAudio() {
  if (!audioTried) {
    log("Waking from charging mode.\n");
    yoto::chargingMode = false;
    yoto::light::off(500);
    yoto::face::set(yoto::face::IDLE);
    startAudio();
    return false;                    // the start-up sound is the response to this press
  }
  return playQueue && ampsOk;
}

// ---------- Tracks: the folder of the last file played ----------
// Turning the right knob moves through the WAV files in that folder, in name order.
// With nothing played yet it uses /music if it exists, otherwise the top folder.
// The start-up, ready and shutdown sounds are skipped.
String currentFolder() {
  String last = lastPath;
  if (last.length()) {
    int slash = last.lastIndexOf('/');
    return slash > 0 ? last.substring(0, slash) : String("/");
  }
  return SD_MMC.exists("/music") ? String("/music") : String("/");
}

bool isTrack(const String &name) {
  String n = name;
  n.toLowerCase();
  if (!n.endsWith(".wav") || n.startsWith(".")) return false;
  return n != "startup.wav" && n != "ready.wav" && n != "shutdown.wav";
}

std::vector<String> listTracks(const String &folder) {
  std::vector<String> out;
  File d = SD_MMC.open(folder);
  if (!d || !d.isDirectory()) return out;
  File f = d.openNextFile();
  while (f && out.size() < 300) {
    String name = f.name();
    if (!f.isDirectory() && isTrack(name)) out.push_back((folder == "/" ? "" : folder) + "/" + name);
    f.close();
    f = d.openNextFile();
  }
  d.close();
  std::sort(out.begin(), out.end(), [](const String &a, const String &b) { return a.compareTo(b) < 0; });
  return out;
}

void playTrack(const String &path) {
  if (!ensureAudio()) return;
  stopAudio();
  wakeAmps();
  char p[160];
  strlcpy(p, path.c_str(), sizeof(p));
  xQueueSend(playQueue, p, 0);
}

void stepTrack(int by) {
  if (!yoto::sdOk) return;
  String folder = currentFolder();
  std::vector<String> tracks = listTracks(folder);
  if (tracks.empty()) { log("No WAV files in %s\n", folder.c_str()); return; }
  int n = tracks.size(), at = -1;
  for (int i = 0; i < n; i++) if (tracks[i] == String(lastPath)) at = i;
  int next = at < 0 ? (by > 0 ? 0 : n - 1) : ((at + by) % n + n) % n;   // wraps around
  playTrack(tracks[next]);
}

// ---------- Knobs and buttons ----------
// Left knob: turn = volume (clockwise louder), press = pause / resume.
// Right knob: turn = next / previous track, press = play / stop.
const int VOLUME_STEP_DB = 3;

void onKnob(int knob, int clicks) {
  if (knob == 0) {
    int before = volumeDown;
    setVolume(volumeDown - clicks * VOLUME_STEP_DB);
    if (volumeDown != before) {
      log("Volume -%d dB\n", volumeDown);
      logAmps("after volume");
    }
    if (!ensureAudio()) return;
    if (!playing) {                      // let you hear the new level
      wakeAmps();
      tickRequest = true;
    }
  } else {
    stepTrack(clicks > 0 ? 1 : -1);
  }
}

// Pause powers the amps down straight away (no 1.5 s of hiss); resume wakes them first.
void togglePause() {
  if (!playing) return;
  if (paused) {
    wakeAmps();
    paused = false;
    log("Resumed.\n");
  } else {
    paused = true;
    delay(15);                       // let the audio task finish its current chunk
    if (!ampsMuted) setAmpsMuted(true);
    log("Paused.\n");
  }
}

// ---------- Boost mode (AW88194 BSTCTRL2 0x61, bits 6:4) ----------
// Datasheet: 0 = transparent (boost off), 1 = force boost, 5 = smart boost 1,
// 6 = smart boost 2. The amps start in mode 2, which the datasheet lists as reserved;
// Yoto's firmware presumably sets this, ours didn't. Changed from the web page to compare.

int readBoostMode() {
  int32_t v = read16(AMP_L, 0x61);
  return v < 0 ? -1 : (int)((v >> 4) & 7);
}

void setBoostMode(int mode) {
  if (!ampsOk) return;
  if (!ampsMuted) setAmpsMuted(true);   // change it while the amps are powered down
  for (uint8_t amp : {AMP_L, AMP_R}) {
    int32_t v = read16(amp, 0x61);
    if (v >= 0) write16(amp, 0x61, (uint16_t)((v & ~0x0070) | ((mode & 7) << 4)));
  }
  boostMode = readBoostMode();
  log("Boost mode set to %d (%s)\n", boostMode,
      boostMode == 0 ? "off" : boostMode == 1 ? "always on" : boostMode == 5 ? "smart 1" :
      boostMode == 6 ? "smart 2" : "as found");
}

void onButton(int button, bool pressed) {
  if (!pressed) return;
  if (button == 2) { ensureAudio(); return; }   // power button: only wakes from charging mode
  if (!ensureAudio()) return;
  if (button == 0) {
    togglePause();
  } else {
    if (playing) stopAudio();
    else if (lastPath[0]) playTrack(String(lastPath));
    else stepTrack(1);
  }
}

// ---------- Web routes for the player ----------
void setupPlayerRoutes() {
  server.on("/api/play", HTTP_POST, []() {
    String p = yoto::cleanPath(server.arg("path"));
    if (p.length() < 2) { server.send(400, "text/plain", "Bad path"); return; }
    if (!audioTried) { ensureAudio(); delay(300); }
    if (!ampsOk) { server.send(503, "text/plain", "The amps didn't start; see the log"); return; }
    playTrack(p);
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/stop", HTTP_POST, []() {
    stopAudio();
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/pause", HTTP_POST, []() {
    togglePause();
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/boost", HTTP_POST, []() {
    int m = server.arg("mode").toInt();
    if (m != 0 && m != 1 && m != 5 && m != 6 && m != boostFound) { server.send(400, "text/plain", "Bad mode"); return; }
    setBoostMode(m);
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/volume", HTTP_POST, []() {
    setVolume(server.arg("down").toInt());
    log("Volume -%d dB\n", volumeDown);
    server.send(200, "text/plain", "OK");
  });
}

String playerStatus() {
  return "\"playing\":\"" + yoto::jsonEscape(String(nowPlaying)) + "\",\"paused\":" + String(paused ? "true" : "false") +
         ",\"volume_down\":" + String(volumeDown) + ",\"amps\":" + String(ampsOk ? "true" : "false") +
         ",\"charging_mode\":" + String(!audioTried ? "true" : "false") +
         ",\"boost\":" + String(boostMode) + ",\"boost_found\":" + String(boostFound);
}

// Plays /shutdown.wav to the end (up to 4 s) before the board switches off.
void goodbye() {
  stopAudio();
  yoto::face::set(yoto::face::ASLEEP);
  if (!playIfExists("/shutdown.wav")) return;
  for (int i = 0; i < 50 && !playing; i++) delay(10);     // wait for it to start
  for (int i = 0; i < 400 && playing; i++) delay(10);     // then to finish
}

// Before switching off: amplifier off, then chip power-down (SYSCTRL bits 1 and 0).
void ampsOff() {
  if (!audioTried) return;
  for (uint8_t amp : {AMP_L, AMP_R}) {
    int32_t s = read16(amp, 0x04);
    if (s >= 0) write16(amp, 0x04, (uint16_t)(s | 0x0003));
  }
}

// ---------- Main ----------
void setup() {
  yoto::earlyHook = earlyStart;
  bool safe = yoto::begin();
  if (safe) return;                   // safe mode: base only

  yoto::stopHook = stopAudio;
  yoto::shutdownHook = ampsOff;
  yoto::goodbyeHook = goodbye;
  yoto::statusHook = playerStatus;
  yoto::knobHook = onKnob;
  yoto::buttonHook = onButton;
  setupPlayerRoutes();
  yoto::face::begin();
  if (yoto::chargingMode) yoto::face::set(yoto::face::ASLEEP);
  if (!audioTried) return;            // charging mode: quiet until woken
  log("Player ready. Volume -%d dB. Left knob: volume, press to pause. Right knob: tracks, press to play/stop.\n",
      volumeDown);
  playIfExists("/ready.wav");         // the web page is reachable now
}

// With auto on, the face speaks while sound plays (a stand-in until CAL has a voice).
void faceAuto() {
  using namespace yoto::face;
  if (!autoMode || manual || yoto::chargingMode) return;
  bool sound = playing && !paused;
  if (sound && state == IDLE) state = SPEAKING;
  else if (!sound && state == SPEAKING) state = IDLE;
}

void loop() {
  yoto::loop();
  idleMuteCheck();
  faceAuto();
  delay(2);
}
