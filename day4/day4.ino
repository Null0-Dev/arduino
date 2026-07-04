#include <Stepper.h>

// 1. Setup Stepper Motor (2048 steps = 1 full 360-degree rotation)
const int STEPS_PER_REV = 2048;
// Pin sequence 8, 10, 9, 11 for the ULN2003 driver board
Stepper myStepper(STEPS_PER_REV, 8, 10, 9, 11);

// 2. Setup Ultrasonic Sensor Pins
const int TRIG_PIN = 5;
const int ECHO_PIN = 6;

// Mapping Variables
int currentAngle = 0;
int stepIncrement = 11; // ~2 degrees per measurement slot (2048 steps / 360 deg = ~5.7 steps per degree)

void setup() {
  Serial.begin(9600);
  
  // Initialize sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // Set motor speed (10-12 RPM is ideal for a stable sensor sweep)
  myStepper.setSpeed(12);
  
  Serial.println("--- RADAR SCAN STARTING ---");
}

void loop() {
  // Sweep Forward: 0 to 358 degrees for a full 360-degree scan
  for (currentAngle = 0; currentAngle < 360; currentAngle += 2) {
    int distance = getDistance();

    // Print in "angle,distance" format for your eyes and for Python
    Serial.print(currentAngle);
    Serial.print(",");
    Serial.println(distance);

    // Move the motor forward by a few steps (~2 degrees worth)
    myStepper.step(11);
    delay(50); // Small pause to stabilize the sensor before the next reading
  }

  // Sweep Backward: 358 down to 0 degrees to complete the full-circle sweep
  for (currentAngle = 358; currentAngle >= 0; currentAngle -= 2) {
    int distance = getDistance();

    Serial.print(currentAngle);
    Serial.print(",");
    Serial.println(distance);

    // Move the motor backward (negative steps)
    myStepper.step(-11);
    delay(50);
  }
}

// Helper function to calculate ultrasonic distance
int getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout stops it from hanging if no bounce occurs
  
  // Convert duration to centimeters
  int cm = duration / 29.1 / 2;
  
  // If reading is out of range or 0, cap it at a maximum value (e.g., 200cm) so your map stays clean
  if (cm <= 0 || cm > 200) {
    cm = 200;
  }
  
  return cm;
}