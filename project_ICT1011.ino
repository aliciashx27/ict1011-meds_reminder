
/************************************************************************
 * Meds Reminder Keychain - Prototype 1
 * TinyScreen+ + Buzzer Wireling on Port 1 (A1)
 *
 * Test mode:
 * - Reminder activates 30 seconds after startup
 * - Upper-left button acknowledges reminder
 ************************************************************************/

#include <Wireling.h>
#include <TinyScreen.h>

// ---------- Hardware ----------
const int buzzerPin = A1;

TinyScreen display = TinyScreen(TinyScreenPlus);

// ---------- Medication details ----------
const char medicationName[] = "Vitamin C";
const char reminderMessage[] = "Time to take meds!";

// ---------- Test reminder ----------
const unsigned long TEST_DELAY = 30000;

// ---------- Buzzer pattern ----------
const unsigned long BEEP_ON_TIME = 500;
const unsigned long BEEP_OFF_TIME = 300;
const int BUZZER_FREQUENCY = 1000;

// ---------- State variables ----------
unsigned long startupTime = 0;
unsigned long lastBeepTime = 0;

bool reminderActive = false;
bool reminderAcknowledged = false;
bool buzzerOn = false;

// Button state for detecting a new press
bool previousButtonState = false;

void setup() {
  Wireling.begin();

  pinMode(buzzerPin, OUTPUT);
  noTone(buzzerPin);

  // Initialize TinyScreen+
  display.begin();
  display.setBrightness(10);
  display.setBitDepth(16);
  display.setFont(liberationSans_8ptFontInfo);
  startupTime = millis();

  showWaitingScreen();
}

void loop() {
  unsigned long currentTime = millis();

  // ---------- Activate reminder ----------
  if (!reminderActive &&
      currentTime - startupTime >= TEST_DELAY) {

    reminderActive = true;
    buzzerOn = true;
    lastBeepTime = currentTime;

    // Start buzzer immediately
    tone(buzzerPin, BUZZER_FREQUENCY);

    showReminderScreen();
  }

  // ---------- Handle active reminder ----------
  if (reminderActive && !reminderAcknowledged) {

    // Read upper-left button
    bool buttonPressed =
        display.getButtons(TSButtonUpperLeft);

    // Detect a new press instead of a held button
    if (buttonPressed && !previousButtonState) {
      acknowledgeReminder();
    }

    previousButtonState = buttonPressed;

    // ---------- Buzzer pattern ----------
    if (!reminderAcknowledged) {

      if (buzzerOn &&
          currentTime - lastBeepTime >= BEEP_ON_TIME) {

        noTone(buzzerPin);
        buzzerOn = false;
        lastBeepTime = currentTime;
      }

      else if (!buzzerOn &&
               currentTime - lastBeepTime >= BEEP_OFF_TIME) {

        tone(buzzerPin, BUZZER_FREQUENCY);
        buzzerOn = true;
        lastBeepTime = currentTime;
      }
    }
  }
}

// ---------- Waiting screen ----------
void showWaitingScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("MEDS REMINDER");

  display.setCursor(0, 22);
  display.print("Next reminder:");

  display.setCursor(0, 35);
  display.print("in 30 seconds");

  display.setCursor(0, 50);
  display.print("Test mode");
}

// ---------- Reminder screen ----------
void showReminderScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 3);
  display.print("TIME FOR MEDS!");

  display.setCursor(0, 18);
  display.print(medicationName);

  display.setCursor(0, 32);
  display.print("Time to take");

  display.setCursor(0, 42);
  display.print("your medication");

  display.setCursor(0, 55);
  display.print("Press button");
}

// ---------- Acknowledge reminder ----------
void acknowledgeReminder() {
  reminderAcknowledged = true;

  noTone(buzzerPin);
  buzzerOn = false;

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 10);
  display.print("Reminder");

  display.setCursor(0, 25);
  display.print("acknowledged!");

  display.setCursor(0, 45);
  display.print("Stay healthy :)");
}
