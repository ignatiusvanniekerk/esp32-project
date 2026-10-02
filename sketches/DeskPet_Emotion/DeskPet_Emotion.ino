#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 21
#define OLED_SCL 22
#define TOUCH_PIN 32
#define MIC_PIN 35
#define SPEAKER_PIN 25
#define MOTOR_PIN 27

#define MIC_SAMPLE_INTERVAL_MS 5
#define MIC_WINDOW_MS 100
#define MIC_MIN_TRIGGER_DELTA 250
#define MIC_NOISE_MARGIN 450
#define MIC_MAX_TRIGGER_DELTA 1200
#define MIC_STRONG_PEAK_MARGIN 300
#define MIC_HIGH_WINDOW_COUNT 2
#define MIC_SPEAKER_BLANK_MS 800
#define MIC_COOLDOWN_MS 2500

#define TOUCH_BEEP_FREQUENCY 1200
#define TOUCH_BEEP_DURATION_MS 120
#define STARTLE_BEEP_FREQUENCY 1600
#define STARTLE_BEEP_DURATION_MS 70
#define STARTLE_BEEP_GAP_MS 40
#define STARTLE_BEEP_COUNT 3

#define TOUCH_MOOD_MS 2600
#define STARTLE_MOOD_MS 1500
#define CURIOUS_MOOD_MS 2000
#define BLINK_CLOSE_MS 130
#define FACE_FRAME_MS 80
#define MOTOR_PURR_MS 1000
#define MOTOR_PWM_HZ 20000
#define MOTOR_PWM_BITS 8
#define MOTOR_PURR_DUTY 70

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool oledOk = false;
bool touchWasDown = false;
bool eyesClosed = false;

enum Mood { MOOD_IDLE, MOOD_HAPPY, MOOD_SURPRISED, MOOD_CURIOUS };

Mood mood = MOOD_IDLE;
unsigned long moodUntil = 0;
unsigned long blinkStartedAt = 0;
unsigned long nextBlinkAt = 1500;
unsigned long nextGazeAt = 0;
unsigned long lastFaceAt = 0;
int gazeX = 0;
int gazeY = 0;

int micBaseline = 2048;
int micAmbientPeak = 0;
int micTriggerThreshold = MIC_MIN_TRIGGER_DELTA;
int micHighWindows = 0;
int micPeak = 0;
unsigned long micSampleAt = 0;
unsigned long micWindowAt = 0;
unsigned long lastNoiseAt = 0;
unsigned long lastToneAt = 0;

unsigned long motorUntil = 0;
unsigned long toneUntil = 0;
uint32_t toneFrequency = 0;
uint32_t toneDuration = 0;
uint32_t toneGap = 0;
uint8_t toneCount = 0;
uint8_t toneIndex = 0;
bool toneGapActive = false;

const char* moodName(Mood value) {
  switch (value) {
    case MOOD_HAPPY: return "HAPPY";
    case MOOD_SURPRISED: return "SURPRISED";
    case MOOD_CURIOUS: return "CURIOUS";
    default: return "IDLE";
  }
}

void setMood(Mood nextMood, unsigned long duration, const char* reason) {
  mood = nextMood;
  moodUntil = duration == 0 ? 0 : millis() + duration;
  if (mood != MOOD_HAPPY) motorUntil = 0;
  Serial.printf("Mood %s: %s\n", moodName(mood), reason);
}

void updateMood(unsigned long now) {
  if (mood != MOOD_IDLE && moodUntil != 0 && now >= moodUntil) {
    setMood(MOOD_IDLE, 0, "expired");
  }
}

void startTone(uint32_t frequency, uint32_t duration, uint8_t count, uint32_t gap) {
  toneFrequency = frequency;
  toneDuration = duration;
  toneGap = gap;
  toneCount = count;
  toneIndex = 0;
  toneGapActive = false;
  lastToneAt = millis();
  ledcWriteTone(SPEAKER_PIN, frequency);
  toneUntil = millis() + duration;
}

void updateTone(unsigned long now) {
  if (toneCount == 0 || now < toneUntil) return;

  if (!toneGapActive) {
    ledcWriteTone(SPEAKER_PIN, 0);
    if (toneIndex + 1 >= toneCount) {
      toneCount = 0;
    } else {
      toneGapActive = true;
      toneUntil = now + toneGap;
    }
  } else {
    toneGapActive = false;
    toneIndex++;
    ledcWriteTone(SPEAKER_PIN, toneFrequency);
    toneUntil = now + toneDuration;
  }
}

void reactHappy(unsigned long now, const char* reason) {
  lastNoiseAt = now;
  setMood(MOOD_HAPPY, TOUCH_MOOD_MS, reason);
  motorUntil = now + MOTOR_PURR_MS;
  startTone(TOUCH_BEEP_FREQUENCY, TOUCH_BEEP_DURATION_MS, 1, 0);
}

void reactStartled(unsigned long now, const char* reason) {
  lastNoiseAt = now;
  setMood(MOOD_SURPRISED, STARTLE_MOOD_MS, reason);
  startTone(STARTLE_BEEP_FREQUENCY, STARTLE_BEEP_DURATION_MS, STARTLE_BEEP_COUNT, STARTLE_BEEP_GAP_MS);
}

void updateMotor(unsigned long now) {
  if (now < motorUntil && mood == MOOD_HAPPY) {
    ledcWrite(MOTOR_PIN, MOTOR_PURR_DUTY);
  } else {
    ledcWrite(MOTOR_PIN, 0);
  }
}

void updateBlink(unsigned long now) {
  if (eyesClosed) {
    if (now - blinkStartedAt >= BLINK_CLOSE_MS) {
      eyesClosed = false;
      nextBlinkAt = now + random(2200, 4800);
    }
  } else if (now >= nextBlinkAt) {
    eyesClosed = true;
    blinkStartedAt = now;
  }
}

void updateGaze(unsigned long now) {
  if (now >= nextGazeAt) {
    nextGazeAt = now + random(1200, 3000);
    gazeX = random(-2, 3);
    gazeY = random(-1, 2);
  }
}

void drawEye(int x, int y, int shiftX, int shiftY) {
  if (mood == MOOD_HAPPY) {
    display.drawLine(x - 8, y + 3, x - 3, y - 3, SSD1306_WHITE);
    display.drawLine(x - 3, y - 3, x + 3, y - 3, SSD1306_WHITE);
    display.drawLine(x + 3, y - 3, x + 8, y + 3, SSD1306_WHITE);
    return;
  }

  if (eyesClosed) {
    display.drawLine(x - 8, y, x + 8, y, SSD1306_WHITE);
    return;
  }

  int radius = mood == MOOD_SURPRISED ? 10 : 8;
  int pupil = mood == MOOD_SURPRISED ? 4 : 3;
  display.drawCircle(x, y, radius, SSD1306_WHITE);
  display.fillCircle(x + shiftX, y + shiftY, pupil, SSD1306_WHITE);
}

void drawMouth() {
  int centerX = 64;
  int centerY = 49;

  if (mood == MOOD_HAPPY) {
    for (int x = -8; x <= 8; x++) {
      int edge = x < 0 ? -x : x;
      display.drawPixel(centerX + x, centerY - 3 + (8 - edge) / 2, SSD1306_WHITE);
    }
  } else if (mood == MOOD_SURPRISED) {
    display.drawCircle(centerX, centerY, 4, SSD1306_WHITE);
  } else if (mood == MOOD_CURIOUS) {
    display.drawLine(centerX - 5, centerY - 2, centerX, centerY + 2, SSD1306_WHITE);
    display.drawLine(centerX, centerY + 2, centerX + 5, centerY - 2, SSD1306_WHITE);
  } else {
    display.drawLine(centerX - 5, centerY, centerX + 5, centerY, SSD1306_WHITE);
  }
}

void drawFace() {
  display.clearDisplay();
  drawEye(42, 25, gazeX, gazeY);
  drawEye(86, 25, gazeX, gazeY);
  if (mood == MOOD_CURIOUS) {
    display.drawLine(34, 14, 44, 12, SSD1306_WHITE);
    display.drawLine(84, 12, 94, 14, SSD1306_WHITE);
  }
  drawMouth();
  display.display();
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

void calibrateMic() {
  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);
  int samples[100];
  long total = 0;
  for (int i = 0; i < 100; i++) {
    samples[i] = analogRead(MIC_PIN);
    total += samples[i];
    delay(10);
  }

  micBaseline = total / 100;
  micAmbientPeak = 0;
  for (int i = 0; i < 100; i++) {
    int deviation = samples[i] - micBaseline;
    if (deviation < 0) deviation = -deviation;
    if (deviation > micAmbientPeak) micAmbientPeak = deviation;
  }

  micTriggerThreshold = micAmbientPeak + MIC_NOISE_MARGIN;
  if (micTriggerThreshold < MIC_MIN_TRIGGER_DELTA) micTriggerThreshold = MIC_MIN_TRIGGER_DELTA;
  if (micTriggerThreshold > MIC_MAX_TRIGGER_DELTA) micTriggerThreshold = MIC_MAX_TRIGGER_DELTA;
}

void pollTouch(unsigned long now) {
  bool down = digitalRead(TOUCH_PIN) == HIGH;
  if (down && !touchWasDown) {
    reactHappy(now, "touch");
  }
  touchWasDown = down;
}

void pollMic(unsigned long now) {
  if (now - micSampleAt >= MIC_SAMPLE_INTERVAL_MS) {
    micSampleAt = now;
    int sample = analogRead(MIC_PIN);
    int deviation = sample - micBaseline;
    if (deviation < 0) deviation = -deviation;
    if (deviation > micPeak) micPeak = deviation;
  }

  if (now - micWindowAt >= MIC_WINDOW_MS) {
    int peak = micPeak;
    micPeak = 0;
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
      Serial.printf("Mic peak=%d threshold=%d\n", peak, micTriggerThreshold);
      reactStartled(now, "loud noise");
    }
  }
}

void handleSerial() {
  if (!Serial.available()) return;
  char command = Serial.read();
  if (command == 'h') reactHappy(millis(), "serial h");
  if (command == 's') reactStartled(millis(), "serial s");
  if (command == 'c') setMood(MOOD_CURIOUS, CURIOUS_MOOD_MS, "serial c");
  if (command == 'b') {
    eyesClosed = true;
    blinkStartedAt = millis();
  }
}

void setup() {
  Serial.begin(115200);
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

  calibrateMic();
  nextBlinkAt = millis() + 1500;
  nextGazeAt = millis() + 1000;
  lastFaceAt = millis();
  micWindowAt = millis();

  Serial.println("Desk pet emotion firmware");
  Serial.printf("OLED=%s touch=GPIO%d mic=GPIO%d speaker L=GPIO%d motor=GPIO%d\n",
                oledOk ? "ready" : "missing", TOUCH_PIN, MIC_PIN, SPEAKER_PIN, MOTOR_PIN);
  Serial.printf("mic baseline=%d ambient=%d threshold=%d\n",
                micBaseline, micAmbientPeak, micTriggerThreshold);
  Serial.println("Commands: h=happy s=surprised c=curious b=blink");

  if (oledOk) drawFace();
}

void loop() {
  unsigned long now = millis();
  updateTone(now);
  updateMood(now);
  updateMotor(now);
  updateBlink(now);
  updateGaze(now);
  pollTouch(now);
  pollMic(now);
  handleSerial();

  if (oledOk && now - lastFaceAt >= FACE_FRAME_MS) {
    lastFaceAt = now;
    drawFace();
  }

  delay(5);
}
