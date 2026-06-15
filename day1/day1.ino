#include <LedControl.h>
#include <DHT.h>

// MAX7219 Configuration (DIN to 12, CLK to 11, CS to 10)
LedControl lc = LedControl(12, 11, 10, 1);

// DHT11 Configuration (DATA to Pin 2)
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Custom 3x5 font pixel maps for numbers 0-9
const byte numFont[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
  {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
  {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
  {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
  {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
  {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
  {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
  {0b111, 0b001, 0b001, 0b001, 0b001}, // 7
  {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
  {0b111, 0b101, 0b111, 0b001, 0b111}  // 9
};

// Timing variables
unsigned long lastSensorUpdate = 0;
unsigned long heatTimerStart = 0;
bool inHeatWarning = false;

// Threshold configuration
const float TEMP_THRESHOLD = 90.0;       // Threshold in Fahrenheit
const unsigned long TIME_PER_DOT = 120000; // 2 minutes in milliseconds (120,000 ms)

void setup() {
  // Initialize Matrix Display
  lc.shutdown(0, false);
  lc.setIntensity(0, 4);
  lc.clearDisplay(0);

  // Initialize DHT11 Sensor
  dht.begin();
}

void loop() {
  unsigned long currentMillis = millis();

  // Read sensor and update display every 2 seconds
  if (currentMillis - lastSensorUpdate >= 2000) {
    lastSensorUpdate = currentMillis;

    // Read temperature directly as Fahrenheit
    float t = dht.readTemperature(true);

    // Error handling: if communication fails, show 'E E'
    if (isnan(t) || t < 0 || t > 99) {
      lc.clearDisplay(0);
      lc.setRow(0, 0, 0b11101110);
      lc.setRow(0, 1, 0b10001000);
      lc.setRow(0, 2, 0b11101110);
      lc.setRow(0, 3, 0b10001000);
      lc.setRow(0, 4, 0b11101110);
      return;
    }

    // --- Heat Stroke Timer Logic ---
    if (t >= TEMP_THRESHOLD) {
      if (!inHeatWarning) {
        // Just entered high heat, start the exposure clock
        heatTimerStart = currentMillis;
        inHeatWarning = true;
      }
    } else {
      // Temperature dropped back to normal, reset exposure clock
      inHeatWarning = false;
      heatTimerStart = 0;
    }

    // Calculate how many warning dots should light up on the bottom row (0 to 8)
    int dotsToLight = 0;
    if (inHeatWarning) {
      unsigned long totalElapsedTime = currentMillis - heatTimerStart;
      dotsToLight = totalElapsedTime / TIME_PER_DOT;
      if (dotsToLight > 8) dotsToLight = 8; // Cap at the full 8 dots
    }

    // Convert the number of dots into a binary row byte (e.g., 3 dots = 0b11100000)
    byte bottomRowPattern = 0;
    for (int i = 0; i < dotsToLight; i++) {
      bottomRowPattern |= (1 << (7 - i));
    }

    // --- Render Display ---
    int fahrenheit = (int)t;
    int tens = fahrenheit / 10;
    int ones = fahrenheit % 10;

    lc.clearDisplay(0);
   
    // Draw temperature on rows 0-4
    for (int row = 0; row < 5; row++) {
      byte rowPattern = (numFont[tens][row] << 4) | numFont[ones][row];
      lc.setRow(0, row, rowPattern);
    }

    // Draw the warning progress bar on the very bottom row (Row 7)
    lc.setRow(0, 7, bottomRowPattern);
  }
}