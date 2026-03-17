#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Adafruit_NeoPixel.h>

// ---------- Nokia 5110 pins ----------
#define PIN_SCLK 10
#define PIN_DIN 11
#define PIN_DC 14
#define PIN_CS 13
#define PIN_RST 15

// ---------- NeoPixel ----------
#define LED_PIN 16
#define LED_COUNT 1

Adafruit_PCD8544 display =
  Adafruit_PCD8544(PIN_SCLK, PIN_DIN, PIN_DC, PIN_CS, PIN_RST);

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println("RP2040 Zero Hardware Test");

  // ---------- Start LED ----------
  led.begin();
  led.setBrightness(40);
  led.show();

  // ---------- Start display ----------
  display.begin();
  display.setContrast(55);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);

  display.setCursor(0,0);
  display.println("RP2040 ZERO");

  display.setCursor(0,10);
  display.println("Nokia 5110");

  display.setCursor(0,20);
  display.println("Display OK");

  display.setCursor(0,30);
  display.println("RGB LED TEST");

  display.display();
}

void loop() {

  led.setPixelColor(0, led.Color(255,0,0));
  led.show();
  delay(500);

  led.setPixelColor(0, led.Color(0,255,0));
  led.show();
  delay(500);

  led.setPixelColor(0, led.Color(0,0,255));
  led.show();
  delay(500);
}