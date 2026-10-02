// Desk pet for plain ESP32 + SSD1306 128x64 OLED (I2C)
// Face engine ported from the ESP32-S3-Zero / ST7789 240x240 version:
// spring-physics eyes, saccades, blinking, breathing, 10 moods, eyelid masks.
// WiFi portal, weather, NTP clock and the colour palette are dropped:
// the OLED is monochrome and the build is pin-mapped to this board.
//
// Wiring:
//   SSD1306 SDA -> GPIO21   SCL -> GPIO22   VCC -> 3V3   GND -> GND
//   TTP223  OUT -> GPIO32
//   MAX4466 OUT -> GPIO35   VCC -> 3V3      GND -> GND
//   HXJ8002 L   -> GPIO25   VCC -> 5V       GND -> GND
//   Motor driver IN -> GPIO27
//
// Gestures: single tap = happy, double tap = round eyes, triple tap = blink,
//           long press = cycle mood.
// Serial 115200: 0-9 set mood, b = blink, d = dump state.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <cmath>
#include "Eye.h"

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RESET -1

#define TOUCH_PIN 32
#define MIC_PIN 35
#define SPEAKER_PIN 25
#define MOTOR_PIN 27

#define EYE_CX_LEFT 36
#define EYE_CX_RIGHT 92
#define EYE_Y 22
#define MOUTH_Y 52
#define SCLERA_PAD 3

#define MIC_SAMPLE_INTERVAL_MS 5
#define MIC_WINDOW_MS 100
#define MIC_MIN_TRIGGER_DELTA 250
#define MIC_MAX_TRIGGER_DELTA 3000
#define MIC_TRIGGER_RATIO 2.5
#define MIC_TRIGGER_MARGIN 200
#define MIC_FLOOR_TRACKING 0.02f
#define MIC_FLOOR_MIN 30.0f
#define MIC_BASELINE_TRACKING 0.01f
#define MIC_STRONG_PEAK_MARGIN 400
#define MIC_HIGH_WINDOW_COUNT 2
#define MIC_SPEAKER_BLANK_MS 800
#define MIC_COOLDOWN_MS 2500
#define MIC_LOG_INTERVAL_MS 5000

#define TOUCH_BEEP_FREQUENCY 1200
#define TOUCH_BEEP_DURATION_MS 120
#define STARTLE_BEEP_FREQUENCY 1600
#define STARTLE_BEEP_DURATION_MS 70
#define STARTLE_BEEP_GAP_MS 40
#define STARTLE_BEEP_COUNT 3

#define TOUCH_MOOD_MS 2600
#define STARTLE_MOOD_MS 1500
#define MANUAL_MOOD_MS 6000
#define BLINK_CLOSE_MS 130
#define FACE_FRAME_MS 40
#define MOTOR_PURR_MS 1000
#define MOTOR_PWM_HZ 20000
#define MOTOR_PWM_BITS 8
#define MOTOR_PURR_DUTY 70

#define LONG_PRESS_MS 800
#define DOUBLE_TAP_MS 400
#define DEBOUNCE_MS 40
#define SERIAL_BAUD 115200

enum Mood {
  MOOD_NORMAL = 0,
  MOOD_HAPPY,
  MOOD_SURPRISED,
  MOOD_SLEEPY,
  MOOD_ANGRY,
  MOOD_SAD,
  MOOD_EXCITED,
  MOOD_LOVE,
  MOOD_SUSPICIOUS,
  MOOD_HEART,
  MOOD_COUNT
};

static const char* moodName(Mood m) {
  switch (m) {
    case MOOD_HAPPY: return "HAPPY";
    case MOOD_SURPRISED: return "SURPRISED";
    case MOOD_SLEEPY: return "SLEEPY";
    case MOOD_ANGRY: return "ANGRY";
    case MOOD_SAD: return "SAD";
    case MOOD_EXCITED: return "EXCITED";
    case MOOD_LOVE: return "LOVE";
    case MOOD_SUSPICIOUS: return "SUSPICIOUS";
    case MOOD_HEART: return "HEART";
    default: return "NORMAL";
  }
}

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

bool oledOk = false;
bool roundEyeMode = false;
Mood mood = MOOD_NORMAL;
unsigned long moodUntil = 0;

Eye leftEye, rightEye;

bool blinking = false;
unsigned long blinkStartedAt = 0;
unsigned long nextBlinkAt = 0;
unsigned long lastSaccadeAt = 0;
unsigned long saccadeInterval = 3000;
float breath = 0.0f;

int micBaseline = 2048;
int micAmbientPeak = 0;
int micAmbientMean = 0;
int micTriggerThreshold = MIC_MIN_TRIGGER_DELTA;
float micNoiseFloor = 0.0f;
int micHighWindows = 0;
int micPeak = 0;
long micWindowDevSum = 0;
long micWindowRawSum = 0;
int micWindowSamples = 0;
unsigned long micSampleAt = 0;
unsigned long micWindowAt = 0;
unsigned long lastNoiseAt = 0;
unsigned long lastToneAt = 0;
unsigned long lastMicLogAt = 0;

int toneFrequency = 0;
int toneDurationMs = 0;
int toneCount = 0;
int toneGapMs = 0;
int toneIndex = 0;
bool toneGapActive = false;
unsigned long toneStepAt = 0;
bool toneActive = false;

unsigned long motorUntil = 0;
bool motorActive = false;

unsigned long lastFaceAt = 0;

void moodBaseSize(Mood m, float breathOffset, float& w, float& h) {
  switch (m) {
    case MOOD_HAPPY:      w = 40.0f; h = 26.0f; break;
    case MOOD_SURPRISED:  w = 40.0f; h = 36.0f; break;
    case MOOD_SLEEPY:     w = 42.0f; h = 26.0f; break;
    case MOOD_ANGRY:      w = 40.0f; h = 28.0f; break;
    case MOOD_SAD:        w = 38.0f; h = 32.0f; break;
    case MOOD_EXCITED:    w = 48.0f; h = 34.0f; break;
    case MOOD_LOVE:       w = 42.0f; h = 34.0f; break;
    case MOOD_SUSPICIOUS: w = 40.0f; h = 30.0f; break;
    case MOOD_HEART:      w = 44.0f; h = 34.0f; break;
    default:              w = 44.0f; h = 30.0f + breathOffset; break;
  }
}

void drawArcManual(int cx, int cy, int radius, float startDeg, float endDeg) {
  const float DEG_TO_RAD_F = 0.0174532925f;
  for (float a = startDeg; a <= endDeg; a += 2.0f) {
    float rad = a * DEG_TO_RAD_F;
    int px = cx + (int)lroundf(cosf(rad) * radius);
    int py = cy + (int)lroundf(sinf(rad) * radius);
    display.drawPixel(px, py, SSD1306_WHITE);
  }
}

void drawHeartShape(int cx, int cy, int size, uint16_t color) {
  int r = size / 4;
  if (r < 1) r = 1;
  display.fillCircle(cx - r, cy - r / 2, r, color);
  display.fillCircle(cx + r, cy - r / 2, r, color);
  display.fillTriangle(cx - 2 * r, cy - r / 2, cx + 2 * r, cy - r / 2, cx, cy + size / 2, color);
}

void drawLidMask(int cx, int cy, int w, int h, Mood m, bool isLeft) {
  if (m == MOOD_HEART) return;

  int x0 = cx - w / 2;
  int y0 = cy - h / 2;
  int cover = (int)(h * 0.45f);

  switch (m) {
    case MOOD_HAPPY:
    case MOOD_LOVE:
    case MOOD_EXCITED:
      display.fillCircle(cx, cy + h / 3, w / 2 + 2, SSD1306_BLACK);
      break;

    case MOOD_SLEEPY:
      display.fillRect(x0 - 2, y0 - 2, w + 4, h / 2 + 3, SSD1306_BLACK);
      break;

    case MOOD_ANGRY:
      if (isLeft) {
        display.fillTriangle(x0 - 2, y0 - 2, x0 + w + 2, y0 - 2, x0 + w + 2, y0 + cover, SSD1306_BLACK);
      } else {
        display.fillTriangle(x0 - 2, y0 - 2, x0 + w + 2, y0 - 2, x0 - 2, y0 + cover, SSD1306_BLACK);
      }
      break;

    case MOOD_SAD:
      if (isLeft) {
        display.fillTriangle(x0 - 2, y0 - 2, x0 + w + 2, y0 - 2, x0 - 2, y0 + cover, SSD1306_BLACK);
      } else {
        display.fillTriangle(x0 - 2, y0 - 2, x0 + w + 2, y0 - 2, x0 + w + 2, y0 + cover, SSD1306_BLACK);
      }
      break;

    case MOOD_SUSPICIOUS:
      if (isLeft) {
        display.fillRect(x0 - 2, y0 - 2, w + 4, h / 2 - 2, SSD1306_BLACK);
      } else {
        display.fillRect(x0 - 2, y0 + h - 8, w + 4, 10, SSD1306_BLACK);
      }
      break;

    default:
      break;
  }
}

void drawEyeAt(int cx, const Eye& e, bool isLeft) {
  int w = (int)e.w;
  int h = (int)e.h;
  if (w < 4) w = 4;
  if (h < 3) h = 3;

  int x0 = cx - w / 2;
  int y0 = EYE_Y - h / 2;
  int r = roundEyeMode ? w / 2 : h / 3;
  if (r < 2) r = 2;

  if (mood == MOOD_HEART) {
    drawHeartShape(cx, EYE_Y, w, SSD1306_WHITE);
    if (w > 20 && h > 18) {
      display.fillCircle(cx + w / 5, EYE_Y - h / 5, max(1, w / 12), SSD1306_BLACK);
    }
    return;
  }

  if (roundEyeMode) {
    display.fillEllipse(cx, EYE_Y, w / 2, h / 2, SSD1306_WHITE);
  } else {
    display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
  }

  if (mood == MOOD_SURPRISED) {
    int pw = max(4, w / 2 - 2);
    int ph = max(4, h / 2 - 2);
    display.fillCircle(cx + (int)e.pupilX, EYE_Y + (int)e.pupilY, min(pw, ph) / 2 + 1, SSD1306_BLACK);
  } else {
    int pw = max(4, w / 2);
    int ph = max(4, h / 2);
    int px = cx + (int)e.pupilX - pw / 2;
    int py = EYE_Y + (int)e.pupilY - ph / 2;

    int minPX = x0 + SCLERA_PAD;
    int maxPX = x0 + w - SCLERA_PAD - pw;
    int minPY = y0 + SCLERA_PAD;
    int maxPY = y0 + h - SCLERA_PAD - ph;
    if (px < minPX) px = minPX;
    if (px > maxPX) px = maxPX;
    if (py < minPY) py = minPY;
    if (py > maxPY) py = maxPY;

    if (roundEyeMode) {
      display.fillEllipse(px + pw / 2, py + ph / 2, pw / 2, ph / 2, SSD1306_BLACK);
    } else {
      display.fillRoundRect(px, py, pw, ph, r / 2 + 1, SSD1306_BLACK);
    }

    if (w > 16 && h > 14) {
      display.fillCircle(px + pw - 3, py + 2, 1, SSD1306_WHITE);
    }
  }

  drawLidMask(cx, EYE_Y, w, h, mood, isLeft);
}

void drawMouth() {
  int cx = SCREEN_W / 2;
  int cy = MOUTH_Y;

  switch (mood) {
    case MOOD_NORMAL:
      display.drawFastHLine(cx - 8, cy, 17, SSD1306_WHITE);
      break;
    case MOOD_HAPPY:
      drawArcManual(cx, cy - 4, 9, 0, 180);
      break;
    case MOOD_EXCITED:
      drawArcManual(cx, cy - 5, 11, 0, 180);
      drawArcManual(cx, cy - 5, 11, 180, 360);
      break;
    case MOOD_SURPRISED:
      display.fillEllipse(cx, cy - 2, 4, 5, SSD1306_WHITE);
      display.fillEllipse(cx, cy - 2, 2, 3, SSD1306_BLACK);
      break;
    case MOOD_SLEEPY:
      display.drawFastHLine(cx - 5, cy, 11, SSD1306_WHITE);
      break;
    case MOOD_ANGRY:
      drawArcManual(cx, cy + 6, 9, 180, 360);
      break;
    case MOOD_SAD:
      drawArcManual(cx, cy + 6, 10, 180, 360);
      break;
    case MOOD_LOVE:
    case MOOD_HEART:
      drawHeartShape(cx, cy - 1, 11, SSD1306_WHITE);
      break;
    case MOOD_SUSPICIOUS:
      for (int i = 0; i < 3; i++) {
        int sx = cx - 9 + i * 7;
        int sy = cy + ((i % 2) ? 1 : -1);
        display.drawFastHLine(sx, sy, 5, SSD1306_WHITE);
      }
      break;
    default:
      break;
  }
}

void drawFace() {
  display.clearDisplay();

  if (mood == MOOD_HEART) {
    drawHeartShape(64, 8, 9, SSD1306_WHITE);
    drawHeartShape(20, 10, 6, SSD1306_WHITE);
    drawHeartShape(108, 10, 6, SSD1306_WHITE);
  } else if (mood == MOOD_SLEEPY) {
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(100, 6);
    display.print("z");
    display.setCursor(106, 2);
    display.print("Z");
  } else if (mood == MOOD_ANGRY) {
    for (int i = 0; i < 5; i++) {
      display.drawFastVLine(20 + i, 8 - i, 6, SSD1306_WHITE);
      display.drawFastVLine(103 + i, 8 - i, 6, SSD1306_WHITE);
    }
  }

  drawEyeAt(EYE_CX_LEFT, leftEye, true);
  drawEyeAt(EYE_CX_RIGHT, rightEye, false);
  drawMouth();

  display.display();
}

void updatePhysics() {
  unsigned long now = millis();
  breath = sinf(now / 900.0f) * 1.5f;

  if (!blinking && now >= nextBlinkAt) {
    blinking = true;
    blinkStartedAt = now;
  }
  if (blinking && now - blinkStartedAt > BLINK_CLOSE_MS) {
    blinking = false;
    nextBlinkAt = now + random(2200, 6500);
  }

  if (!blinking && now - lastSaccadeAt > saccadeInterval) {
    lastSaccadeAt = now;
    saccadeInterval = random(600, 3000);
    int roll = random(0, 10);
    float ox = 0, oy = 0;
    if (roll == 4) { ox = -3; oy = -2; }
    else if (roll == 5) { ox = 3; oy = -2; }
    else if (roll == 6) { ox = -3; oy = 2; }
    else if (roll == 7) { ox = 3; oy = 2; }
    else if (roll == 8) { ox = 4; oy = 0; }
    else if (roll == 9) { ox = -4; oy = 0; }

    leftEye.targetPupilX = ox;
    leftEye.targetPupilY = oy;
    rightEye.targetPupilX = ox;
    rightEye.targetPupilY = oy;
  }

  float bw, bh;
  moodBaseSize(mood, breath, bw, bh);

  if (blinking) {
    leftEye.targetW = bw;
    leftEye.targetH = 3;
    rightEye.targetW = bw;
    rightEye.targetH = 3;
  } else {
    leftEye.targetW = bw;
    leftEye.targetH = bh;
    rightEye.targetW = bw;
    rightEye.targetH = (mood == MOOD_SUSPICIOUS) ? bh + 6.0f : bh;
  }

  leftEye.update();
  rightEye.update();
}

void startTone(int frequency, int durationMs, int count, int gapMs) {
  toneFrequency = frequency;
  toneDurationMs = durationMs;
  toneCount = count;
  toneGapMs = gapMs;
  toneIndex = 0;
  toneGapActive = false;
  toneStepAt = millis();
  toneActive = true;
  lastToneAt = millis();
  ledcWriteTone(SPEAKER_PIN, frequency);
}

void updateTone() {
  if (!toneActive) return;
  if (millis() - toneStepAt < (unsigned long)toneDurationMs) return;

  toneIndex++;
  if (toneIndex >= toneCount) {
    ledcWriteTone(SPEAKER_PIN, 0);
    toneActive = false;
    return;
  }

  if (!toneGapActive) {
    ledcWriteTone(SPEAKER_PIN, 0);
    toneGapActive = true;
    toneStepAt = millis() + toneGapMs;
  } else {
    ledcWriteTone(SPEAKER_PIN, toneFrequency);
    toneGapActive = false;
    toneStepAt = millis();
  }
}

void purr() {
  motorActive = true;
  motorUntil = millis() + MOTOR_PURR_MS;
  ledcWrite(MOTOR_PIN, (1 << MOTOR_PWM_BITS) * MOTOR_PURR_DUTY / 100);
}

void updateMotor() {
  if (!motorActive) return;
  if (millis() >= motorUntil) {
    ledcWrite(MOTOR_PIN, 0);
    motorActive = false;
  }
}

void setMood(Mood next, unsigned long holdMs, const char* reason) {
  mood = next;
  moodUntil = holdMs > 0 ? millis() + holdMs : 0;
  Serial.printf("Mood %s: %s\n", moodName(mood), reason);
}

void updateMood() {
  if (mood == MOOD_NORMAL || moodUntil == 0) return;
  if (millis() >= moodUntil) {
    mood = MOOD_NORMAL;
    moodUntil = 0;
    Serial.println("Mood NORMAL: expired");
  }
}

void computeMicThreshold() {
  micTriggerThreshold = (int)(micNoiseFloor * MIC_TRIGGER_RATIO) + MIC_TRIGGER_MARGIN;
  if (micTriggerThreshold < (int)micNoiseFloor + 150) {
    micTriggerThreshold = (int)micNoiseFloor + 150;
  }
  if (micTriggerThreshold < MIC_MIN_TRIGGER_DELTA) micTriggerThreshold = MIC_MIN_TRIGGER_DELTA;
  if (micTriggerThreshold > MIC_MAX_TRIGGER_DELTA) micTriggerThreshold = MIC_MAX_TRIGGER_DELTA;
}

void calibrateMic() {
  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);
  int samples[200];
  long total = 0;
  int rawMin = 4095;
  int rawMax = 0;

  for (int i = 0; i < 200; i++) {
    int s = analogRead(MIC_PIN);
    samples[i] = s;
    total += s;
    if (s < rawMin) rawMin = s;
    if (s > rawMax) rawMax = s;
    delay(10);
  }

  micBaseline = total / 200;
  micAmbientPeak = 0;
  long deviationTotal = 0;
  for (int i = 0; i < 200; i++) {
    int deviation = samples[i] - micBaseline;
    if (deviation < 0) deviation = -deviation;
    deviationTotal += deviation;
    if (deviation > micAmbientPeak) micAmbientPeak = deviation;
  }

  micAmbientMean = (int)(deviationTotal / 200);
  micNoiseFloor = micAmbientMean;
  if (micNoiseFloor < MIC_FLOOR_MIN) micNoiseFloor = MIC_FLOOR_MIN;
  computeMicThreshold();

  Serial.printf("mic raw min=%d max=%d dc=%d meanDev=%d peakDev=%d\n",
                rawMin, rawMax, micBaseline, micAmbientMean, micAmbientPeak);
  if (rawMax >= 4090 || rawMin <= 5) {
    Serial.println("WARNING: mic output is clipping at the ADC rail");
  }
}

void pollMic(unsigned long now) {
  if (now - micSampleAt >= MIC_SAMPLE_INTERVAL_MS) {
    micSampleAt = now;
    int sample = analogRead(MIC_PIN);
    int deviation = sample - micBaseline;
    if (deviation < 0) deviation = -deviation;
    if (deviation > micPeak) micPeak = deviation;
    micWindowDevSum += deviation;
    micWindowRawSum += sample;
    micWindowSamples++;
  }

  if (now - micWindowAt < MIC_WINDOW_MS) return;

  int peak = micPeak;
  int meanDev = micWindowSamples > 0 ? (int)(micWindowDevSum / micWindowSamples) : 0;
  float meanRaw = micWindowSamples > 0 ? (float)micWindowRawSum / micWindowSamples : (float)micBaseline;
  micPeak = 0;
  micWindowDevSum = 0;
  micWindowRawSum = 0;
  micWindowSamples = 0;
  micWindowAt = now;

  if (peak >= micTriggerThreshold) {
    micHighWindows++;
  } else {
    micHighWindows = 0;
  }

  bool strongPeak = peak >= micTriggerThreshold + MIC_STRONG_PEAK_MARGIN;
  bool sustainedPeak = micHighWindows >= MIC_HIGH_WINDOW_COUNT;
  bool speakerClear = now - lastToneAt >= MIC_SPEAKER_BLANK_MS;
  bool cooldownClear = lastNoiseAt == 0 || now - lastNoiseAt >= MIC_COOLDOWN_MS;

  if ((strongPeak || sustainedPeak) && speakerClear && cooldownClear) {
    micHighWindows = 0;
    lastNoiseAt = now;
    Serial.printf("Mic peak=%d meanDev=%d threshold=%d floor=%d\n",
                  peak, meanDev, micTriggerThreshold, (int)micNoiseFloor);
    setMood(MOOD_SURPRISED, STARTLE_MOOD_MS, "loud noise");
    startTone(STARTLE_BEEP_FREQUENCY, STARTLE_BEEP_DURATION_MS, STARTLE_BEEP_COUNT, STARTLE_BEEP_GAP_MS);
  } else if (speakerClear) {
    micNoiseFloor += ((float)meanDev - micNoiseFloor) * MIC_FLOOR_TRACKING;
    if (micNoiseFloor < MIC_FLOOR_MIN) micNoiseFloor = MIC_FLOOR_MIN;
    micBaseline += (int)((meanRaw - (float)micBaseline) * MIC_BASELINE_TRACKING);
    if (micBaseline < 0) micBaseline = 0;
    if (micBaseline > 4095) micBaseline = 4095;
    computeMicThreshold();
  }

  if (now - lastMicLogAt >= MIC_LOG_INTERVAL_MS) {
    lastMicLogAt = now;
    Serial.printf("mic dc=%d floor=%d threshold=%d\n", micBaseline, (int)micNoiseFloor, micTriggerThreshold);
  }
}

int tapCount = 0;
unsigned long lastTapAt = 0;
bool touchHeld = false;
bool longPressFired = false;
unsigned long pressStartedAt = 0;
unsigned long lastEdgeAt = 0;

void handleTouch(unsigned long now) {
  bool level = digitalRead(TOUCH_PIN);

  if (level != touchHeld && now - lastEdgeAt >= DEBOUNCE_MS) {
    lastEdgeAt = now;
    touchHeld = level;
  }

  if (touchHeld) {
    if (!longPressFired && now - pressStartedAt > LONG_PRESS_MS) {
      longPressFired = true;
      tapCount = 0;
      Mood next = (Mood)((mood + 1) % MOOD_COUNT);
      setMood(next, MANUAL_MOOD_MS, "long press");
    }
    return;
  }

  if (now - pressStartedAt < LONG_PRESS_MS || longPressFired) {
    longPressFired = false;
    pressStartedAt = now;
    return;
  }

  tapCount++;
  lastTapAt = now;
  pressStartedAt = now;

  if (tapCount == 1) {
    setMood(MOOD_HAPPY, TOUCH_MOOD_MS, "touch");
    startTone(TOUCH_BEEP_FREQUENCY, TOUCH_BEEP_DURATION_MS, 1, 0);
    purr();
  } else if (tapCount == 2) {
    roundEyeMode = !roundEyeMode;
    Serial.printf("Round eyes %s\n", roundEyeMode ? "on" : "off");
  } else if (tapCount >= 3) {
    blinking = false;
    blinkStartedAt = now;
    nextBlinkAt = now + BLINK_CLOSE_MS + random(2200, 6500);
    Serial.println("Blink forced");
  }
}

void resolveTaps(unsigned long now) {
  if (tapCount == 0) return;
  if (now - lastTapAt < DOUBLE_TAP_MS) return;

  if (tapCount == 2) {
    roundEyeMode = !roundEyeMode;
    Serial.printf("Round eyes %s\n", roundEyeMode ? "on" : "off");
  } else if (tapCount >= 3) {
    nextBlinkAt = now;
    Serial.println("Blink forced");
  }
  tapCount = 0;
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {
      setMood((Mood)(c - '0'), MANUAL_MOOD_MS, "serial");
    } else if (c == 'b') {
      nextBlinkAt = millis();
      Serial.println("Blink forced");
    } else if (c == 'd') {
      Serial.printf("mood=%s round=%d micBaseline=%d micFloor=%d micThreshold=%d\n",
                    moodName(mood), roundEyeMode, micBaseline, (int)micNoiseFloor, micTriggerThreshold);
    }
  }
}

bool findOled() {
  for (int attempt = 0; attempt < 5; attempt++) {
    for (uint8_t address = 0x3C; address <= 0x3D; address++) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0) {
        oledOk = display.begin(SSD1306_SWITCHCAPVCC, address);
        if (oledOk) return true;
      }
    }
    delay(100);
  }
  return false;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  randomSeed(micros());

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000L);
  oledOk = findOled();

  pinMode(TOUCH_PIN, INPUT);
  pinMode(MIC_PIN, INPUT);

  ledcAttach(SPEAKER_PIN, 1000, 10);
  ledcWriteTone(SPEAKER_PIN, 0);
  ledcAttach(MOTOR_PIN, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
  ledcWrite(MOTOR_PIN, 0);

  leftEye.init();
  rightEye.init();

  calibrateMic();
  nextBlinkAt = millis() + 2000;
  lastSaccadeAt = millis();
  lastFaceAt = millis();
  micWindowAt = millis();

  Serial.println("Desk pet face engine on SSD1306 128x64");
  Serial.printf("OLED=%s touch=GPIO%d mic=GPIO%d speaker L=GPIO%d motor=GPIO%d\n",
                oledOk ? "ready" : "missing", TOUCH_PIN, MIC_PIN, SPEAKER_PIN, MOTOR_PIN);
  Serial.printf("mic baseline=%d floor=%d threshold=%d\n",
                micBaseline, (int)micNoiseFloor, micTriggerThreshold);
  Serial.println("Tap=happy double=toggle round triple=blink hold=cycle mood");
  Serial.println("Serial: 0-9 mood, b blink, d dump");

  if (oledOk) drawFace();
}

void loop() {
  unsigned long now = millis();

  updateTone();
  updateMood();
  updateMotor();
  updatePhysics();
  handleTouch(now);
  resolveTaps(now);
  pollMic(now);
  handleSerial();

  if (oledOk && now - lastFaceAt >= FACE_FRAME_MS) {
    lastFaceAt = now;
    drawFace();
  }
}
