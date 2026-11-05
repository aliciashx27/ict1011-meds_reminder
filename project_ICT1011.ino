
/************************************************************************
 * Meds Reminder Keychain - Prototype 2
 * TinyScreen+ + Buzzer Wireling on A1
 *
 * Buttons:
 * - Upper-left: Select / acknowledge reminder
 * - Lower-left: Open menu / move down / back
 *
 * Test reminder activates 30 seconds after startup.
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

// ---------- Buzzer ----------
const unsigned long BEEP_ON_TIME = 500;
const unsigned long BEEP_OFF_TIME = 300;
const int BUZZER_FREQUENCY = 1000;

// ---------- Screens ----------
enum ScreenState {
  MAIN_SCREEN,
  MENU_SCREEN,
  NEXT_REMINDER_SCREEN,
  MEDICATION_SCREEN,
  DEVICE_INFO_SCREEN
};

ScreenState currentScreen = MAIN_SCREEN;

// ---------- Menu ----------
const char* menuItems[] = {
  "Next Reminder",
  "Medication List",
  "Test Reminder",
  "Device Info",
  "Back"
};

const int MENU_COUNT = 5;
int menuIndex = 0;

// ---------- Reminder state ----------
unsigned long startupTime = 0;
unsigned long lastBeepTime = 0;

bool reminderActive = false;
bool reminderAcknowledged = false;
bool buzzerOn = false;

// ---------- Button state ----------
bool previousUpperButton = false;
bool previousLowerButton = false;

void setup() {
  Wireling.begin();

  pinMode(buzzerPin, OUTPUT);
  noTone(buzzerPin);

  display.begin();
  display.setBrightness(10);
  display.setBitDepth(16);
  display.setFont(liberationSans_8ptFontInfo);

  startupTime = millis();

  showMainScreen();
}

void loop() {
  unsigned long currentTime = millis();

  // Read buttons
  bool upperButton = display.getButtons(TSButtonUpperLeft);
  bool lowerButton = display.getButtons(TSButtonLowerLeft);

  // Detect new button presses
  bool upperPressed = upperButton && !previousUpperButton;
  bool lowerPressed = lowerButton && !previousLowerButton;

  previousUpperButton = upperButton;
  previousLowerButton = lowerButton;

  // ---------- Automatic test reminder ----------
  if (!reminderActive &&
      currentTime - startupTime >= TEST_DELAY) {
    startReminder();
  }

  // ---------- Handle active reminder ----------
  if (reminderActive && !reminderAcknowledged) {

    // Upper-left button acknowledges reminder
    if (upperPressed) {
      acknowledgeReminder();
    }

    // Buzzer pattern
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

  // ---------- Menu controls ----------
  if (currentScreen == MAIN_SCREEN) {
    if (lowerPressed) {
      currentScreen = MENU_SCREEN;
      menuIndex = 0;
      showMenu();
    }
  }

  else if (currentScreen == MENU_SCREEN) {
    if (lowerPressed) {
      menuIndex++;
      if (menuIndex >= MENU_COUNT) {
        menuIndex = 0;
      }
      showMenu();
    }

    if (upperPressed) {
      selectMenuItem();
    }
  }

  // Return to menu from information screens
  else if (currentScreen != MAIN_SCREEN &&
           currentScreen != MENU_SCREEN) {
    if (lowerPressed) {
      currentScreen = MENU_SCREEN;
      showMenu();
    }
  }
}

// ---------- Start reminder ----------
void startReminder() {
  reminderActive = true;
  reminderAcknowledged = false;
  buzzerOn = true;
  lastBeepTime = millis();

  tone(buzzerPin, BUZZER_FREQUENCY);

  currentScreen = MAIN_SCREEN;
  showReminderScreen();
}

// ---------- Acknowledge reminder ----------
void acknowledgeReminder() {
  reminderAcknowledged = true;

  noTone(buzzerPin);
  buzzerOn = false;

  currentScreen = MAIN_SCREEN;
  showAcknowledgedScreen();
}

// ---------- Menu selection ----------
void selectMenuItem() {
  switch (menuIndex) {

    case 0:
      currentScreen = NEXT_REMINDER_SCREEN;
      showNextReminderScreen();
      break;

    case 1:
      currentScreen = MEDICATION_SCREEN;
      showMedicationScreen();
      break;

    case 2:
      startReminder();
      break;

    case 3:
      currentScreen = DEVICE_INFO_SCREEN;
      showDeviceInfoScreen();
      break;

    case 4:
      currentScreen = MAIN_SCREEN;
      showMainScreen();
      break;
  }
}

// ---------- Main screen ----------
void showMainScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("MEDS REMINDER");

  display.setCursor(0, 22);
  display.print("Next reminder:");

  display.setCursor(0, 35);
  display.print("in 30 seconds");

  display.setCursor(0, 52);
  display.print("Menu: lower btn");
}

// ---------- Menu screen ----------
void showMenu() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 2);
  display.print("MENU");

  display.setCursor(0, 20);
  display.print("> ");
  display.print(menuItems[menuIndex]);

  display.setCursor(0, 45);
  display.print("Lower: next");

  display.setCursor(0, 57);
  display.print("Upper: select");
}

// ---------- Next reminder ----------
void showNextReminderScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("NEXT REMINDER");

  display.setCursor(0, 22);
  display.print(medicationName);

  display.setCursor(0, 38);
  display.print("Test: 30 sec");

  display.setCursor(0, 55);
  display.print("Lower: back");
}

// ---------- Medication list ----------
void showMedicationScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("MEDICATION");

  display.setCursor(0, 23);
  display.print(medicationName);

  display.setCursor(0, 39);
  display.print("Reminder set");

  display.setCursor(0, 55);
  display.print("Lower: back");
}

// ---------- Device information ----------
void showDeviceInfoScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("DEVICE INFO");

  display.setCursor(0, 22);
  display.print("Meds Keychain");

  display.setCursor(0, 38);
  display.print("Prototype 2");

  display.setCursor(0, 55);
  display.print("Lower: back");
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
  display.print("Upper: stop");
}

// ---------- Acknowledged screen ----------
void showAcknowledgedScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 10);
  display.print("Reminder");

  display.setCursor(0, 25);
  display.print("acknowledged!");

  display.setCursor(0, 45);
  display.print("Stay healthy :)");
}
