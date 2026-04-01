#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// HW-61 backpack commonly uses 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("LCD 0x27 OK?");
  lcd.setCursor(0, 1);
  lcd.print("Adjust contrast");

  Serial.println("LCD diagnostic started.");
}

void loop() {
  static uint32_t last = 0;
  static uint32_t count = 0;
  if (millis() - last >= 1000) {
    last = millis();
    count++;
    lcd.setCursor(0, 1);
    char line[17];
    snprintf(line, sizeof(line), "Count:%-9lu", (unsigned long)count);
    lcd.print(line);
  }
}
