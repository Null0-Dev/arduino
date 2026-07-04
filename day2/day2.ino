#include <LiquidCrystal.h>

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

String streamingTicker = "Waiting for PC data...";
int textPosition = 0;
unsigned long lastScrollTime = 0;
const int scrollSpeed = 450;

String topText = "null0dev.vercel.app       ";
int topTextPosition = 0;
unsigned long lastTopScrollTime = 0;
const int topScrollSpeed = 800;

void setup() {
  lcd.begin(16, 2);
  Serial.begin(9600);
  lcd.clear();
}

void loop() {
  if (millis() - lastTopScrollTime >= topScrollSpeed) {
    lastTopScrollTime = millis();
    String topDisplay = topText.substring(topTextPosition, topTextPosition + 16);
    if (topDisplay.length() < 16) {
      topDisplay += topText.substring(0, 16 - topDisplay.length());
    }
    lcd.setCursor(0, 0);
    lcd.print(topDisplay);
    topTextPosition++;
    if (topTextPosition >= topText.length()) {
      topTextPosition = 0;
    }
  }

  if (Serial.available() > 0) {
    String newData = Serial.readStringUntil('\n');
    newData.trim();
    if (newData.length() > 0) {
      streamingTicker = "                " + newData + "      ";
      textPosition = 0;
      lcd.setCursor(0, 1);
      lcd.print("                ");
    }
  }

  if (millis() - lastScrollTime >= scrollSpeed) {
    lastScrollTime = millis();
    
    String currentDisplay = streamingTicker.substring(textPosition, textPosition + 16);
    lcd.setCursor(0, 1);
    lcd.print(currentDisplay);
    
    textPosition++;
    if (textPosition >= streamingTicker.length() - 16) {
      textPosition = 0;
    }
  }
}