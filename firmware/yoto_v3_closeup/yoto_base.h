/*
  yoto_base.h: the "close-up" foundation for Yoto Player V3 sketches
  ------------------------------------------------------------------
  Include this in every future sketch so the case can stay closed:
    - WiFi from /wifi.txt on the SD card, or a setup hotspot "CAL-Yoto-Setup"
    - Wireless updates from Arduino IDE (network port "cal-yoto")
    - Web page at http://cal-yoto.local : log, SD card files, battery, restart
    - Safe mode: hold a knob button while powering on (or press "Restart in safe mode"
      on the web page), and the app is skipped
      (only WiFi, updates and the web page run), so a broken sketch can't lock you out
    - Power off: hold the power button 3 seconds (on battery)

  Use:
    #include "yoto_base.h"
    void setup() { bool safe = yoto::begin(); if (safe) return; ...app setup... }
    void loop()  { yoto::loop(); if (yoto::safeMode) return; ...app loop... }
  Optional hooks: yoto::stopHook (stop audio before updates/uploads/power off),
  yoto::statusHook (extra JSON fields for the web page).
*/
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <SD_MMC.h>
#include <stdarg.h>
#include "web_page.h"

namespace yoto {

// ---------- Board facts (see the teardown repo's hardware reference) ----------
const int PIN_SDA = 21;
const int PIN_SCL = 25;
const uint8_t IOX_A   = 0x20;   // ports p0, p1 (knob buttons, power button, headphone detect)
const uint8_t IOX_B   = 0x21;   // ports p2, p3 (amp release, power hold)
const uint8_t GAUGE   = 0x62;   // CW2015
const uint8_t CHARGER = 0x1A;   // SGM41513
const char *HOSTNAME  = "cal-yoto";
const char *AP_NAME   = "CAL-Yoto-Setup";

// ---------- State ----------
WebServer server(80);
bool safeMode = false;
bool sdOk = false;
bool apMode = false;
void (*stopHook)() = nullptr;      // set by the app
String (*statusHook)() = nullptr;  // set by the app; returns extra JSON fields, e.g. "\"x\":1"

uint32_t bootMs = 0;
uint32_t readyMs = 0;              // when begin() finished (WiFi can take a while)
float batteryVolts = 0;
String inputState = "?";
String chargeState = "?";
String otaPass;

// ---------- Log: Serial + a ring buffer the web page reads ----------
const size_t LOG_SIZE = 12000;
String logText;
uint32_t logTotal = 0;             // characters ever logged
SemaphoreHandle_t logLock = nullptr;

void log(const char *fmt, ...) {
  char b[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  Serial.print(b);
  if (!logLock) return;
  xSemaphoreTake(logLock, portMAX_DELAY);
  logText += b;
  logTotal += strlen(b);
  if (logText.length() > LOG_SIZE) logText.remove(0, logText.length() - LOG_SIZE * 3 / 4);
  xSemaphoreGive(logLock);
}

// ---------- I2C ----------
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

// Stock values: IOX.2.4 releases the amps, IOX.3.1 holds the power on.
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

// ---------- Helpers ----------
String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

// Keeps paths on the card: always starts with "/", no "..".
String cleanPath(String p) {
  p.trim();
  p.replace("\\", "/");
  if (!p.startsWith("/")) p = "/" + p;
  while (p.indexOf("//") >= 0) p.replace("//", "/");
  if (p.indexOf("..") >= 0) return "";
  if (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

void stopApp() {
  if (stopHook) stopHook();
}

// ---------- Log on the SD card ----------
// The current session goes to /cal_log.txt; at boot the previous one becomes
// /cal_log_prev.txt, so an unexpected shutdown leaves a record.
uint32_t logSaved = 0;
uint32_t lastLogSave = 0;

void saveLog(bool force = false) {
  if (!sdOk || !logLock) return;
  if (!force && millis() - lastLogSave < 3000) return;
  lastLogSave = millis();
  xSemaphoreTake(logLock, portMAX_DELAY);
  uint32_t start = logTotal - logText.length();
  String chunk = logSaved >= logTotal ? String() : logText.substring(logSaved > start ? logSaved - start : 0);
  logSaved = logTotal;
  xSemaphoreGive(logLock);
  if (!chunk.length()) return;
  File f = SD_MMC.open("/cal_log.txt", FILE_APPEND);
  if (f) { f.print(chunk); f.close(); }
}

void rotateLog() {
  if (!sdOk || !SD_MMC.exists("/cal_log.txt")) return;
  SD_MMC.remove("/cal_log_prev.txt");
  SD_MMC.rename("/cal_log.txt", "/cal_log_prev.txt");
}

const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power on (or power was cut)";
    case ESP_RST_SW:        return "restart from firmware";
    case ESP_RST_PANIC:     return "CRASH (firmware error)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog (firmware stuck)";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (supply voltage dipped)";
    case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
    case ESP_RST_EXT:       return "reset pin";
    default:                return "unknown";
  }
}

// ---------- Safe mode request, kept across a restart ----------
RTC_NOINIT_ATTR uint32_t safeBootFlag;
const uint32_t SAFE_MAGIC = 0x5AFE5AFE;

void restartIntoSafeMode() {
  log("Restarting into safe mode.\n");
  stopApp();
  saveLog(true);
  safeBootFlag = SAFE_MAGIC;
  delay(300);
  ESP.restart();
}

// ---------- Power ----------
// Confirmed Nov 12: buttons read 0 when pressed.
//   left knob = port0 bit 5, right knob = port0 bit 4, power = port1 bit 3, headphones = port1 bit 1
void (*shutdownHook)() = nullptr;  // set by the app: power down amps etc.
void (*goodbyeHook)() = nullptr;   // set by the app: play a sound before switching off
void (*earlyHook)() = nullptr;     // set by the app before begin(): runs before WiFi (not in safe mode)

bool usbPresent() {
  int st = read8(CHARGER, 0x08);
  return st >= 0 && ((st >> 5) & 7) != 0;
}

bool powerButtonDown() {
  int p1 = read8(IOX_A, 0x01);
  return p1 >= 0 && ((p1 >> 3) & 1) == 0;
}

// Mirrors Yoto's own shutdown (open-yoto power notes): quiet everything, turn the
// expander pins into inputs except VIN_HOLD (IOX.3.1) and the level converter
// (IOX.3.0), wait for the power button to be let go, then release VIN_HOLD.
void powerOff() {
  log("Power off requested.\n");
  if (usbPresent()) {
    log("USB-C is connected, so the board stays powered. Unplug USB-C, then hold the power button again.\n");
    return;
  }
  if (goodbyeHook) goodbyeHook();   // the shutdown sound: your cue to let go
  stopApp();
  if (shutdownHook) shutdownHook();
  for (int i = 0; i < 100 && powerButtonDown(); i++) delay(100);   // up to 10 s for release
  log("Switching off.\n");
  saveLog(true);
  delay(100);
  write8(IOX_A, 0x06, 0xFF);        // port0 all inputs
  write8(IOX_A, 0x07, 0xFF);        // port1 all inputs
  write8(IOX_B, 0x06, 0xFF);        // port2 all inputs (amp release, display selects, power enable)
  write8(IOX_B, 0x07, 0xFC);        // port3: only bit0 (level converter) and bit1 (VIN_HOLD) stay outputs
  delay(50);
  write8(IOX_B, 0x03, 0xDD);        // VIN_HOLD low
  delay(3000);
  // Still here: power wasn't cut. Restart to restore everything, and say so.
  setupExpanders();
  log("Still running 3 s after releasing the power hold on battery. Restarting instead.\n");
  saveLog(true);
  delay(200);
  ESP.restart();
}

bool powerArmed = false;           // only after the button has been seen released (not the press that switched it on)
uint32_t pressStart = 0;
uint32_t knobStart = 0;
int lastP0 = -2, lastP1 = -2;
uint32_t lastInputPoll = 0;

void pollInputs() {
  if (millis() - lastInputPoll < 50) return;
  lastInputPoll = millis();
  int p0 = read8(IOX_A, 0x00);
  int p1 = read8(IOX_A, 0x01);
  if (p0 < 0 || p1 < 0) return;
  bool left = !((p0 >> 5) & 1), right = !((p0 >> 4) & 1);
  bool power = !((p1 >> 3) & 1), phones = !((p1 >> 1) & 1);
  if (p0 != lastP0 || p1 != lastP1) {
    if (lastP0 >= 0) {
      if (((p0 ^ lastP0) >> 5) & 1) log("Left knob %s\n", left ? "pressed" : "released");
      if (((p0 ^ lastP0) >> 4) & 1) log("Right knob %s\n", right ? "pressed" : "released");
      if (((p1 ^ lastP1) >> 3) & 1) log("Power button %s\n", power ? "pressed" : "released");
      if (((p1 ^ lastP1) >> 1) & 1) log("Headphones %s\n", phones ? "in" : "out");
    }
    int other0 = (p0 ^ lastP0) & ~0x30, other1 = (p1 ^ lastP1) & ~0x0A;
    if (lastP0 < 0 || other0 || other1) log("Inputs: port0 0x%02X  port1 0x%02X\n", p0, p1);
    lastP0 = p0;
    lastP1 = p1;
  }

  // Safe mode by holding a knob for 1 s during the first 6 s after start-up finishes.
  if (!safeMode && millis() - readyMs < 6000) {
    if (left || right) {
      if (!knobStart) knobStart = millis();
      else if (millis() - knobStart > 1000) restartIntoSafeMode();
    } else knobStart = 0;
  }

  // Power off: hold 3 s, counted only once the switch-on press has been let go.
  if (!power) { powerArmed = true; pressStart = 0; return; }
  if (!powerArmed) return;
  if (!pressStart) pressStart = millis();
  else if (millis() - pressStart > 3000) {
    powerArmed = false;
    powerOff();
  }
}

// ---------- Battery ----------
uint32_t lastBattery = 0;

void pollBattery(bool force = false) {
  if (!force && millis() - lastBattery < 5000) return;
  lastBattery = millis();
  int hi = read8(GAUGE, 0x02), lo = read8(GAUGE, 0x03);
  if (hi >= 0 && lo >= 0) batteryVolts = (((hi << 8) | lo) & 0x3FFF) * 0.000305f;
  int st = read8(CHARGER, 0x08);
  if (st >= 0) {
    switch ((st >> 5) & 7) {
      case 0: inputState = "battery"; break;
      case 1: inputState = "USB 500 mA"; break;
      case 2: inputState = "USB adapter"; break;
      default: inputState = "other"; break;
    }
    switch ((st >> 3) & 3) {
      case 0: chargeState = "not charging"; break;
      case 1: chargeState = "pre-charging"; break;
      case 2: chargeState = "charging"; break;
      default: chargeState = "charged"; break;
    }
  }
}

// ---------- WiFi ----------
volatile int lastDisconnectReason = 0;
String scanJson = "[]";          // networks seen at the last scan, for the setup page

const char *reasonText(int r) {
  switch (r) {
    case 2:
      return "the router stopped answering mid-login, usually a weak signal (below about -80 dBm), sometimes the password";
    case 15: case 202: case 204: case 205:
      return "the security handshake failed: usually the password, sometimes a weak signal or WPA3-only settings";
    case 201:
      return "the network wasn't found (5 GHz only, out of range, or the name is different)";
    case 210: case 211:
      return "the network's security type isn't supported (try a WPA2 or WPA2/WPA3 mixed setting)";
    case 0:
      return "no reason reported";
    default:
      return "see the reason number";
  }
}

// Lists nearby networks to the log, and keeps them for the setup page's name list.
void scanNetworks(const String &wanted) {
  int n = WiFi.scanNetworks();
  if (n <= 0) { log("WiFi scan found no networks.\n"); return; }
  bool seen = false;
  scanJson = "[";
  log("Networks the Yoto can see (2.4 GHz only):\n");
  for (int i = 0; i < n && i < 20; i++) {
    String name = WiFi.SSID(i);
    if (name == wanted) seen = true;
    log("  %-32s signal %4d dBm  channel %2d%s\n", name.length() ? name.c_str() : "(hidden)", WiFi.RSSI(i),
        WiFi.channel(i), WiFi.encryptionType(i) == WIFI_AUTH_WPA3_PSK ? "  WPA3 only" : "");
    if (name.length()) {
      if (scanJson.length() > 1) scanJson += ",";
      scanJson += "\"" + jsonEscape(name) + "\"";
    }
  }
  scanJson += "]";
  WiFi.scanDelete();
  if (wanted.length())
    log(seen ? "\"%s\" is visible. Below about -80 dBm, logins often fail even with the right password (check the antenna is connected).\n"
             : "\"%s\" isn't in the list: it may be 5 GHz only, or named differently.\n", wanted.c_str());
}

void startHotspot(const String &wanted = "") {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);          // hotspot, while still able to scan
  delay(100);
  scanNetworks(wanted);
  WiFi.softAP(AP_NAME);
  apMode = true;
  log("Setup hotspot \"%s\" started. Join it, then open http://192.168.4.1\n", AP_NAME);
}

void startWifi() {
  String ssid, pass;
  if (sdOk) {
    File f = SD_MMC.open("/wifi.txt");
    if (f) {
      ssid = f.readStringUntil('\n');
      pass = f.readStringUntil('\n');
      otaPass = f.readStringUntil('\n');
      f.close();
      ssid.trim(); pass.trim(); otaPass.trim();
    }
  }
  if (ssid.length() == 0) {
    log("No /wifi.txt on the SD card.\n");
    startHotspot();
    return;
  }
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);   // full transmit power, for a distant router
  WiFi.begin(ssid.c_str(), pass.c_str());
  log("Joining WiFi \"%s\" (password is %u characters)", ssid.c_str(), (unsigned)pass.length());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    int r = lastDisconnectReason;
    log("\nCouldn't join \"%s\" in 20 s. Reason %d: %s.\n", ssid.c_str(), r, reasonText(r));
    startHotspot(ssid);
    return;
  }
  WiFi.setSleep(false);            // faster transfers and updates
  WiFi.setAutoReconnect(true);
  log("\nJoined. Open http://%s.local or http://%s\n", HOSTNAME, WiFi.localIP().toString().c_str());
}

// ---------- Wireless updates ----------
void setupOta() {
  ArduinoOTA.setHostname(HOSTNAME);
  if (otaPass.length()) ArduinoOTA.setPassword(otaPass.c_str());
  ArduinoOTA.onStart([]() {
    stopApp();
    log("Wireless update starting...\n");
  });
  ArduinoOTA.onEnd([]() { log("Update received. Restarting.\n"); });
  ArduinoOTA.onError([](ota_error_t e) { log("Update failed (error %u).\n", (unsigned)e); });
  ArduinoOTA.begin();               // also starts mDNS as cal-yoto.local
  MDNS.addService("http", "tcp", 80);
  log("Wireless updates ready: in Arduino IDE pick the network port \"%s\".\n", HOSTNAME);
}

// ---------- Web: files ----------
File upFile;
uint8_t *upBuf = nullptr;
const size_t UP_BUF = 32768;      // collect small network chunks into big SD writes
size_t upLen = 0;
String upPath;
bool upOk = false;

void flushUpload() {
  if (upLen && upFile) {
    if (upFile.write(upBuf, upLen) != upLen) upOk = false;
  }
  upLen = 0;
}

void handleUpload() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    stopApp();
    String dir = cleanPath(server.arg("dir"));
    String name = u.filename;
    name.replace("/", "_");
    name.replace("\\", "_");
    upPath = (dir == "/" ? "" : dir) + "/" + name;
    if (!upBuf) upBuf = (uint8_t *)(psramFound() ? ps_malloc(UP_BUF) : malloc(UP_BUF));
    upFile = SD_MMC.open(upPath, FILE_WRITE);
    upOk = upFile && upBuf && dir.length();
    upLen = 0;
    log("Receiving %s\n", upPath.c_str());
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!upOk) return;
    size_t i = 0;
    while (i < u.currentSize) {
      size_t n = min((size_t)(u.currentSize - i), UP_BUF - upLen);
      memcpy(upBuf + upLen, u.buf + i, n);
      upLen += n;
      i += n;
      if (upLen == UP_BUF) flushUpload();
    }
  } else if (u.status == UPLOAD_FILE_END) {
    flushUpload();
    if (upFile) upFile.close();
    log(upOk ? "Saved %s (%u bytes)\n" : "Failed to save %s\n", upPath.c_str(), (unsigned)u.totalSize);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
    SD_MMC.remove(upPath);
    upOk = false;
    log("Upload of %s cancelled.\n", upPath.c_str());
  }
}

void handleList() {
  String dir = cleanPath(server.arg("dir"));
  File d = dir.length() ? SD_MMC.open(dir) : File();
  if (!d || !d.isDirectory()) { server.send(404, "application/json", "{\"error\":\"Folder not found\"}"); return; }
  String out = "{\"dir\":\"" + jsonEscape(dir) + "\",\"items\":[";
  int count = 0;
  File f = d.openNextFile();
  while (f && count < 500) {
    if (count++) out += ",";
    out += "{\"name\":\"" + jsonEscape(String(f.name())) + "\",\"dir\":" + (f.isDirectory() ? "true" : "false") +
           ",\"size\":" + String((uint32_t)f.size()) + "}";
    f.close();
    f = d.openNextFile();
  }
  d.close();
  out += "]}";
  server.send(200, "application/json", out);
}

const char *contentType(const String &p) {
  String l = p;
  l.toLowerCase();
  if (l.endsWith(".wav")) return "audio/wav";
  if (l.endsWith(".mp3")) return "audio/mpeg";
  if (l.endsWith(".txt") || l.endsWith(".log")) return "text/plain";
  if (l.endsWith(".json")) return "application/json";
  if (l.endsWith(".png")) return "image/png";
  if (l.endsWith(".jpg") || l.endsWith(".jpeg")) return "image/jpeg";
  return "application/octet-stream";
}

void handleDownload() {
  String p = cleanPath(server.arg("path"));
  File f = p.length() ? SD_MMC.open(p) : File();
  if (!f || f.isDirectory()) { server.send(404, "text/plain", "Not found"); return; }
  String name = p.substring(p.lastIndexOf('/') + 1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, contentType(p));
  f.close();
}

bool removeTree(const String &p) {
  File f = SD_MMC.open(p);
  if (!f) return false;
  if (!f.isDirectory()) { f.close(); return SD_MMC.remove(p); }
  File c = f.openNextFile();
  while (c) {
    String child = p + "/" + String(c.name());
    c.close();
    removeTree(child);
    c = f.openNextFile();
  }
  f.close();
  return SD_MMC.rmdir(p);
}

void handleDelete() {
  String p = cleanPath(server.arg("path"));
  if (p.length() < 2) { server.send(400, "text/plain", "Bad path"); return; }
  stopApp();
  bool ok = removeTree(p);
  log(ok ? "Deleted %s\n" : "Couldn't delete %s\n", p.c_str());
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Failed");
}

void handleMkdir() {
  String p = cleanPath(server.arg("path"));
  bool ok = p.length() > 1 && SD_MMC.mkdir(p);
  log(ok ? "Made folder %s\n" : "Couldn't make folder %s\n", p.c_str());
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Failed");
}

// ---------- Web: status, log, settings ----------
void handleStatus() {
  pollBattery(true);
  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  String out = "{";
  out += "\"name\":\"" + String(HOSTNAME) + "\"";
  out += ",\"ip\":\"" + ip + "\"";
  out += ",\"hotspot\":" + String(apMode ? "true" : "false");
  out += ",\"rssi\":" + String(apMode ? 0 : WiFi.RSSI());
  out += ",\"safe\":" + String(safeMode ? "true" : "false");
  out += ",\"sd\":" + String(sdOk ? "true" : "false");
  out += ",\"volts\":" + String(batteryVolts, 3);
  out += ",\"input\":\"" + inputState + "\"";
  out += ",\"charge\":\"" + chargeState + "\"";
  out += ",\"uptime\":" + String(millis() / 1000);
  out += ",\"heap\":" + String(ESP.getFreeHeap());
  if (statusHook) {
    String extra = statusHook();
    if (extra.length()) out += "," + extra;
  }
  out += "}";
  server.send(200, "application/json", out);
}

void handleLog() {
  uint32_t since = server.arg("since").toInt();
  xSemaphoreTake(logLock, portMAX_DELAY);
  uint32_t start = logTotal - logText.length();
  String chunk = since <= start ? logText : (since >= logTotal ? String() : logText.substring(since - start));
  uint32_t total = logTotal;
  xSemaphoreGive(logLock);
  server.sendHeader("X-Log-Total", String(total));
  server.sendHeader("Access-Control-Expose-Headers", "X-Log-Total");
  server.send(200, "text/plain; charset=utf-8", chunk);
}

void handleWifiSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass"), ota = server.arg("ota");
  ssid.trim();
  if (!sdOk || ssid.length() == 0) { server.send(400, "text/plain", "Need a network name and a working SD card"); return; }
  File f = SD_MMC.open("/wifi.txt", FILE_WRITE);
  if (!f) { server.send(500, "text/plain", "Couldn't write /wifi.txt"); return; }
  f.print(ssid + "\n" + pass + "\n" + ota + "\n");
  f.close();
  log("Saved WiFi settings for \"%s\". Restarting.\n", ssid.c_str());
  server.send(200, "text/plain", "Saved. Restarting and joining your WiFi...");
  delay(1000);
  ESP.restart();
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/list", HTTP_GET, handleList);
  server.on("/api/download", HTTP_GET, handleDownload);
  server.on("/api/delete", HTTP_POST, handleDelete);
  server.on("/api/mkdir", HTTP_POST, handleMkdir);
  server.on("/api/upload", HTTP_POST, []() {
    server.send(upOk ? 200 : 500, "text/plain", upOk ? "OK" : "Upload failed");
  }, handleUpload);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/networks", HTTP_GET, []() { server.send(200, "application/json", scanJson); });
  server.on("/api/restart", HTTP_POST, []() {
    server.send(200, "text/plain", "Restarting");
    log("Restart requested from the web page.\n");
    stopApp();
    saveLog(true);
    delay(500);
    ESP.restart();
  });
  server.on("/api/safemode", HTTP_POST, []() {
    server.send(200, "text/plain", "Restarting into safe mode");
    delay(200);
    restartIntoSafeMode();
  });
  server.on("/api/poweroff", HTTP_POST, []() {
    server.send(200, "text/plain", "Switching off");
    delay(200);
    powerOff();
  });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
}

// ---------- Start and run ----------
bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  bool ioxOk = setupExpanders();     // first, so the power hold is on early
  bootMs = millis();
  Serial.begin(115200);
  logLock = xSemaphoreCreateMutex();
  delay(200);

  log("\n=== CAL Yoto (boot %lu ms) ===\n", millis());
  log("Last restart: %s\n", resetReasonText());
  log("Expanders: %s\n", ioxOk ? "OK" : "WRITE FAILED");

  // Safe mode: requested before a restart, or a knob held right at power-up.
  // (A knob held for 1 s in the first 6 s also works; see pollInputs.) Pressed reads 0.
  int p0 = read8(IOX_A, 0x00);
  bool requested = safeBootFlag == SAFE_MAGIC;
  safeBootFlag = 0;
  bool knobHeld = p0 >= 0 && (((p0 >> 4) & 1) == 0 || ((p0 >> 5) & 1) == 0);
  safeMode = requested || knobHeld;
  log("Start mode: %s\n", safeMode ? "SAFE MODE (app skipped; WiFi, updates and web page only)" : "normal");

  sdOk = SD_MMC.begin("/sdcard", true);
  log("SD card: %s\n", sdOk ? "OK" : "not found");
  rotateLog();

  write8(GAUGE, 0x0A, 0x00);         // wake the fuel gauge; its first reading takes a moment,
  for (int i = 0; i < 25 && !safeMode; i++) {   // so use that time to keep watching the knobs
    int k = read8(IOX_A, 0x00);
    if (k >= 0 && (((k >> 4) & 1) == 0 || ((k >> 5) & 1) == 0)) {
      safeMode = true;
      log("Knob held: SAFE MODE (app skipped; WiFi, updates and web page only)\n");
    }
    delay(50);
  }
  delay(200);
  pollBattery(true);
  log("Battery %.2f V, %s, %s\n", batteryVolts, inputState.c_str(), chargeState.c_str());

  if (!safeMode && earlyHook) earlyHook();   // e.g. start audio and play the startup sound
  startWifi();
  setupOta();
  setupRoutes();
  server.begin();
  readyMs = millis();
  return safeMode;
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  pollInputs();
  pollBattery();
  saveLog();
}

}  // namespace yoto
