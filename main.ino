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
 ************************************************************************/

#include <Wireling.h>
#include <TinyScreen.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <RTCZero.h>
#include <ArduinoJson.h>
#include <FlashStorage_SAMD.h>
//import the pets section
#include "Pet.h"

// ---------- GUI Colours (RGB565) ----------
#define BG_COLOR       0x11A7
#define TEXT_COLOR     0xF7FB
#define ACCENT_COLOR   0x2EB7
#define SECONDARY_COLOR 0x54D6

// ---------- Wi-Fi credentials ----------
char ssid[] = "YOUR_WIFI_SSID";
char wifiPassword[] = "YOUR_WIFI_PASSWORD";

IPAddress serverIP(0, 0, 0, 0); // Replace with your backend server IP
const int serverPort = 5050;

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

// ---------- Medication data ----------

#define MAX_MEDICATIONS 10

// ---------- Medication synchronisation ----------
unsigned long lastMedicationSync = 0;
const unsigned long MEDICATION_SYNC_INTERVAL = 30000;

struct Medication {
  char id[37];
  char name[21];
  char time[6];
  char message[17];

  bool days[7];

  int hour;
  int minute;

  unsigned long lastReminderDay;
};
Medication medications[MAX_MEDICATIONS];

int medicationCount = 0;
int activeMedicationIndex = -1;
int medicationListIndex = 0;

// ---------- Jingle Settings ----------

const int cuteChime[] = {
  523, 0,
  659, 0,
  784, 0,
  659, 0
};

const int cuteChimeDurations[] = {
  180, 60,
  180, 60,
  280, 80,
  360, 300
};

const int retroGame[] = {
  784, 784,
  988, 0,
  784, 659,
  523, 0
};

const int retroGameDurations[] = {
  100, 100,
  200, 60,
  100, 100,
  350, 250
};

const int gentleBell[] = {
  659, 0,
  784, 0,
  1047, 0
};

const int gentleBellDurations[] = {
  300, 100,
  300, 100,
  500, 300
};

struct Jingle {
  const int* melody;
  const int* durations;
  int length;
};

Jingle jingles[] = {
  {cuteChime, cuteChimeDurations, 8},
  {retroGame, retroGameDurations, 8},
  {gentleBell, gentleBellDurations, 6}
};

const int JINGLE_COUNT = 3;

// ---------- Saved Jingle/Pet growth Preference ----------

struct JingleSettings {
  uint8_t marker;
  uint8_t selectedIndex;
  uint8_t petGrowthPoints;
};

FlashStorage(jingleSettingsStorage, JingleSettings);

int selectedJingleIndex = 0;
int jingleMenuIndex = 0;
int currentNote = 0;
unsigned long lastNoteChange = 0;

// ---------- Screens ----------
enum ScreenState {
  MAIN_SCREEN,
  MENU_SCREEN,
  NEXT_REMINDER_SCREEN,
  MEDICATION_SCREEN,
  DEVICE_INFO_SCREEN,
  JINGLE_SETTINGS_SCREEN,
  PET_SCREEN
};

ScreenState currentScreen = MAIN_SCREEN;

// ---------- Pet animation ----------
uint8_t petFrame = 0;

unsigned long lastPetAnimation = 0;

const unsigned long PET_FRAME_INTERVAL = 450;

// ---------- Pet growth and selection ----------
uint8_t petGrowthPoints = 0;
char selectedPetId[8] = "tv";

// ---------- Menu ----------
const char* menuItems[] = {
  "Next Reminder",
  "Meds List",
  "Test Reminder",
  "Device Info",
  "Jingle Settings",
  "Back"
};

const int MENU_COUNT = 6;
int menuIndex = 0;

// ---------- Reminder state ----------
unsigned long lastBeepTime = 0;

bool reminderActive = false;
bool reminderAcknowledged = false;
bool buzzerOn = false;

// Dose history retry queue
#define MAX_PENDING_DOSES 5
#define DOSE_PAYLOAD_SIZE 256

char pendingDosePayloads[MAX_PENDING_DOSES][DOSE_PAYLOAD_SIZE];
bool pendingDoseUsed[MAX_PENDING_DOSES] = { false };

unsigned long lastDoseRetry = 0;
const unsigned long DOSE_RETRY_INTERVAL = 10000;

// ---------- Button state ----------
bool previousUpperButton = false;
bool previousLowerButton = false;


bool fetchMedications();

void saveJinglePreference() {

  JingleSettings settings;

  settings.marker = 0xA7;
  settings.selectedIndex = selectedJingleIndex;

  settings.petGrowthPoints = petGrowthPoints; //we add this here so we can use the flash storage

  jingleSettingsStorage.write(settings);

  SerialUSB.println("Jingle preference saved!");
}

void loadJinglePreference() {

  JingleSettings settings;
  jingleSettingsStorage.read(settings);

  bool needsSave = false;

  if (settings.marker == 0xA7 &&
      settings.selectedIndex < JINGLE_COUNT) {

    selectedJingleIndex = settings.selectedIndex;

    if (settings.petGrowthPoints <= 10) {
      petGrowthPoints = settings.petGrowthPoints;
    } else {
      petGrowthPoints = 0;
      needsSave = true;
    }

  } else {

    selectedJingleIndex = 0;
    petGrowthPoints = 0;
    needsSave = true;
  }

  jingleMenuIndex = selectedJingleIndex;

  if (needsSave) {
    saveJinglePreference();
  }

  SerialUSB.print("Loaded jingle: ");
  SerialUSB.println(selectedJingleIndex);

  SerialUSB.print("Pet growth points: ");
  SerialUSB.println(petGrowthPoints);
}

// ======================================================================
// SETUP
// ======================================================================

void setup() {
  Wireling.begin();

  pinMode(buzzerPin, OUTPUT);
  noTone(buzzerPin);

  display.begin();
  display.setColorMode(TSColorModeRGB);
  display.setBrightness(10);
  display.setBitDepth(16);
  display.setFont(liberationSans_8ptFontInfo);
  display.setFlip(true);

  SerialUSB.begin(9600);

  rtc.begin();

  loadJinglePreference();

  showSyncingScreen();

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    timeSynced = syncTimeFromNTP();

    if (timeSynced) {
      lastSyncMillis = millis();
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    fetchMedications();
    fetchPet();
    }
  showMainScreen();
}


bool fetchMedications() {

  WiFiClient client;

  SerialUSB.println("Fetching medications...");

  if (!client.connect(serverIP, serverPort)) {
    SerialUSB.println("Backend connection failed.");
    return false;
  }

  client.println("GET /api/device/medications HTTP/1.1");
  client.print("Host: ");
  client.println(serverIP);
  client.println("Connection: close");
  client.println();

  // Wait for the HTTP response
  unsigned long timeout = millis();

  while (!client.available() && millis() - timeout < 5000) {
    delay(10);
  }

  if (!client.find("\r\n\r\n")) {
    SerialUSB.println("HTTP headers not found.");
    client.stop();
    return false;
  }

  // Read the JSON response
  StaticJsonDocument<4096> doc;

  DeserializationError error = deserializeJson(doc, client);

  if (error) {
    SerialUSB.print("JSON error: ");
    SerialUSB.println(error.c_str());
    client.stop();
    return false;
  }

  JsonArray medicationArray = doc.as<JsonArray>();

  // Save previous reminder tracking before refreshing
  char previousIds[MAX_MEDICATIONS][37];
  unsigned long previousReminderDays[MAX_MEDICATIONS];

  int previousCount = medicationCount;

  for (int i = 0; i < previousCount; i++) {
      strcpy(previousIds[i], medications[i].id);
      previousReminderDays[i] = medications[i].lastReminderDay;
  }

  medicationCount = 0;

  for (JsonObject item : medicationArray) {

    if (medicationCount >= MAX_MEDICATIONS) {
      break;
    }

    Medication &med = medications[medicationCount];

    // Read medication information
    strncpy(med.id, item["id"] | "", sizeof(med.id) - 1);
    med.id[sizeof(med.id) - 1] = '\0';

    strncpy(med.name, item["name"] | "", sizeof(med.name) - 1);
    med.name[sizeof(med.name) - 1] = '\0';

    strncpy(med.time, item["time"] | "00:00", sizeof(med.time) - 1);
    med.time[sizeof(med.time) - 1] = '\0';

    strncpy(med.message, item["message"] | "", sizeof(med.message) - 1);
    med.message[sizeof(med.message) - 1] = '\0';

    // Convert HH:MM into separate numbers
    sscanf(med.time, "%d:%d", &med.hour, &med.minute);

    // Reset scheduled days
    for (int i = 0; i < 7; i++) {
      med.days[i] = false;
    }

    // Read scheduled days
    JsonArray scheduledDays = item["days"].as<JsonArray>();

    for (JsonVariant day : scheduledDays) {

      int dayNumber = day.as<int>();

      if (dayNumber >= 0 && dayNumber <= 6) {
        med.days[dayNumber] = true;
      }
    }

    // Preserve reminder history for existing medications
    med.lastReminderDay = 0;

    for (int j = 0; j < previousCount; j++) {
        if (strcmp(med.id, previousIds[j]) == 0) {
            med.lastReminderDay = previousReminderDays[j];
            break;
        }
    }

    medicationCount++;

    // Print retrieved information
    SerialUSB.println("--------------------");
    SerialUSB.print("Name: ");
    SerialUSB.println(med.name);

    SerialUSB.print("Time: ");
    SerialUSB.println(med.time);

    SerialUSB.print("Message: ");
    SerialUSB.println(med.message);
  }

  client.stop();

  SerialUSB.print("Total medications loaded: ");
  SerialUSB.println(medicationCount);

  return true;
}

void fetchPet() {
    SerialUSB.println("Fetching pet data...");

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    WiFiClient client;

    if (!client.connect(serverIP, serverPort)) {
        SerialUSB.println("Failed to connect for pet sync");
        return;
    }

    client.println("GET /api/pet HTTP/1.1");
    client.print("Host: ");
    client.println(serverIP);
    client.println("Connection: close");
    client.println();

    // Wait for the response
    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 5000) {
        delay(10);
    }

    if (!client.available()) {
        client.stop();
        return;
    }

    // Skip HTTP headers
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0) {
            break;
        }
    }

    String response = client.readString();

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, response);

    if (!error) {
    // Retrieve selected pet.
    const char* newPetId = doc["petId"] | "tv";

    if (strcmp(newPetId, "tv") == 0 ||
        strcmp(newPetId, "cloud") == 0 ||
        strcmp(newPetId, "ghost") == 0) {

        strncpy(selectedPetId, newPetId, sizeof(selectedPetId) - 1);
        selectedPetId[sizeof(selectedPetId) - 1] = '\0';
    }

    // Retrieve growth points.
    uint8_t newGrowthPoints = doc["growthPoints"] | 0;

    if (newGrowthPoints != petGrowthPoints) {
        petGrowthPoints = newGrowthPoints;
        saveJinglePreference();
    }
      SerialUSB.print("Selected pet: ");
      SerialUSB.println(selectedPetId);

      SerialUSB.print("Growth points: ");
      SerialUSB.println(petGrowthPoints);

    } else {
        SerialUSB.println("Failed to parse pet data");
    }

    client.stop();
}

// ======================================================================
// MAIN LOOP
// ======================================================================

void loop() {
  unsigned long currentTime = millis();


  // ---------- Read buttons ----------
  bool upperButton = display.getButtons(TSButtonUpperLeft);
  bool lowerButton = display.getButtons(TSButtonLowerLeft);
  bool rightButton = display.getButtons(TSButtonUpperRight);

  bool upperPressed = upperButton && !previousUpperButton;
  bool lowerPressed = lowerButton && !previousLowerButton;

  static bool previousRightButton = false;
  bool rightPressed = rightButton && !previousRightButton;

  previousUpperButton = upperButton;
  previousLowerButton = lowerButton;
  previousRightButton = rightButton;


  // ---------- Periodic time synchronisation ----------
  if (WiFi.status() == WL_CONNECTED &&
      currentTime - lastSyncMillis >= SYNC_INTERVAL) {

    if (syncTimeFromNTP()) {
      lastSyncMillis = currentTime;
      timeSynced = true;
    }
  }

  // ---------- Retry pending dose uploads ----------
  if (WiFi.status() == WL_CONNECTED &&
      currentTime - lastDoseRetry >= DOSE_RETRY_INTERVAL) {

    lastDoseRetry = currentTime;
    retryPendingDoseUploads();
  }
  // ---------- Periodic medication synchronisation ----------
  if (WiFi.status() == WL_CONNECTED &&
      currentTime - lastMedicationSync >= MEDICATION_SYNC_INTERVAL) {

      if (fetchMedications()) {
          lastMedicationSync = currentTime;
          SerialUSB.println("Medication data updated!");

          if (currentScreen == MEDICATION_SCREEN) {
              if (medicationListIndex >= medicationCount) {
                  medicationListIndex = 0;
              }

              showMedicationScreen();
          }
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

  // ---------- Pet idle animation ----------
  if (currentScreen == PET_SCREEN &&
      currentTime - lastPetAnimation >= PET_FRAME_INTERVAL) {

    petFrame = (petFrame + 1) % 2;

    lastPetAnimation = currentTime;

    showPetScreen(display, petFrame, petGrowthPoints, selectedPetId);
  }

  // ---------- Handle active reminder ----------
  if (reminderActive && !reminderAcknowledged) {

    // Upper-left acknowledges the reminder
    if (upperPressed) {
      acknowledgeReminder();
    }

    // Play the repeating jingle  
    if (!reminderAcknowledged) {
      updateReminderJingle();
    }
    // Don't process menu buttons while reminder is active
    return;
  }

  // ---------- Main screen / menu controls ----------
  if (currentScreen == MAIN_SCREEN) {

      if (rightPressed) {
        currentScreen = PET_SCREEN;

        petFrame = 0;
        lastPetAnimation = millis();

        showPetScreen(display, petFrame, petGrowthPoints, selectedPetId);
      }

      else if (lowerPressed) {
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

// ---------- Medication list controls ----------
else if (currentScreen == MEDICATION_SCREEN) {

    // Lower-left: next medication
    if (lowerPressed && medicationCount > 0) {

        medicationListIndex++;

        if (medicationListIndex >= medicationCount) {
            medicationListIndex = 0;
        }

        showMedicationScreen();
    }

    // Upper-left: return to menu
    if (upperPressed) {
        currentScreen = MENU_SCREEN;
        showMenu();
    }
}

// ---------- Jingle settings controls ----------
else if (currentScreen == JINGLE_SETTINGS_SCREEN) {

  // Lower-left: move to the next jingle
  if (lowerPressed) {

    jingleMenuIndex++;

    if (jingleMenuIndex >= JINGLE_COUNT) {
      jingleMenuIndex = 0;
    }

    showJingleSettingsScreen();
  }

  // Upper-left: select the highlighted jingle
  if (upperPressed) {

  selectedJingleIndex = jingleMenuIndex;

  saveJinglePreference();

  currentScreen = MENU_SCREEN;
  showMenu();

  SerialUSB.print("Selected jingle: ");
  SerialUSB.println(selectedJingleIndex);
  }
}

// ---------- Return from other information screens ----------
else if (currentScreen == PET_SCREEN) {

    if (rightPressed) {
        currentScreen = MAIN_SCREEN;
        showMainScreen();
    }
}

else {
    if (lowerPressed) {
        currentScreen = MENU_SCREEN;
        showMenu();
    }
}

}

void updateReminderJingle() {
  unsigned long currentTime = millis();

  Jingle& jingle = jingles[selectedJingleIndex];

  if (currentTime - lastNoteChange >=
      jingle.durations[currentNote]) {

    currentNote++;

    if (currentNote >= jingle.length) {
      currentNote = 0;
    }

    if (jingle.melody[currentNote] == 0) {
      noTone(buzzerPin);
    } else {
      tone(buzzerPin, jingle.melody[currentNote]);
    }

    lastNoteChange = currentTime;
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

void testBackendConnection() {

  WiFiClient client;

  SerialUSB.println("Connecting to backend...");

  if (client.connect(serverIP, serverPort)) {

    SerialUSB.println("Backend connected!");

    client.println("GET /api/device/medications HTTP/1.1");
    client.print("Host: ");
    client.println(serverIP);
    client.println("Connection: close");
    client.println();

    SerialUSB.println("Fetching medication list...");
    SerialUSB.println("--------------------------");

    unsigned long timeout = millis();

    while (client.connected() || client.available()) {

      if (client.available()) {
        char c = client.read();
        SerialUSB.write(c);
        timeout = millis();
      }

      if (millis() - timeout > 10000) {
        break;
      }
    }

    SerialUSB.println();
    SerialUSB.println("--------------------------");

    client.stop();

  } else {
    SerialUSB.println("Could not connect to backend.");
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

    // Current date, represented as a day number
    unsigned long currentDay = rtc.getEpoch() / 86400UL;

    // Convert current date to weekday:
    // Sunday = 0, Monday = 1, ... Saturday = 6
    int currentWeekday = (currentDay + 4) % 7;

    // Check every downloaded medication
    for (int i = 0; i < medicationCount; i++) {

        Medication &med = medications[i];

        // Check whether today is a scheduled day
        if (!med.days[currentWeekday]) {
            continue;
        }

        // Check whether the scheduled time has arrived
        if (currentHour == med.hour &&
            currentMinute == med.minute &&
            med.lastReminderDay != currentDay) {

            // Remember which medication triggered the reminder
            med.lastReminderDay = currentDay;
            activeMedicationIndex = i;

            SerialUSB.print("Reminder triggered: ");
            SerialUSB.println(med.name);

            startReminder();
            break;
        }
    }
}



// ======================================================================
// START REMINDER
// ======================================================================

void startReminder() {

  reminderActive = true;
  reminderAcknowledged = false;

  // Reset jingle to the first note
  currentNote = 0;
  lastNoteChange = millis();

  // Play the first note immediately
  tone(buzzerPin, jingles[selectedJingleIndex].melody[currentNote]);

  currentScreen = MAIN_SCREEN;

  showReminderScreen();
}

bool postDoseHistory(const char* payload) {
  if (WiFi.status() != WL_CONNECTED) {
    SerialUSB.println("Wi-Fi offline.");
    return false;
  }

  WiFiClient client;

  if (!client.connect(serverIP, serverPort)) {
    SerialUSB.println("Backend connection failed.");
    return false;
  }

  client.println("POST /api/dose-history HTTP/1.1");
  client.print("Host: ");
  client.println(serverIP);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(payload));
  client.println("Connection: close");
  client.println();
  client.print(payload);

  unsigned long startWait = millis();

  while (!client.available() && millis() - startWait < 5000) {
    delay(10);
  }

  bool success = false;

  if (client.available()) {
    String statusLine = client.readStringUntil('\n');
    statusLine.trim();

    success = statusLine.startsWith("HTTP/1.1 2") ||
              statusLine.startsWith("HTTP/1.0 2");

    SerialUSB.println(statusLine);
  }

  client.stop();
  return success;
}


void queueDoseUpload(const char* payload) {
  for (int i = 0; i < MAX_PENDING_DOSES; i++) {
    if (!pendingDoseUsed[i]) {
      strncpy(pendingDosePayloads[i], payload,
              DOSE_PAYLOAD_SIZE - 1);

      pendingDosePayloads[i][DOSE_PAYLOAD_SIZE - 1] = '\0';
      pendingDoseUsed[i] = true;

      SerialUSB.println("Dose queued for retry.");
      return;
    }
  }

  SerialUSB.println("Dose retry queue is full.");
}


void retryPendingDoseUploads() {
  for (int i = 0; i < MAX_PENDING_DOSES; i++) {
    if (pendingDoseUsed[i]) {
      if (postDoseHistory(pendingDosePayloads[i])) {
        pendingDoseUsed[i] = false;
        pendingDosePayloads[i][0] = '\0';

        SerialUSB.println("Pending dose uploaded.");
      }
    }
  }
}


bool recordDoseTaken(int medicationIndex) {
  if (medicationIndex < 0 ||
      medicationIndex >= medicationCount) {
    SerialUSB.println("Invalid medication index.");
    return false;
  }

  Medication &med = medications[medicationIndex];

  unsigned long dayNumber = rtc.getEpoch() / 86400UL;

  char recordId[52];
  snprintf(recordId, sizeof(recordId),
           "%s-%lu", med.id, dayNumber);

  char timestamp[30];

  snprintf(timestamp, sizeof(timestamp),
           "%04d-%02d-%02dT%02d:%02d:%02d+08:00",
           rtc.getYear() + 2000,
           rtc.getMonth(),
           rtc.getDay(),
           rtc.getHours(),
           rtc.getMinutes(),
           rtc.getSeconds());

  StaticJsonDocument<256> doc;

  doc["id"] = recordId;
  doc["medicationId"] = med.id;
  doc["medicationName"] = med.name;
  doc["timestamp"] = timestamp;
  doc["status"] = "taken";

  char payload[DOSE_PAYLOAD_SIZE];

  size_t payloadLength =
      serializeJson(doc, payload, sizeof(payload));

  if (payloadLength == 0 ||
      payloadLength >= sizeof(payload)) {
    SerialUSB.println("JSON creation failed.");
    return false;
  }

  if (postDoseHistory(payload)) {
    SerialUSB.println("Dose history uploaded!");
    return true;
  }

  queueDoseUpload(payload);
  return false;
}

// ======================================================================
// ACKNOWLEDGE REMINDER
// ======================================================================

void acknowledgeReminder() {

    // Remember which medication was acknowledged
    int acknowledgedIndex = activeMedicationIndex;

    // Record the medication as taken
    recordDoseTaken(acknowledgedIndex);

    // Reset the active medication
    activeMedicationIndex = -1;

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
      activeMedicationIndex = -1;
      startReminder();
      break;

    case 3:
      currentScreen = DEVICE_INFO_SCREEN;
      showDeviceInfoScreen();
      break;

    case 4:
      currentScreen = JINGLE_SETTINGS_SCREEN;
      jingleMenuIndex = selectedJingleIndex;
      showJingleSettingsScreen();
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

  // Background
  display.clearScreen();
  display.drawRect(
    0, 0, 96, 64,
    TSRectangleFilled,
    BG_COLOR
  );

  // Header
  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 2);
  display.print("MEDS REMINDER");

  // Accent separator
  display.drawLine(0, 12, 95, 12, ACCENT_COLOR);

  // Clock and Date
  display.fontColor(TEXT_COLOR, BG_COLOR);

  if (timeSynced) {

      char timeString[6];
      sprintf(timeString, "%02d:%02d",
              rtc.getHours(),
              rtc.getMinutes());

      display.setCursor(18, 16);
      display.print(timeString);

      char dateString[6];
      sprintf(dateString, "%02d/%02d",
              rtc.getDay(),
              rtc.getMonth());

      display.setCursor(65, 16);
      display.print(dateString);

  } else {
      display.setCursor(18, 16);
      display.print("No time sync");
  }

  // Medication section
  display.drawLine(0, 30, 95, 30, ACCENT_COLOR);

  display.fontColor(ACCENT_COLOR, BG_COLOR);
  display.setCursor(2, 33);
  display.print("NEXT MEDICATION");

  int daysUntil = -1;
  int nextIndex = -1;

  if (timeSynced) {
    nextIndex = findNextMedication(daysUntil);
  }

  if (nextIndex >= 0) {

    Medication &med = medications[nextIndex];

    // Medication name
    display.fontColor(TEXT_COLOR, BG_COLOR);
    display.setCursor(2, 43);
    display.print(med.name);

    // Scheduled time
    display.setCursor(2, 54);
    display.print("Time: ");
    display.print(med.time);

  } else {

    display.fontColor(TEXT_COLOR, BG_COLOR);
    display.setCursor(2, 43);

    if (!timeSynced) {
      display.print("Waiting for time");
    } else {
      display.print("No upcoming meds");
    }
  }
  display.setCursor(76, 57);
  display.print("R:Pet");
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

  // Background
  display.drawRect(
    0, 0, 96, 64,
    TSRectangleFilled,
    BG_COLOR
  );

  // Header
  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 2);
  display.print("MENU");

  // Menu position
  display.setCursor(76, 2);
  display.print(menuIndex + 1);
  display.print("/");
  display.print(MENU_COUNT);

  // Header separator
  display.drawLine(0, 11, 95, 11, ACCENT_COLOR);

  // Calculate which menu items should be visible
  int firstVisible = 0;

  if (menuIndex >= 4) {
    firstVisible = menuIndex - 3;
  }

  // Display four menu options
  for (int i = 0; i < 4; i++) {

    int itemIndex = firstVisible + i;
    int y = 14 + (i * 10);

    if (itemIndex >= MENU_COUNT) {
      break;
    }

    // Highlight selected option
    if (itemIndex == menuIndex) {

      display.drawRect(
        0, y, 96, 9,
        TSRectangleFilled,
        ACCENT_COLOR
      );

      display.fontColor(BG_COLOR, ACCENT_COLOR);

      display.setCursor(4, y);
      display.print(">");
      display.print(menuItems[itemIndex]);

    } else {

      display.fontColor(TEXT_COLOR, BG_COLOR);

      display.setCursor(4, y);
      display.print(menuItems[itemIndex]);
    }
  }

  // Footer
  display.drawLine(0, 55, 95, 55, ACCENT_COLOR);

  display.fontColor(SECONDARY_COLOR, BG_COLOR);
  display.setCursor(5, 57);
  display.print("L:Scroll  U:Select");
}


int findNextMedication(int &daysUntil) {

  int currentHour = rtc.getHours();
  int currentMinute = rtc.getMinutes();

  int currentMinutes = currentHour * 60 + currentMinute;

  unsigned long currentDay = rtc.getEpoch() / 86400UL;
  int currentWeekday = (currentDay + 4) % 7;

  int bestIndex = -1;
  int shortestWait = 10080; // 7 days in minutes

  daysUntil = -1;

  for (int i = 0; i < medicationCount; i++) {

    Medication &med = medications[i];

    // Check the next 7 days
    for (int day = 0; day < 7; day++) {

      int weekday = (currentWeekday + day) % 7;

      // Skip days when this medication isn't scheduled
      if (!med.days[weekday]) {
        continue;
      }

      int scheduledMinutes =
        med.hour * 60 + med.minute;

      int waitMinutes =
        (day * 1440) + scheduledMinutes - currentMinutes;

      // Skip reminders that have already passed today
      if (waitMinutes < 0) {
        continue;
      }

      // Skip today's reminder if it has already triggered
      if (day == 0 && med.lastReminderDay == currentDay) {
        continue;
      }

      // Keep the closest upcoming reminder
      if (waitMinutes < shortestWait) {

        shortestWait = waitMinutes;
        bestIndex = i;
        daysUntil = day;
      }
    }
  }

  return bestIndex;
}


// ======================================================================
// NEXT REMINDER SCREEN
// ======================================================================


void showNextReminderScreen() {

  display.clearScreen();

  display.drawRect(
    0, 0, 96, 64,
    TSRectangleFilled,
    BG_COLOR
  );

  // Header
  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 2);
  display.print("NEXT REMINDER");

  display.drawLine(0, 12, 95, 12, ACCENT_COLOR);

  int daysUntil = -1;
  int nextIndex = findNextMedication(daysUntil);

  if (nextIndex >= 0) {

    Medication &med = medications[nextIndex];

    // Show when the reminder is scheduled
    display.fontColor(SECONDARY_COLOR, BG_COLOR);
    display.setCursor(2, 17);

    if (daysUntil == 0) {
      display.print("TODAY");
    }
    else if (daysUntil == 1) {
      display.print("TOMORROW");
    }
    else {
      display.print("IN ");
      display.print(daysUntil);
      display.print(" DAYS");
    }

    // Medication name
    display.fontColor(TEXT_COLOR, BG_COLOR);
    display.setCursor(2, 29);
    display.print(med.name);

    // Scheduled time
    display.fontColor(SECONDARY_COLOR, BG_COLOR);
    display.setCursor(2, 42);
    display.print("SCHEDULED AT");

    display.fontColor(TEXT_COLOR, BG_COLOR);
    display.setCursor(2, 51);
    display.print(med.time);

  } else {

    display.fontColor(TEXT_COLOR, BG_COLOR);
    display.setCursor(5, 29);
    display.print("No upcoming");

    display.setCursor(5, 40);
    display.print("reminders");
  }

  display.drawLine(0, 59, 95, 59, ACCENT_COLOR);

  display.setCursor(2, 60);
  display.print("L:Back");
}

// ======================================================================
// MEDICATION LIST SCREEN
// ======================================================================

void showMedicationScreen() {

  display.clearScreen();

  // Background
  display.drawRect(0, 0, 96, 64,
                   TSRectangleFilled, BG_COLOR);

  // Header
  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 2);
  display.print("MEDICATIONS");

  display.drawLine(0, 12, 95, 12, ACCENT_COLOR);

  if (medicationCount == 0) {

    display.setCursor(8, 27);
    display.print("No medications");

    display.setCursor(5, 57);
    display.print("U:Back");

    return;
  }

  // Medication name
  display.fontColor(SECONDARY_COLOR, BG_COLOR);
  display.setCursor(2, 17);
  display.print("MEDICATION NAME");

  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 28);
  display.print(medications[medicationListIndex].name);

  // Medication time
  display.fontColor(SECONDARY_COLOR, BG_COLOR);
  display.setCursor(2, 40);
  display.print("SCHEDULED TIME");

  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 49);
  display.print(medications[medicationListIndex].time);

  // Footer
  display.drawLine(0, 58, 95, 58, ACCENT_COLOR);

  display.setCursor(2, 59);
  display.print(medicationListIndex + 1);
  display.print("/");
  display.print(medicationCount);
}

void showJingleSettingsScreen() {

  display.clearScreen();

  // Background
  display.drawRect(
    0, 0, 96, 64,
    TSRectangleFilled,
    BG_COLOR
  );

  // Header
  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 2);
  display.print("JINGLE SETTINGS");

  display.drawLine(0, 12, 95, 12, ACCENT_COLOR);

  // Available jingles
  const char* jingleNames[] = {
    "Cute Chime",
    "Retro Game",
    "Gentle Bell"
  };

  for (int i = 0; i < JINGLE_COUNT; i++) {

    int y = 17 + (i * 11);

    if (i == jingleMenuIndex) {

      display.drawRect(
        0, y, 96, 10,
        TSRectangleFilled,
        ACCENT_COLOR
      );

      display.fontColor(BG_COLOR, ACCENT_COLOR);

      display.setCursor(4, y);
      display.print("> ");
      display.print(jingleNames[i]);

    } else {

      display.fontColor(TEXT_COLOR, BG_COLOR);

      display.setCursor(4, y);
      display.print(jingleNames[i]);
    }

    // Mark the currently saved jingle
    if (i == selectedJingleIndex) {
      display.setCursor(83, y);
      display.print("*");
    }
  }

  // Footer
  display.drawLine(0, 51, 95, 51, ACCENT_COLOR);

  display.fontColor(SECONDARY_COLOR, BG_COLOR);
  display.setCursor(2, 54);
  display.print("L:Next  U:Select");
}

// ======================================================================
// DEVICE INFORMATION
// ======================================================================


void showDeviceInfoScreen() {

  display.clearScreen();

  display.drawRect(0, 0, 96, 64,
                   TSRectangleFilled, BG_COLOR);

  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 2);
  display.print("DEVICE INFO");

  display.drawLine(0, 12, 95, 12, ACCENT_COLOR);

  display.setCursor(2, 19);
  display.print("Meds Keychain");

  display.setCursor(2, 31);
  display.print("Prototype v1.0");

  display.setCursor(2, 43);

  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: Online");
  } else {
    display.print("WiFi: Offline");
  }

  display.drawLine(0, 55, 95, 55, ACCENT_COLOR);

  display.setCursor(2, 57);
  display.print("L:Back");
}


// ======================================================================
// REMINDER SCREEN
// ======================================================================

void showReminderScreen() {

  display.clearScreen();

  // Background
  display.drawRect(
    0, 0, 96, 64,
    TSRectangleFilled,
    BG_COLOR
  );

  // Header
  display.fontColor(ACCENT_COLOR, BG_COLOR);
  display.setCursor(2, 3);
  display.print("MEDICATION ALERT");

  display.drawLine(0, 14, 95, 14, ACCENT_COLOR);

  // Reminder title
  display.fontColor(SECONDARY_COLOR, BG_COLOR);
  display.setCursor(2, 19);
  display.print("TIME FOR MEDS!");

  // Medication name and custom message
  display.fontColor(TEXT_COLOR, BG_COLOR);

  if (activeMedicationIndex >= 0 &&
      activeMedicationIndex < medicationCount) {

    Medication &med = medications[activeMedicationIndex];

    display.setCursor(2, 30);
    display.print(med.name);

    display.setCursor(2, 40);
    display.print(med.message);

  } else {

    display.setCursor(2, 30);
    display.print("Test Reminder");
  }

  // Acknowledgement instruction
  display.drawLine(0, 51, 95, 51, ACCENT_COLOR);

  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(2, 55);
  display.print("U: Acknowledge");
}

// ======================================================================
// ACKNOWLEDGED SCREEN
// ======================================================================

void showAcknowledgedScreen() {

  display.clearScreen();

  display.drawRect(0, 0, 96, 64,
                   TSRectangleFilled, BG_COLOR);

  display.fontColor(ACCENT_COLOR, BG_COLOR);
  display.setCursor(13, 9);
  display.print("REMINDER");

  display.drawLine(0, 21, 95, 21, ACCENT_COLOR);

  display.fontColor(TEXT_COLOR, BG_COLOR);
  display.setCursor(7, 28);
  display.print("Acknowledged!");

  display.fontColor(SECONDARY_COLOR, BG_COLOR);
  display.setCursor(12, 45);
  display.print("Take care :)");
}
