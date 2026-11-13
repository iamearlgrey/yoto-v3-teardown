// CAL's face on the 16x16 display: two spirit-level eyes.
//
// Each eye is a little level vial; the bubble is the pupil. The accelerometer
// (LIS2DH12 at 0x18) moves the bubbles like a real level: tip the Yoto and they
// drift to the high side. Expressions: idle, listening, thinking, speaking,
// pleased, indignant, nostalgic, asleep.
//
// Runs in its own task at about 25 frames a second and only sends the display
// chips whose pixels changed. Drawing on the web page's grid or using the display
// test tools pauses the face until a face state is picked again.
//
// Included from yoto_base.h after yoto_display.h.
#pragma once

namespace yoto {
namespace face {

enum State { IDLE, LISTENING, THINKING, SPEAKING, PLEASED, INDIGNANT, NOSTALGIC, ASLEEP, STATE_COUNT };
const char *const NAMES[STATE_COUNT] = {"idle", "listening", "thinking", "speaking",
                                        "pleased", "indignant", "nostalgic", "asleep"};

volatile State state = IDLE;
volatile bool autoMode = true;     // the app may switch idle <-> speaking while sound plays
volatile bool manual = false;      // true while the drawing grid or test tools own the display
volatile float level = 0;          // sound level 0-1, set by the audio task
bool running = false;

// ---- Accelerometer ----
const uint8_t ACC = 0x18;
bool accOk = false;
float acc[3] = {0, 0, 1};          // filtered, in g
float calLevel[3] = {0, 0, 1};     // gravity when sitting level
float calAxis[3] = {1, 0, 0};      // direction gravity moves when the right side goes down
bool calOk = false;
float tilt = 0;                    // + = right side down, roughly sin(angle)

bool accBegin() {
  if (read8(ACC, 0x0F) != 0x33) { log("Accelerometer: no LIS2DH12 at 0x18.\n"); return false; }
  write8(ACC, 0x20, 0x47);         // 50 Hz, X Y Z on
  write8(ACC, 0x23, 0x88);         // block data update, high resolution, +-2 g
  return true;
}

bool accRead(float out[3]) {
  Wire.beginTransmission(ACC);
  Wire.write(0x28 | 0x80);         // OUT_X_L, auto-increment
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)ACC, 6) != 6) return false;
  for (int i = 0; i < 3; i++) {
    int16_t v = Wire.read();
    v |= Wire.read() << 8;
    out[i] = v / 16000.0f;         // left-justified 12-bit, 1 mg per step
  }
  return true;
}

float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

bool loadCal() {
  File f = SD_MMC.open("/face.txt");
  if (!f) return false;
  String s = f.readString();
  f.close();
  float v[6];
  if (sscanf(s.c_str(), "level %f %f %f axis %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 3; i++) { calLevel[i] = v[i]; calAxis[i] = v[3 + i]; }
  return true;
}

bool saveCal() {
  File f = SD_MMC.open("/face.txt", FILE_WRITE);
  if (!f) return false;
  f.printf("level %.4f %.4f %.4f axis %.4f %.4f %.4f\n", calLevel[0], calLevel[1], calLevel[2],
           calAxis[0], calAxis[1], calAxis[2]);
  f.close();
  return true;
}

// Step "level": sitting level as normal. Step "right": right side tipped down.
String calibrate(const String &step) {
  if (!accOk) return "No accelerometer";
  if (step == "level") {
    for (int i = 0; i < 3; i++) calLevel[i] = acc[i];
    return "Level recorded. Now tip the right side down and press Right side down.";
  }
  float d[3], n = 0;
  for (int i = 0; i < 3; i++) { d[i] = acc[i] - calLevel[i]; n += d[i] * d[i]; }
  n = sqrtf(n);
  if (n < 0.15f) return "Not tipped enough: tip it further (about 20 degrees) and try again.";
  for (int i = 0; i < 3; i++) calAxis[i] = d[i] / n;
  calOk = true;
  bool saved = sdOk && saveCal();
  log("Face: level calibrated (axis %.2f %.2f %.2f)%s.\n", calAxis[0], calAxis[1], calAxis[2], saved ? ", saved" : "");
  return saved ? "Calibrated and saved." : "Calibrated (couldn't save /face.txt).";
}

// ---- Drawing ----
float fb[16][16][3];

struct Col { float r, g, b; };
const Col TEAL = {70, 170, 160}, LIME = {210, 255, 70}, AMBER = {255, 150, 30}, ORANGE = {255, 80, 15};

void put(int x, int y, Col c, float a) {
  if (x < 0 || x > 15 || y < 0 || y > 15 || a <= 0) return;
  float *p = fb[y][x];
  p[0] = fmaxf(p[0], c.r * a);
  p[1] = fmaxf(p[1], c.g * a);
  p[2] = fmaxf(p[2], c.b * a);
}

// A vial w wide and h tall at (x0, top), slanted by slope rows per column from its
// middle. The bubble is 2 wide at position bx (0 = left end, w-4 = right end),
// drawn with soft edges so it slides smoothly between pixels. bx < 0: no bubble.
void vial(int x0, int top, int w, int h, float slope, float bx, Col oc, Col bc, float a) {
  auto dy = [&](int i) { return (int)lroundf(slope * (i - (w - 1) / 2.0f)); };
  for (int i = 0; i < w; i++) {
    int d = dy(i);
    if (i > 0 && i < w - 1) { put(x0 + i, top + d, oc, a); put(x0 + i, top + h - 1 + d, oc, a); }
    else for (int r = 1; r < h - 1; r++) put(x0 + i, top + r + d, oc, a);
  }
  if (bx < 0) return;
  bx = constrain(bx, 0.0f, (float)(w - 4));
  for (int i = 1; i < w - 1; i++) {      // interior column i covers [i, i+1); bubble covers [bx+1, bx+3)
    float cover = fminf(i + 1, bx + 3) - fmaxf(i, bx + 1);
    if (cover <= 0) continue;
    for (int r = 1; r < h - 1; r++) put(x0 + i, top + r + dy(i), bc, a * fminf(cover, 1));
  }
}

// ---- Bubble motion: a damped spring per eye, so they slosh a little ----
struct Bubble { float pos = 1.5f, vel = 0; };
Bubble bub[2];

void moveBubble(Bubble &b, float target, float dt) {
  float k = 70, damp = 9;
  b.vel += ((target - b.pos) * k - b.vel * damp) * dt;
  b.pos += b.vel * dt;
  if (b.pos < -0.5f) { b.pos = -0.5f; b.vel = 0; }   // the ends of the vial
  if (b.pos > 3.5f) { b.pos = 3.5f; b.vel = 0; }
}

// ---- One frame ----
uint32_t nextBlink = 3000, blinkUntil = 0, nextGlance = 6000, glanceUntil = 0;
float glance = 0, levelSmooth = 0;

void render(uint32_t now, float t, float dt) {
  memset(fb, 0, sizeof(fb));
  const int W = 7, L = 0, R = 9;
  const float C = 1.5f;              // centre bubble position
  float tb = -tilt * 3.2f;           // bubble runs to the high side
  State s = state;

  // speaking: fast attack, slow release
  float lv = level;
  levelSmooth = lv > levelSmooth ? lv : levelSmooth * 0.85f;

  float targetL = C + tb, targetR = C + tb;
  if (s == IDLE) {
    if (now > nextGlance) {
      glance = (esp_random() % 3) - 1.0f;          // left, centre or right
      glanceUntil = now + 900 + esp_random() % 1500;
      nextGlance = glanceUntil + 4000 + esp_random() % 7000;
    }
    float g = now < glanceUntil ? glance * 1.4f : 0;
    float drift = sinf(t * 0.7f) * 0.35f;
    targetL += g + drift; targetR += g + drift;
  } else if (s == THINKING) {
    targetL += sinf(t * 1.3f) * 1.4f;
    targetR += sinf(t * 1.3f + 2.0f) * 1.4f;
  } else if (s == SPEAKING) {
    float j = sinf(t * 17) * levelSmooth * 1.2f;
    targetL += j; targetR -= j;
  } else if (s == NOSTALGIC) {
    targetL = targetR = 0;                         // gazing off to the left
  } else if (s == INDIGNANT) {
    float sh = sinf(t * 25) * 0.4f;
    targetL += sh; targetR += sh;
  }
  moveBubble(bub[0], targetL, dt);
  moveBubble(bub[1], targetR, dt);
  // Draw the bubbles on whole pixels, and only step once a bubble has clearly moved,
  // so one hovering between two pixels doesn't shimmer.
  static float shown[2] = {1.5f, 1.5f};
  for (int e = 0; e < 2; e++) {
    float p = constrain(bub[e].pos, 0.0f, 3.0f);
    if (fabsf(p - shown[e]) > 0.7f || shown[e] == 1.5f) shown[e] = roundf(p);
  }
  float bl = shown[0], br = shown[1];

  // blinking, in the states with open eyes
  bool canBlink = s == IDLE || s == LISTENING || s == THINKING || s == SPEAKING;
  if (canBlink && now > nextBlink) { blinkUntil = now + 140; nextBlink = now + 2500 + esp_random() % 4500; }
  if (canBlink && now < blinkUntil) {
    for (int i = 1; i < W - 1; i++) { put(L + i, 8, TEAL, 0.7f); put(R + i, 8, TEAL, 0.7f); }
    return;
  }

  switch (s) {
    case IDLE:
      vial(L, 6, W, 4, 0, bl, TEAL, LIME, 1); vial(R, 6, W, 4, 0, br, TEAL, LIME, 1);
      break;
    case LISTENING: {
      float p = 0.85f + 0.15f * sinf(t * 3);
      vial(L, 5, W, 6, 0, bl, TEAL, LIME, p); vial(R, 5, W, 6, 0, br, TEAL, LIME, p);
      break;
    }
    case THINKING:
      vial(L, 4, W, 4, 0, bl, TEAL, LIME, 0.9f); vial(R, 4, W, 4, 0, br, TEAL, LIME, 0.9f);
      break;
    case SPEAKING: {
      vial(L, 5, W, 4, 0, bl, TEAL, LIME, 1); vial(R, 5, W, 4, 0, br, TEAL, LIME, 1);
      for (int x = 5; x <= 10; x++) put(x, 12, TEAL, 0.35f);     // the mouth: a small level bar
      int half = (int)lroundf(fminf(levelSmooth * 4.0f, 1.0f) * 3);
      for (int i = 0; i < half; i++) { put(7 - i, 12, LIME, 1); put(8 + i, 12, LIME, 1); }
      break;
    }
    case PLEASED:
      for (int i = 0; i < W; i++) {
        int y = 8 - (int)lroundf((3 - abs(i - 3)) * 0.67f);
        put(L + i, y, LIME, 1); put(R + i, y, LIME, 1);
      }
      break;
    case INDIGNANT:
      vial(L, 6, W, 4, 0.5f, bl, AMBER, ORANGE, 1); vial(R, 6, W, 4, -0.5f, br, AMBER, ORANGE, 1);
      break;
    case NOSTALGIC: {
      float a = 0.45f + 0.1f * sinf(t * 0.8f);
      vial(L, 7, W, 4, 0, bl, TEAL, LIME, a); vial(R, 7, W, 4, 0, br, TEAL, LIME, a);
      break;
    }
    case ASLEEP: {
      float a = 0.3f + 0.3f * (1 + sinf(t * 1.4f)) / 2;
      for (int i = 1; i < W - 1; i++) { put(L + i, 9, TEAL, a); put(R + i, 9, TEAL, a); }
      break;
    }
    default: break;
  }
}

void task(void *) {
  TickType_t wake = xTaskGetTickCount();
  uint32_t last = millis();
  uint32_t lastAcc = 0;
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(40));
    uint32_t now = millis();
    float dt = fminf((now - last) / 1000.0f, 0.1f);
    last = now;
    if (accOk && now - lastAcc >= 40) {
      lastAcc = now;
      float a[3];
      if (accRead(a)) for (int i = 0; i < 3; i++) acc[i] += (a[i] - acc[i]) * 0.3f;
      float tl = dot3(acc, calAxis) - dot3(calLevel, calAxis);
      tilt = constrain(tl, -1.0f, 1.0f);
    }
    if (manual || !display::ok) continue;
    render(now, now / 1000.0f, dt);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++)
        display::pixel(x, y, (uint8_t)fminf(fb[y][x][0], 255), (uint8_t)fminf(fb[y][x][1], 255),
                       (uint8_t)fminf(fb[y][x][2], 255));
    display::flushChanged();
  }
}

void set(State s) {
  if (s >= STATE_COUNT) return;
  state = s;
  manual = false;
}

int stateFromName(const String &n) {
  for (int i = 0; i < STATE_COUNT; i++) if (n == NAMES[i]) return i;
  return -1;
}

void begin() {
  if (running || !display::ok) return;
  accOk = accBegin();
  if (accOk) {
    delay(30);
    accRead(acc);
    calOk = sdOk && loadCal();
    if (!calOk) {                    // assume it's level now; guess a sideways axis
      for (int i = 0; i < 3; i++) calLevel[i] = acc[i];
      int up = 0;
      for (int i = 1; i < 3; i++) if (fabsf(acc[i]) > fabsf(acc[up])) up = i;
      for (int i = 0; i < 3; i++) calAxis[i] = 0;
      calAxis[(up + 1) % 3] = 1;     // a guess: calibrate to be sure
    }
    log("Accelerometer: %.2f %.2f %.2f g, level %s.\n", acc[0], acc[1], acc[2],
        calOk ? "calibration loaded" : "not calibrated (using start-up position)");
  }
  running = true;
  xTaskCreatePinnedToCore(task, "face", 4096, nullptr, 1, nullptr, 0);
  log("Face: spirit-level eyes running.\n");
}

String json() {
  char b[200];
  snprintf(b, sizeof(b),
           "{\"state\":\"%s\",\"auto\":%s,\"manual\":%s,\"acc\":[%.2f,%.2f,%.2f],\"tilt\":%.2f,\"cal\":%s,\"accOk\":%s}",
           NAMES[state], autoMode ? "true" : "false", manual ? "true" : "false", acc[0], acc[1], acc[2], tilt,
           calOk ? "true" : "false", accOk ? "true" : "false");
  return b;
}

}  // namespace face
}  // namespace yoto
