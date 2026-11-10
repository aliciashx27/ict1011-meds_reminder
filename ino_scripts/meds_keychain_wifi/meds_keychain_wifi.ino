/*
  Meds Reminder Keychain - Wi-Fi version
  TinyScreen+ / SAMD21, Wi-Fi TinyShield ASD2123-R, Buzzer Wireling A1
  ONE button: upper-left acknowledges an active reminder only.

  Before Wi-Fi use: install WiFi101, RTCZero, Wireling, TinyScreen,
  and FlashStorage libraries; configure WIFI_SSID, WIFI_PASSWORD,
  SERVER_HOST, SERVER_PORT and SERVER_PATH below.

  Schedule server response (HTTP 200, text/plain), one line per reminder:
    Vitamin C|08:00|127|Take after breakfast
    Medicine B|20:30|62|Take with water
  Days bitmask: Sunday=1, Monday=2, Tuesday=4, Wednesday=8,
  Thursday=16, Friday=32, Saturday=64; 127 = every day.
  Names and messages must not contain | or newline.

  Serial Monitor at 9600, newline enabled, for offline testing:
    TIME 2026-11-08 19:45:00
    ADD Vitamin C|19:46|127|Take after food
    LIST
    CLEAR
    SAVE
    SYNC

  IMPORTANT: RTCZero does not guarantee time across complete power loss.
  On reboot, reconnect Wi-Fi for NTP or set TIME via Serial Monitor.
*/

#include <Arduino.h>
#include <Wireling.h>
#include <TinyScreen.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <RTCZero.h>
#include <FlashStorage.h>
#include <string.h>
#include <stdlib.h>

// ---------------- User settings ----------------
const char WIFI_SSID[] = "Am1a";
const char WIFI_PASSWORD[] = "Mizu5traumabells";
const char SERVER_HOST[] = "10.157.216.6";  // Replace with your server host
const uint16_t SERVER_PORT = 5050;             // HTTP prototype only
const char SERVER_PATH[] = "/api/device/medications?device_id=demo01";
const bool ENABLE_WIFI = true; // Set true only when Wi-Fi TinyShield is installed

// ---------------- Hardware ----------------
const int BUZZER_PIN = A1;
const uint16_t BUZZER_HZ = 1000;
const unsigned long BEEP_ON_MS = 500;
const unsigned long BEEP_OFF_MS = 300;
const unsigned long DISPLAY_REFRESH_MS = 1000;
const unsigned long WIFI_RETRY_MS = 60000UL;
const unsigned long SYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
const long SG_UTC_OFFSET_SECONDS = 8L * 3600L;
const uint32_t NTP_TO_UNIX = 2208988800UL;
const uint8_t MAX_MEDS = 10;

TinyScreen display = TinyScreen(TinyScreenPlus);
RTCZero rtc;
WiFiUDP udp;

// Fixed-size records are safe to store in flash (no String pointers).
struct Medication {
  char name[25];
  char message[45];
  uint8_t hour;
  uint8_t minute;
  uint8_t daysMask;
};
struct SavedSchedules {
  uint32_t magic;
  uint8_t count;
  Medication meds[MAX_MEDS];
  uint32_t checksum;
};
FlashStorage(scheduleStorage, SavedSchedules);
const uint32_t STORAGE_MAGIC = 0x4D454453UL;

Medication medications[MAX_MEDS];
uint8_t medicationCount = 0;
uint32_t lastTriggeredDay[MAX_MEDS];
int activeMedication = -1;
bool timeSynced = false;
bool buzzerOn = false;
bool previousButton = false;
unsigned long lastBeepMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastWiFiAttemptMs = 0;
unsigned long lastSyncMs = 0;
unsigned long acknowledgementShownMs = 0;
bool acknowledgementVisible = false;
String serialLine;

// ---------------- Helpers ----------------
void safeCopy(char *dest, size_t capacity, const String &source) {
  if (capacity == 0) return;
  source.toCharArray(dest, capacity);
}

uint32_t checksumSchedules(const SavedSchedules &data) {
  // Simple corruption check, not a cryptographic signature.
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&data);
  const size_t length = sizeof(SavedSchedules) - sizeof(uint32_t);
  uint32_t result = 2166136261UL;
  for (size_t i = 0; i < length; ++i) {
    result ^= bytes[i];
    result *= 16777619UL;
  }
  return result;
}

void saveSchedules() {
  SavedSchedules data = {};
  data.magic = STORAGE_MAGIC;
  data.count = medicationCount;
  for (uint8_t i = 0; i < medicationCount; ++i) data.meds[i] = medications[i];
  data.checksum = checksumSchedules(data);
  scheduleStorage.write(data); // Only on edits/sync, not each loop.
  SerialUSB.println("Schedules saved to flash.");
}

void loadSchedules() {
  SavedSchedules data = scheduleStorage.read();
  if (data.magic != STORAGE_MAGIC || data.count > MAX_MEDS ||
      data.checksum != checksumSchedules(data)) {
    SerialUSB.println("No valid saved schedules; starting empty.");
    return;
  }
  medicationCount = data.count;
  for (uint8_t i = 0; i < medicationCount; ++i) {
    medications[i] = data.meds[i];
    medications[i].name[sizeof(medications[i].name) - 1] = '\0';
    medications[i].message[sizeof(medications[i].message) - 1] = '\0';
  }
  SerialUSB.print("Loaded schedules: ");
  SerialUSB.println(medicationCount);
}

void drawText(int x, int y, const char *text) {
  display.setCursor(x, y);
  display.print(text);
}

void showMainScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);
  drawText(0, 1, "MEDS REMINDER");
  char line[28];
  if (timeSynced) {
    snprintf(line, sizeof(line), "%02u:%02u:%02u", rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
    drawText(0, 16, line);
  } else {
    drawText(0, 16, "Clock not set");
  }
  snprintf(line, sizeof(line), "Meds: %u", medicationCount);
  drawText(0, 31, line);
  drawText(0, 46, ENABLE_WIFI ? "Wi-Fi mode" : "Offline test");
}

void showReminderScreen() {
  if (activeMedication < 0) return;
  const Medication &m = medications[activeMedication];
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);
  drawText(0, 1, "TIME FOR MEDS!");
  drawText(0, 17, m.name);
  // TinyScreen+ is narrow: display first ~17 chars of message.
  char shortMessage[18];
  strncpy(shortMessage, m.message, sizeof(shortMessage) - 1);
  shortMessage[sizeof(shortMessage) - 1] = '\0';
  drawText(0, 34, shortMessage);
  drawText(0, 51, "Press UL: stop");
}

void showAcknowledgedScreen() {
  display.clearScreen();
  display.fontColor(0xFFFF, 0x0000);
  drawText(0, 15, "Acknowledged!");
  drawText(0, 35, "Buzzer stopped");
}

bool parseSchedule(const String &line, Medication &med) {
  int a = line.indexOf('|');
  int b = line.indexOf('|', a + 1);
  int c = line.indexOf('|', b + 1);
  if (a <= 0 || b < 0 || c < 0) return false;
  String name = line.substring(0, a); name.trim();
  String clock = line.substring(a + 1, b); clock.trim();
  String days = line.substring(b + 1, c); days.trim();
  String message = line.substring(c + 1); message.trim();
  if (name.length() == 0 || name.length() >= sizeof(med.name) ||
      message.length() >= sizeof(med.message) || clock.length() != 5 ||
      clock[2] != ':' || !isDigit(clock[0]) || !isDigit(clock[1]) ||
      !isDigit(clock[3]) || !isDigit(clock[4])) return false;
  int hour = clock.substring(0, 2).toInt();
  int minute = clock.substring(3, 5).toInt();
  if (hour > 23 || minute > 59 || days.length() == 0) return false;
  for (unsigned int i = 0; i < days.length(); ++i) if (!isDigit(days[i])) return false;
  int mask = days.toInt();
  if (mask < 1 || mask > 127) return false;
  memset(&med, 0, sizeof(med));
  safeCopy(med.name, sizeof(med.name), name);
  safeCopy(med.message, sizeof(med.message), message);
  med.hour = hour;
  med.minute = minute;
  med.daysMask = mask;
  return true;
}

void listSchedules() {
  SerialUSB.print("Medication count: "); SerialUSB.println(medicationCount);
  for (uint8_t i = 0; i < medicationCount; ++i) {
    SerialUSB.print(i + 1); SerialUSB.print(". ");
    SerialUSB.print(medications[i].name); SerialUSB.print(" ");
    if (medications[i].hour < 10) SerialUSB.print('0');
    SerialUSB.print(medications[i].hour); SerialUSB.print(':');
    if (medications[i].minute < 10) SerialUSB.print('0');
    SerialUSB.print(medications[i].minute); SerialUSB.print(" days=");
    SerialUSB.println(medications[i].daysMask);
  }
}

bool setTimeFromSerial(const String &input) {
  // Input is Singapore local time: YYYY-MM-DD HH:MM:SS.
  if (input.length() != 19 || input[4] != '-' || input[7] != '-' ||
      input[10] != ' ' || input[13] != ':' || input[16] != ':') return false;
  for (uint8_t i = 0; i < 19; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (!isDigit(input[i])) return false;
  }
  int year = input.substring(0, 4).toInt();
  int month = input.substring(5, 7).toInt();
  int day = input.substring(8, 10).toInt();
  int hour = input.substring(11, 13).toInt();
  int minute = input.substring(14, 16).toInt();
  int second = input.substring(17, 19).toInt();
  if (year < 2000 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return false;
  rtc.setDate(day, month, year - 2000);
  rtc.setTime(hour, minute, second);
  timeSynced = true;
  showMainScreen();
  return true;
}

void handleSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.startsWith("TIME ")) {
    SerialUSB.println(setTimeFromSerial(cmd.substring(5)) ? "Clock set." : "Invalid TIME format.");
  } else if (cmd.startsWith("ADD ")) {
    if (medicationCount >= MAX_MEDS) { SerialUSB.println("List full."); return; }
    Medication med;
    if (!parseSchedule(cmd.substring(4), med)) { SerialUSB.println("Invalid medication format."); return; }
    medications[medicationCount++] = med;
    saveSchedules();
    showMainScreen();
  } else if (cmd == "CLEAR") {
    medicationCount = 0;
    if (activeMedication >= 0) {
      noTone(BUZZER_PIN); buzzerOn = false; activeMedication = -1;
    }
    saveSchedules(); showMainScreen();
  } else if (cmd == "LIST") {
    listSchedules();
  } else if (cmd == "SAVE") {
    saveSchedules();
  } else if (cmd == "SYNC") {
    SerialUSB.println("Sync request received.");
    // Actual Wi-Fi sync is handled in loop when ENABLE_WIFI is true.
    lastSyncMs = 0;
  } else {
    SerialUSB.println("Commands: TIME, ADD, LIST, CLEAR, SAVE, SYNC");
  }
}

void pollSerial() {
  while (SerialUSB.available()) {
    char ch = (char)SerialUSB.read();
    if (ch == '\n' || ch == '\r') {
      if (serialLine.length()) { handleSerialCommand(serialLine); serialLine = ""; }
    } else if (serialLine.length() < 120) {
      serialLine += ch;
    } else {
      serialLine = "";
      SerialUSB.println("Command too long.");
    }
  }
}

void scan_networks() {
  SerialUSB.println("Starting WiFi scan...");
  int numNetworks = WiFi.scanNetworks();
  SerialUSB.print("Networks found: ");
  SerialUSB.println(numNetworks);
  for (int i = 0; i < numNetworks; i++) {
      SerialUSB.println(WiFi.SSID(i));
  }
}

void connectWiFi() {
  // Original project pin configuration for Wi-Fi TinyShield ASD2123-R.
  WiFi.setPins(8, 2, A3, -1);
  // if wifi disabled, or already connected, cancel connection request
  if (!ENABLE_WIFI || WiFi.status() == WL_CONNECTED) return;

  SerialUSB.println(WiFi.status());
  scan_networks();

  // Verify wifi shield is installed
  if (WiFi.status() == WL_NO_SHIELD) {
    SerialUSB.println("Wi-Fi shield NOT detected!");
    return;
  }
  SerialUSB.println("Wi-Fi shield detected!");


  SerialUSB.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long started = millis();
  // wait 15 seconds to connect; wait 50 seconds between attempts
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000UL) delay(50);
  SerialUSB.println(WiFi.status() == WL_CONNECTED ? "Wi-Fi connected." : "Wi-Fi unavailable.");
}

bool syncClockNTP() {
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress ntpIP;
  if (WiFi.hostByName("pool.ntp.org", ntpIP) != 1) return false;
  uint8_t packet[48] = {};
  packet[0] = 0x1B;
  udp.stop();
  if (!udp.begin(2390)) return false;
  udp.beginPacket(ntpIP, 123);
  udp.write(packet, sizeof(packet));
  udp.endPacket();
  unsigned long started = millis();
  while (millis() - started < 3000UL) {
    if (udp.parsePacket() >= 48) {
      udp.read(packet, 48);
      uint32_t ntpSeconds = ((uint32_t)packet[40] << 24) |
                            ((uint32_t)packet[41] << 16) |
                            ((uint32_t)packet[42] << 8) | packet[43];
      if (ntpSeconds < NTP_TO_UNIX) return false;
      rtc.setEpoch(ntpSeconds - NTP_TO_UNIX + SG_UTC_OFFSET_SECONDS);
      timeSynced = true;
      SerialUSB.println("Clock synchronised (Singapore time).");
      return true;
    }
    delay(10);
  }
  return false;
}

bool fetchSchedules() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (strcmp(SERVER_HOST, "example.com") == 0) {
    SerialUSB.println("Set SERVER_HOST and SERVER_PATH first.");
    return false;
  }
  WiFiClient client;
  SerialUSB.println("debug statement 1");
  if (!client.connect(SERVER_HOST, SERVER_PORT)) return false;
  SerialUSB.println("debug statement 2");
  client.print("GET "); client.print(SERVER_PATH); client.println(" HTTP/1.1");
  client.print("Host: "); client.println(SERVER_HOST);
  client.println("Connection: close");
  client.println();
  client.setTimeout(2000);
  String status = client.readStringUntil('\n'); status.trim();
  if (!status.startsWith("HTTP/1.1 200") && !status.startsWith("HTTP/1.0 200")) {
    SerialUSB.println("Schedule request failed."); client.stop(); return false;
  }
  // Skip headers. Require a bounded, plain-text, identity-encoded response.
  bool supportedEncoding = true;
  while (client.connected() || client.available()) {
    String header = client.readStringUntil('\n'); header.trim();
    if (header.length() == 0) break;
    String lower = header; lower.toLowerCase();
    if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0)
      supportedEncoding = false;
  }
  if (!supportedEncoding) {
    SerialUSB.println("Server must send non-chunked text/plain.");
    client.stop(); return false;
  }
  Medication incoming[MAX_MEDS] = {};
  uint8_t count = 0;
  bool invalid = false;
  unsigned long started = millis();
  while ((client.connected() || client.available()) && millis() - started < 6000UL) {
    if (!client.available()) { delay(5); continue; }
    String line = client.readStringUntil('\n'); line.trim();
    if (!line.length()) continue;
    if (line.length() > 120 || count >= MAX_MEDS ||
        !parseSchedule(line, incoming[count])) { invalid = true; break; }
    count++;
  }
  client.stop();
  if (invalid || millis() - started >= 6000UL) {
    SerialUSB.println("Invalid/incomplete schedule response; keeping saved list.");
    return false;
  }
  // Don't replace active reminder while it is sounding.
  if (activeMedication >= 0) return false;
  medicationCount = count;
  for (uint8_t i = 0; i < count; ++i) medications[i] = incoming[i];
  for (uint8_t i = 0; i < MAX_MEDS; ++i) lastTriggeredDay[i] = 0;
  saveSchedules();
  showMainScreen();
  SerialUSB.println("Schedules downloaded.");
  return true;
}

void startReminder(uint8_t index) {
  activeMedication = index;
  buzzerOn = true;
  lastBeepMs = millis();
  tone(BUZZER_PIN, BUZZER_HZ);
  showReminderScreen();
  SerialUSB.print("REMINDER: "); SerialUSB.println(medications[index].name);
}

void acknowledgeReminder() {
  noTone(BUZZER_PIN);
  buzzerOn = false;
  activeMedication = -1;
  acknowledgementVisible = true;
  acknowledgementShownMs = millis();
  showAcknowledgedScreen();
}

void checkReminders() {
  if (!timeSynced || activeMedication >= 0) return;
  uint32_t localEpoch = rtc.getEpoch();
  uint32_t dayNumber = localEpoch / 86400UL;
  uint8_t weekday = (dayNumber + 4UL) % 7; // Unix epoch day 0 was Thursday.
  uint8_t weekdayBit = 1 << weekday;       // Sunday=bit0.
  for (uint8_t i = 0; i < medicationCount; ++i) {
    Medication &med = medications[i];
    if ((med.daysMask & weekdayBit) && med.hour == rtc.getHours() &&
        med.minute == rtc.getMinutes() && lastTriggeredDay[i] != dayNumber) {
      lastTriggeredDay[i] = dayNumber;
      startReminder(i);
      return;
    }
  }
}

void setup() {
  Wireling.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  display.begin();
  display.setBrightness(10);
  display.setBitDepth(16);
  display.setFont(liberationSans_8ptFontInfo);
  SerialUSB.begin(9600);
  rtc.begin();
  loadSchedules();
  showMainScreen();
  if (ENABLE_WIFI) {
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      syncClockNTP();
      fetchSchedules();
      lastSyncMs = millis();
    }
  }
  SerialUSB.println("Ready. Commands: TIME, ADD, LIST, CLEAR, SAVE, SYNC");
}

void loop() {
  unsigned long now = millis();
  pollSerial();

  // Only upper-left is used; it ONLY acknowledges a sounding reminder.
  bool buttonDown = display.getButtons(TSButtonUpperLeft);
  bool pressed = buttonDown && !previousButton;
  previousButton = buttonDown;
  if (pressed && activeMedication >= 0) acknowledgeReminder();

  if (ENABLE_WIFI) {
    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWiFiAttemptMs >= WIFI_RETRY_MS) {
        lastWiFiAttemptMs = now;
        connectWiFi();
      }
    } else if (!timeSynced || now - lastSyncMs >= SYNC_INTERVAL_MS) {
      syncClockNTP();
      fetchSchedules();
      lastSyncMs = millis();
    }
  }

  if (activeMedication >= 0) {
    if (buzzerOn && now - lastBeepMs >= BEEP_ON_MS) {
      noTone(BUZZER_PIN); buzzerOn = false; lastBeepMs = now;
    } else if (!buzzerOn && now - lastBeepMs >= BEEP_OFF_MS) {
      tone(BUZZER_PIN, BUZZER_HZ); buzzerOn = true; lastBeepMs = now;
    }
  } else {
    if (acknowledgementVisible && now - acknowledgementShownMs >= 2000UL)
      acknowledgementVisible = false;
    checkReminders();
    if (activeMedication < 0 && !acknowledgementVisible &&
        now - lastDisplayMs >= DISPLAY_REFRESH_MS) {
      lastDisplayMs = now;
      showMainScreen();
    }
  }
}
