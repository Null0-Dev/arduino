#include <LiquidCrystal.h>

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

String streamingTicker = "Waiting for PC data...";
String lastStatic = "";
int textPosition = 0;
unsigned long lastScrollTime = 0;
const int scrollSpeed = 250;
bool isStatic = false;  // true => one stock frozen in place (no scrolling)

void setup() {
  lcd.begin(16, 2);
  Serial.begin(9600);
  lcd.clear();
}

void loop() {
  lcd.setCursor(0, 0);
  lcd.print("Subscribe!          ");

  if (Serial.available() > 0) {
    String newData = Serial.readStringUntil('\n');
    newData.trim();

    // The PC prefixes '~' during playback. Strip it if present (it's just a hint).
    if (newData.length() > 0 && newData.charAt(0) == '~') {
      newData = newData.substring(1);
    }

    if (newData.length() > 0) {
      // If the whole message fits on the 16-char screen (e.g. a single playback
      // stock), FREEZE it: print once, in place, so only the digits change and the
      // line never slides. Longer live banners still scroll.
      if (newData.length() <= 16) {
        isStatic = true;
        while (newData.length() < 16) newData += " ";  // pad to clear old characters
        if (newData != lastStatic) {                   // only redraw when it actually changed
          lastStatic = newData;
          lcd.setCursor(0, 1);
          lcd.print(newData);
        }
      } else {
        isStatic = false;
        lastStatic = "";
        streamingTicker = "                " + newData + "      ";
        textPosition = 0;
        lcd.setCursor(0, 1);
        lcd.print("                ");
      }
    }
  }

  if (!isStatic && millis() - lastScrollTime >= scrollSpeed) {
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
