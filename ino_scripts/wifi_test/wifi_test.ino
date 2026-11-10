#include <WiFi101.h>
#include <WiFiUdp.h>
#include <RTCZero.h>

// -------- Wi-Fi credentials --------
char ssid[] = "YOUR_WIFI_NAME";
char wifiPassword[] = "YOUR_WIFI_PASSWORD";

// -------- NTP / timezone --------
const char* ntpServer = "pool.ntp.org";
const unsigned int NTP_PORT = 123;
const unsigned long NTP_EPOCH_OFFSET = 2208988800UL;
const long SINGAPORE_OFFSET = 8L * 60L * 60L; // UTC+8

WiFiUDP udp;
RTCZero rtc;

byte ntpPacket[48];

void connectWiFi() {
  WiFi.setPins(8, 2, A3, -1);  // Required for TinyCircuits WiFi TinyShield

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
  } else {
    SerialUSB.println("Wi-Fi connection failed.");
  }
}

bool syncTimeFromNTP() {
  IPAddress ntpIP;

  if (WiFi.hostByName(ntpServer, ntpIP) != 1) {
    SerialUSB.println("Could not resolve NTP server.");
    return false;
  }

  memset(ntpPacket, 0, sizeof(ntpPacket));

  // NTP request: client mode, version 4
  ntpPacket[0] = 0b11100011;
  ntpPacket[1] = 0;
  ntpPacket[2] = 6;
  ntpPacket[3] = 0xEC;

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
        ((unsigned long)ntpPacket[42] << 8)  |
        ((unsigned long)ntpPacket[43]);

      unsigned long utcEpoch =
        secondsSince1900 - NTP_EPOCH_OFFSET;

      // Set RTC to Singapore local time (UTC+8)
      rtc.setEpoch(utcEpoch + SINGAPORE_OFFSET);

      SerialUSB.println("Time synced!");
      return true;
    }

    delay(10);
  }

  SerialUSB.println("NTP request timed out.");
  return false;
}

void printTime() {
  char timeString[9];

  sprintf(timeString, "%02d:%02d:%02d",
          rtc.getHours(),
          rtc.getMinutes(),
          rtc.getSeconds());

  SerialUSB.print("Singapore time: ");
  SerialUSB.println(timeString);
}

void setup() {
  SerialUSB.begin(9600);
  delay(1500);

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    rtc.begin();

    if (syncTimeFromNTP()) {
      printTime();
    }
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    printTime();
  }

  delay(1000);
}