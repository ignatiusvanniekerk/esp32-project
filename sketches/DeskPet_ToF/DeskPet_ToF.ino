#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t oledAddr = 0;
unsigned long lastBlink = 0;
bool blinking = false;

void drawFace(bool blink) {
  display.clearDisplay();

  display.drawCircle(64, 26, 8, SSD1306_WHITE);
  display.drawCircle(64, 26, 10, SSD1306_WHITE);
  display.drawCircle(44, 20, 9, SSD1306_WHITE);
  display.drawCircle(84, 20, 9, SSD1306_WHITE);

  if (blink) {
    display.fillRect(40, 20, 8, 2, SSD1306_WHITE);
    display.fillRect(80, 20, 8, 2, SSD1306_WHITE);
  } else {
    display.fillCircle(44, 20, 3, SSD1306_WHITE);
    display.fillCircle(84, 20, 3, SSD1306_WHITE);
  }

  display.fillCircle(38, 36, 2, SSD1306_WHITE);
  display.fillCircle(90, 36, 2, SSD1306_WHITE);

  display.drawCircle(64, 42, 10, SSD1306_WHITE);
  display.fillRect(54, 34, 20, 8, SSD1306_BLACK);

  display.display();
}

bool findOled() {
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0) { oledAddr = 0x3C; return true; }
  Wire.beginTransmission(0x3D);
  if (Wire.endTransmission() == 0) { oledAddr = 0x3D; return true; }
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Desk Buddy OLED face booting...");
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!findOled()) {
    Serial.println("OLED not found on I2C (tried 0x3C / 0x3D)");
    for (;;) delay(100);
  }
  Serial.printf("OLED found at 0x%02X\n", oledAddr);

  if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddr)) {
    Serial.println("SSD1306 init failed");
    for (;;) delay(100);
  }

  drawFace(false);
  Serial.println("Face on screen");
}

void loop() {
  unsigned long now = millis();

  if (blinking) {
    if (now - lastBlink >= 120) {
      blinking = false;
      lastBlink = now;
      drawFace(false);
    }
  } else {
    if (now - lastBlink >= 3000) {
      blinking = true;
      lastBlink = now;
      drawFace(true);
    }
  }
}