
/************************************************************************
 * Meds Reminder Keychain - Prototype
 * TinyScreen+ + WiFi TinyShield ASD2123-R + Buzzer Wireling on A1
 *
 * Buttons:
 * - Upper-left: Select / confirm / acknowledge reminder
 * - Lower-left: Open menu / scroll / increase schedule value
 *
 * Time:
 * - Automatic NTP synchronisation
 * - Singapore timezone (UTC+8)
 * - Resynchronises every 6 hours while connected
 *
 * Medication:
 * - Daily reminder at the selected time
 * - Default schedule: 20:00
 ************************************************************************/

#include <Wireling.h>
#include <TinyScreen.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <RTCZero.h>

// ---------- Wi-Fi credentials ----------
char ssid[] = "YOUR_WIFI_NAME";
char wifiPassword[] = "YOUR_WIFI_PASSWORD";

// ---------- Hardware ----------
const int buzzerPin = A1;
TinyScreen display = TinyScreen(TinyScreenPlus);

RTCZero rtc;
WiFiUDP udp;

// WiFi TinyShield pin configuration
const char* ntpServer = "pool.ntp.org";
const unsigned int NTP_PORT = 123;

byte ntpPacket[48];

const unsigned long NTP_EPOCH_OFFSET = 2208988800UL;
const long SINGAPORE_OFFSET = 8L * 60L * 60L;

// ---------- Time synchronisation ----------
bool timeSynced = false;

unsigned long lastSyncMillis = 0;
const unsigned long SYNC_INTERVAL = 6UL * 60UL * 60UL * 1000UL;
// Refresh the main screen once per second
unsigned long lastScreenUpdate = 0;
const unsigned long SCREEN_UPDATE_INTERVAL = 1000;

// ---------- Medication details ----------
const char medicationName[] = "Vitamin C";
const char reminderMessage[] = "Time to take meds!";

// ---------- Medication schedule ----------
// Default: 20:00 (8 PM)
int scheduleHour = 20;
int scheduleMinute = 0;

// Used to prevent multiple reminders on the same day
unsigned long lastReminderDay = 0;

// Schedule editing
int scheduleEditField = 0;  // 0 = hour, 1 = minute

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
  SET_SCHEDULE_SCREEN,
  DEVICE_INFO_SCREEN
};

ScreenState currentScreen = MAIN_SCREEN;

// ---------- Menu ----------
const char* menuItems[] = {
  "Next Reminder",
  "Medication List",
  "Set Schedule",
  "Test Reminder",
  "Device Info",
  "Back"
};

const int MENU_COUNT = 6;
int menuIndex = 0;

// ---------- Reminder state ----------
unsigned long lastBeepTime = 0;

bool reminderActive = false;
bool reminderAcknowledged = false;
bool buzzerOn = false;

// ---------- Button state ----------
bool previousUpperButton = false;
bool previousLowerButton = false;


// ======================================================================
// SETUP
// ======================================================================

void setup() {
  Wireling.begin();

  pinMode(buzzerPin, OUTPUT);
  noTone(buzzerPin);

  display.begin();
  display.setBrightness(10);
  display.setBitDepth(16);
  display.setFont(liberationSans_8ptFontInfo);

  SerialUSB.begin(9600);

  rtc.begin();

  showSyncingScreen();

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    timeSynced = syncTimeFromNTP();

    if (timeSynced) {
      lastSyncMillis = millis();
    }
  }

  showMainScreen();
}


// ======================================================================
// MAIN LOOP
// ======================================================================

void loop() {
  unsigned long currentTime = millis();

  // ---------- Read buttons ----------
  bool upperButton = display.getButtons(TSButtonUpperLeft);
  bool lowerButton = display.getButtons(TSButtonLowerLeft);

  bool upperPressed = upperButton && !previousUpperButton;
  bool lowerPressed = lowerButton && !previousLowerButton;

  previousUpperButton = upperButton;
  previousLowerButton = lowerButton;

  // ---------- Periodic time synchronisation ----------
  if (WiFi.status() == WL_CONNECTED &&
      currentTime - lastSyncMillis >= SYNC_INTERVAL) {

    if (syncTimeFromNTP()) {
      lastSyncMillis = currentTime;
      timeSynced = true;
    }
  }

  // ---------- Check medication schedule ----------
  if (timeSynced && !reminderActive) {
    checkMedicationSchedule();
  }

  // ---------- Refresh main screen clock ----------
  if (currentScreen == MAIN_SCREEN &&
    !reminderActive &&
    currentTime - lastScreenUpdate >= SCREEN_UPDATE_INTERVAL) {

  lastScreenUpdate = currentTime;
  showMainScreen();
}

  // ---------- Handle active reminder ----------
  if (reminderActive && !reminderAcknowledged) {

    // Upper-left acknowledges the reminder
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

    // Do not process menu buttons during an active reminder
    return;
  }

  // ---------- Menu controls ----------
  if (currentScreen == MAIN_SCREEN) {

    if (lowerPressed) {
      menuIndex = 0;
      currentScreen = MENU_SCREEN;
      showMenu();
    }
  }

  else if (currentScreen == MENU_SCREEN) {

    // Lower-left: scroll down
    if (lowerPressed) {
      menuIndex++;

      if (menuIndex >= MENU_COUNT) {
        menuIndex = 0;
      }

      showMenu();
    }

    // Upper-left: select option
    if (upperPressed) {
      selectMenuItem();
    }
  }

  // ---------- Schedule editing ----------
  else if (currentScreen == SET_SCHEDULE_SCREEN) {

    // Lower-left increases the selected value
    if (lowerPressed) {

      if (scheduleEditField == 0) {
        scheduleHour++;

        if (scheduleHour > 23) {
          scheduleHour = 0;
        }
      }

      else {
        scheduleMinute++;

        if (scheduleMinute > 59) {
          scheduleMinute = 0;
        }
      }

      showScheduleScreen();
    }

    // Upper-left changes field or saves
    if (upperPressed) {

      if (scheduleEditField == 0) {
        scheduleEditField = 1;
        showScheduleScreen();
      }

      else {
        scheduleEditField = 0;
        currentScreen = MENU_SCREEN;
        showMenu();
      }
    }
  }

  // ---------- Return from information screens ----------
  else {

    if (lowerPressed) {
      currentScreen = MENU_SCREEN;
      showMenu();
    }
  }
}


// ======================================================================
// WI-FI CONNECTION
// ======================================================================

void connectWiFi() {

  // Required pin configuration for ASD2123-R
  WiFi.setPins(8, 2, A3, -1);

  SerialUSB.println("Connecting to Wi-Fi...");

  WiFi.begin(ssid, wifiPassword);

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < 30000) {

    delay(500);
    SerialUSB.print(".");
  }

  SerialUSB.println();

  if (WiFi.status() == WL_CONNECTED) {

    SerialUSB.println("Wi-Fi connected!");
    SerialUSB.print("IP address: ");
    SerialUSB.println(WiFi.localIP());
  }

  else {
    SerialUSB.println("Wi-Fi connection failed.");
  }
}


// ======================================================================
// NTP TIME SYNCHRONISATION
// ======================================================================

bool syncTimeFromNTP() {

  SerialUSB.println("Synchronising time...");

  IPAddress ntpIP;

  if (WiFi.hostByName(ntpServer, ntpIP) != 1) {
    SerialUSB.println("Could not resolve NTP server.");
    return false;
  }

  memset(ntpPacket, 0, sizeof(ntpPacket));

  // NTP request
  ntpPacket[0] = 0b11100011;
  ntpPacket[1] = 0;
  ntpPacket[2] = 6;
  ntpPacket[3] = 0xEC;

  udp.stop();
  udp.begin(2390);

  udp.beginPacket(ntpIP, NTP_PORT);
  udp.write(ntpPacket, sizeof(ntpPacket));
  udp.endPacket();

  unsigned long startWait = millis();

  while (millis() - startWait < 5000) {

    int packetSize = udp.parsePacket();

    if (packetSize >= 48) {

      udp.read(ntpPacket, 48);

      unsigned long secondsSince1900 =
        ((unsigned long)ntpPacket[40] << 24) |
        ((unsigned long)ntpPacket[41] << 16) |
        ((unsigned long)ntpPacket[42] << 8) |
        ((unsigned long)ntpPacket[43]);

      unsigned long utcEpoch =
        secondsSince1900 - NTP_EPOCH_OFFSET;

      // Convert UTC to Singapore time (UTC+8)
      rtc.setEpoch(utcEpoch + SINGAPORE_OFFSET);

      // Set this BEFORE printing the time
      timeSynced = true;

      SerialUSB.println("Time synchronised!");
      printCurrentTime();

      return true;
    }

    delay(10);
  }

  SerialUSB.println("NTP request timed out.");

  return false;
}


// ======================================================================
// DISPLAY CURRENT TIME
// ======================================================================

void printCurrentTime() {

  if (!timeSynced) {
    SerialUSB.println("Time not synchronised.");
    return;
  }

  char timeString[9];

  sprintf(timeString, "%02d:%02d:%02d",
          rtc.getHours(),
          rtc.getMinutes(),
          rtc.getSeconds());

  SerialUSB.print("Singapore time: ");
  SerialUSB.println(timeString);
}


// ======================================================================
// CHECK MEDICATION SCHEDULE
// ======================================================================

void checkMedicationSchedule() {

  int currentHour = rtc.getHours();
  int currentMinute = rtc.getMinutes();

  // Get current day number
  unsigned long currentDay = rtc.getEpoch() / 86400UL;

  // Check whether the scheduled time has arrived
  if (currentHour == scheduleHour &&
      currentMinute == scheduleMinute &&
      currentDay != lastReminderDay) {

    lastReminderDay = currentDay;

    startReminder();
  }
}


// ======================================================================
// START REMINDER
// ======================================================================

void startReminder() {

  reminderActive = true;
  reminderAcknowledged = false;

  buzzerOn = true;
  lastBeepTime = millis();

  tone(buzzerPin, BUZZER_FREQUENCY);

  currentScreen = MAIN_SCREEN;

  showReminderScreen();
}


// ======================================================================
// ACKNOWLEDGE REMINDER
// ======================================================================

void acknowledgeReminder() {

  reminderAcknowledged = true;
  reminderActive = false;

  noTone(buzzerPin);
  buzzerOn = false;

  currentScreen = MAIN_SCREEN;

  showAcknowledgedScreen();
}


// ======================================================================
// MENU SELECTION
// ======================================================================

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
      scheduleEditField = 0;
      currentScreen = SET_SCHEDULE_SCREEN;
      showScheduleScreen();
      break;

    case 3:
      startReminder();
      break;

    case 4:
      currentScreen = DEVICE_INFO_SCREEN;
      showDeviceInfoScreen();
      break;

    case 5:
      currentScreen = MAIN_SCREEN;
      showMainScreen();
      break;
  }
}


// ======================================================================
// MAIN SCREEN
// ======================================================================

void showMainScreen() {

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 3);
  display.print("MEDS REMINDER");

  display.setCursor(0, 18);

  if (timeSynced) {

    char timeString[9];

    sprintf(timeString, "%02d:%02d:%02d",
            rtc.getHours(),
            rtc.getMinutes(),
            rtc.getSeconds());

    display.print("Time: ");
    display.print(timeString);
  }

  else {
    display.print("Time not synced");
  }

  display.setCursor(0, 34);
  display.print("Reminder:");

  display.setCursor(0, 46);

  char scheduleString[6];

  sprintf(scheduleString, "%02d:%02d",
          scheduleHour, scheduleMinute);

  display.print(scheduleString);

  display.setCursor(0, 57);
  display.print("> Menu");
}


// ======================================================================
// SYNCING SCREEN
// ======================================================================

void showSyncingScreen() {

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 10);
  display.print("MEDS REMINDER");

  display.setCursor(0, 28);
  display.print("Syncing time...");

  display.setCursor(0, 45);
  display.print("Please wait");
}


// ======================================================================
// MENU SCREEN
// ======================================================================

void showMenu() {

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 0);
  display.print("MENU");

  for (int i = 0; i < MENU_COUNT; i++) {

    int y = 10 + (i * 9);

    display.setCursor(0, y);

    if (i == menuIndex) {
      display.print(">");
    }

    else {
      display.print(" ");
    }

    display.print(menuItems[i]);
  }
}


// ======================================================================
// NEXT REMINDER SCREEN
// ======================================================================

void showNextReminderScreen() {

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("NEXT REMINDER");

  display.setCursor(0, 23);
  display.print(medicationName);

  display.setCursor(0, 39);
  display.print("Scheduled:");

  display.setCursor(0, 51);

  char scheduleString[6];

  sprintf(scheduleString, "%02d:%02d",
          scheduleHour, scheduleMinute);

  display.print(scheduleString);
}


// ======================================================================
// MEDICATION LIST SCREEN
// ======================================================================

void showMedicationScreen() {

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("MEDICATION");

  display.setCursor(0, 23);
  display.print(medicationName);

  display.setCursor(0, 39);
  display.print("Daily reminder");

  display.setCursor(0, 55);
  display.print("> Back");
}


// ======================================================================
// SET SCHEDULE SCREEN
// ======================================================================

void showScheduleScreen() {

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 3);
  display.print("SET SCHEDULE");

  display.setCursor(0, 19);

  display.print("Hour: ");

  if (scheduleEditField == 0) {
    display.print(">");
  }

  display.print(scheduleHour);

  display.setCursor(0, 34);

  display.print("Minute: ");

  if (scheduleEditField == 1) {
    display.print(">");
  }

  display.print(scheduleMinute);

  display.setCursor(0, 51);

  if (scheduleEditField == 0) {
    display.print("Lower:+  Upper:next");
  }

  else {
    display.print("Lower:+  Upper:save");
  }
}


// ======================================================================
// DEVICE INFORMATION
// ======================================================================

void showDeviceInfoScreen() {

  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);

  display.setCursor(0, 5);
  display.print("DEVICE INFO");

  display.setCursor(0, 22);
  display.print("Meds Keychain");

  display.setCursor(0, 38);
  display.print("Prototype 3");

  display.setCursor(0, 55);
  display.print("> Back");
}


// ======================================================================
// REMINDER SCREEN
// ======================================================================

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
  display.print("> Acknowledge");
}


// ======================================================================
// ACKNOWLEDGED SCREEN
// ======================================================================

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
