#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define SDA_PIN 21
#define SCL_PIN 22
#define TOUCH_PIN 32
#define MIC_PIN 35
#define SPEAKER_PIN 25
#define MIC_SAMPLE_INTERVAL_MS 5
#define MIC_WINDOW_MS 100
#define MIC_TRIGGER_DELTA 200
#define MIC_COOLDOWN_MS 1500
#define TOUCH_BEEP_FREQUENCY 1200
#define TOUCH_BEEP_DURATION_MS 120
#define NOISE_BEEP_FREQUENCY 1600
#define NOISE_BEEP_DURATION_MS 70
#define NOISE_BEEP_GAP_MS 40
#define NOISE_BEEP_COUNT 3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t oledAddr = 0;
bool oledOk = false;
bool happy = false;
bool blinking = false;
unsigned long lastBlink = 0;
unsigned long heartLast = 0;
bool heartOn = true;
bool touchWasDown = false;
unsigned long happyUntil = 0;
int micBaseline = 2048;
int micPeak = 0;
unsigned long micSampleAt = 0;
unsigned long micWindowAt = 0;
unsigned long lastBeepAt = 0;
unsigned long toneUntil = 0;
uint32_t toneFrequency = 0;
uint32_t toneDuration = 0;
uint32_t toneGap = 0;
uint8_t toneCount = 0;
uint8_t toneIndex = 0;
bool toneGapActive = false;

void playBeep(uint32_t frequency, uint32_t duration, uint8_t count, uint32_t gap) {
  toneFrequency = frequency;
  toneDuration = duration;
  toneGap = gap;
  toneCount = count;
  toneIndex = 0;
  toneGapActive = false;
  ledcWriteTone(SPEAKER_PIN, frequency);
  toneUntil = millis() + duration;
}

void updateBeep(unsigned long now) {
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

void calibrateMic() {
  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);
  long total = 0;
  for (int i = 0; i < 100; i++) {
    total += analogRead(MIC_PIN);
    delay(10);
  }
  micBaseline = total / 100;
}

bool findOled() {
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0) { oledAddr = 0x3C; return true; }
  Wire.beginTransmission(0x3D);
  if (Wire.endTransmission() == 0) { oledAddr = 0x3D; return true; }
  return false;
}

void drawEye(int x, int y, bool blink) {
  if (blink) {
    display.fillRect(x - 4, y, 8, 2, SSD1306_WHITE);
    return;
  }
  if (happy) {
    display.fillRect(x - 5, y - 5, 10, 2, SSD1306_WHITE);
    display.fillRect(x - 5, y + 3, 10, 2, SSD1306_WHITE);
    display.fillRect(x - 5, y - 1, 2, 4, SSD1306_WHITE);
    display.fillRect(x + 3, y - 1, 2, 4, SSD1306_WHITE);
  } else {
    display.fillCircle(x, y, 6, SSD1306_WHITE);
    display.fillCircle(x - 2, y - 2, 2, SSD1306_BLACK);
  }
}

void drawFace(bool blink) {
  display.clearDisplay();

  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print(happy ? ":)" : ":|");
  display.setCursor(112, 0);
  display.print(happy ? "O" : heartOn ? "o" : "o");

  display.fillCircle(53, 14, 7, SSD1306_WHITE);
  display.fillCircle(75, 14, 7, SSD1306_WHITE);

  display.fillCircle(64, 38, 24, SSD1306_WHITE);

  drawEye(50, 32, blink);
  drawEye(78, 32, blink);

  display.fillCircle(40, 46, 3, SSD1306_WHITE);
  display.fillCircle(88, 46, 3, SSD1306_WHITE);

  if (happy) {
    display.setCursor(52, 42);
    display.setTextSize(1);
    display.setTextColor(SSD1306_BLACK);
    display.print("U");
  } else {
    display.drawCircle(64, 46, 7, SSD1306_BLACK);
    display.fillRect(57, 39, 14, 7, SSD1306_WHITE);
  }

  display.setCursor(0, 56);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print("TOUCH: GPIO32");

  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial.println("DeskPet face + touch booting...");
  Wire.begin(SDA_PIN, SCL_PIN);

  oledOk = findOled();
  if (oledOk) {
    Serial.printf("OLED found at 0x%02X\n", oledAddr);
    display.begin(SSD1306_SWITCHCAPVCC, oledAddr);
  } else {
    Serial.println("No OLED (tried 0x3C / 0x3D)");
  }

  pinMode(TOUCH_PIN, INPUT);
  pinMode(MIC_PIN, INPUT);
  ledcAttach(SPEAKER_PIN, 1000, 10);
  ledcWriteTone(SPEAKER_PIN, 0);
  calibrateMic();
  micWindowAt = millis();

  Serial.printf("Touch input ready on GPIO%d\n", TOUCH_PIN);
  Serial.printf("Mic input ready on GPIO%d, baseline=%d\n", MIC_PIN, micBaseline);
  Serial.printf("HXJ8002 L -> GPIO%d\n", SPEAKER_PIN);

  if (oledOk) drawFace(false);
}

void loop() {
  unsigned long now = millis();
  updateBeep(now);

  if (now - micSampleAt >= MIC_SAMPLE_INTERVAL_MS) {
    micSampleAt = now;
    int sample = analogRead(MIC_PIN);
    int deviation = sample - micBaseline;
    if (deviation < 0) deviation = -deviation;
    if (deviation > micPeak) micPeak = deviation;
  }

  if (now - heartLast >= 500) {
    heartLast = now;
    heartOn = !heartOn;
  }

  if (blinking) {
    if (now - lastBlink >= 120) {
      blinking = false;
      lastBlink = now;
      if (oledOk) drawFace(false);
    }
  } else {
    if (now - lastBlink >= 3000) {
      blinking = true;
      lastBlink = now;
      if (oledOk) drawFace(true);
    }
  }

  bool touchDown = digitalRead(TOUCH_PIN) == HIGH;
  if (touchDown && !touchWasDown) {
    happy = true;
    happyUntil = now + 1500;
    lastBeepAt = now;
    playBeep(TOUCH_BEEP_FREQUENCY, TOUCH_BEEP_DURATION_MS, 1, 0);
    Serial.println("Touch detected on GPIO32; beep");
    if (oledOk && !blinking) drawFace(false);
  } else if (!touchDown && touchWasDown) {
    Serial.println("Touch released");
  }

  if (now - micWindowAt >= MIC_WINDOW_MS) {
    int lastPeak = micPeak;
    micPeak = 0;
    micWindowAt = now;
    if (lastPeak >= MIC_TRIGGER_DELTA &&
        (lastBeepAt == 0 || now - lastBeepAt >= MIC_COOLDOWN_MS)) {
      lastBeepAt = now;
      playBeep(NOISE_BEEP_FREQUENCY, NOISE_BEEP_DURATION_MS, NOISE_BEEP_COUNT, NOISE_BEEP_GAP_MS);
      Serial.printf("Loud noise detected: peak=%d; 3 beeps\n", lastPeak);
    }
  }

  if (!touchDown && happy && now >= happyUntil) {
    happy = false;
    if (oledOk && !blinking) drawFace(false);
  }

  touchWasDown = touchDown;
  delay(5);
}
