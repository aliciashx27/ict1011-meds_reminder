/************************************************************************
 * TinyCircuits Buzzer Wireling - Simple Test
 * Buzzer connected to Wireling Adapter Port 1
 ************************************************************************/

#include <Wireling.h>

const int buzzerPin = A1;  // Port 1 = A1

void setup() {
  Wireling.begin();
  pinMode(buzzerPin, OUTPUT);

  // Play three test beeps
  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, 1000, 300); // 1000 Hz for 300 ms
    delay(500);
    noTone(buzzerPin);
  }
}

void loop() {
  // Nothing here; test plays once after startup
}