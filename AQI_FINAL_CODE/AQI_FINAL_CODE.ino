#define BLYNK_TEMPLATE_NAME "Smart Air Quality Monitoring System"
#define BLYNK_AUTH_TOKEN "kRafFsK8OBEOBWgz2cxGL5fX14goOE05"
#define BLYNK_TEMPLATE_ID "TMPL6c7Jmy5dT"

#define BLYNK_PRINT Serial
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <SoftwareSerial.h>
#include "DHT.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// Wi-Fi credentials
char ssid[] = "Dumini";
char pass[] = "Dumini2002@";

// Blynk Auth
char auth[] = BLYNK_AUTH_TOKEN;

// Telegram
#define BOT_TOKEN "7503084212:AAHb87iqVSS7hFSmt9x1rS-zDZSYAl4WSkI"
#define CHAT_ID "1511625568"
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// MH-Z19 CO2 Sensor
#define MHZ19_RX D7
#define MHZ19_TX D6
SoftwareSerial mhzSerial(MHZ19_TX, MHZ19_RX);
byte response[9];
const byte READ_CO2_CMD[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};

// PMS5003 PM Sensor
SoftwareSerial pmsSerial(D5, D6); // TX/RX pins for PMS5003
const uint8_t FRAME_LENGTH = 32;
uint8_t pmsData[FRAME_LENGTH];

// DHT22 Sensor
#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// OLED Display
Adafruit_SSD1306 oled(128, 64, &Wire, D0);

// VOC Sensor
#define VOC_PIN A0
int baseline = 0;

// Telegram alert control
String lastSentAirQuality = "";

int readCO2() {
  byte cmdRead[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
  byte response[9];
  mhzSerial.write(cmdRead, 9);
  delay(10);
  if (mhzSerial.available() >= 9) {
    for (int i = 0; i < 9; i++) {
      response[i] = mhzSerial.read();
    }
    if (response[0] == 0xFF && response[1] == 0x86) {
      return (response[2] << 8) + response[3];
    }
  }
  return -1;
}

bool readPMSData() {
  if (pmsSerial.read() != 0x42 || pmsSerial.read() != 0x4D) return false;
  pmsData[0] = 0x42;
  pmsData[1] = 0x4D;
  for (uint8_t i = 2; i < FRAME_LENGTH; i++) {
    pmsData[i] = pmsSerial.read();
  }
  uint16_t checksum = 0;
  for (uint8_t i = 0; i < FRAME_LENGTH - 2; i++) {
    checksum += pmsData[i];
  }
  uint16_t receivedChecksum = (pmsData[30] << 8) + pmsData[31];
  return (checksum == receivedChecksum);
}

int getBaseline() {
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += analogRead(VOC_PIN);
    delay(50);
  }
  return sum / 100;
}

int calculateCO2AQI(int co2) {
  if (co2 <= 400) return map(co2, 0, 400, 0, 50);
  if (co2 <= 1000) return map(co2, 400, 1000, 51, 100);
  if (co2 <= 2000) return map(co2, 1000, 2000, 101, 150);
  if (co2 <= 3000) return map(co2, 2000, 3000, 151, 200);
  if (co2 <= 5000) return map(co2, 3000, 5000, 201, 300);
  return 300;
}

int calculatePM2_5AQI(int pm2_5) {
  if (pm2_5 <= 12) return map(pm2_5, 0, 12, 0, 50);
  if (pm2_5 <= 35) return map(pm2_5, 13, 35, 51, 100);
  if (pm2_5 <= 55) return map(pm2_5, 36, 55, 101, 150);
  if (pm2_5 <= 150) return map(pm2_5, 56, 150, 151, 200);
  if (pm2_5 <= 250) return map(pm2_5, 151, 250, 201, 300);
  return 300;
}

int calculateVOCaqi(int vocLevel) {
  if (vocLevel <= 100) return map(vocLevel, 0, 100, 0, 50);
  if (vocLevel <= 200) return map(vocLevel, 101, 200, 51, 100);
  if (vocLevel <= 300) return map(vocLevel, 201, 300, 101, 150);
  if (vocLevel <= 400) return map(vocLevel, 301, 400, 151, 200);
  if (vocLevel <= 500) return map(vocLevel, 401, 500, 201, 300);
  return 300;
}

void sendTelegramAlert(String message) {
  bot.sendMessage(CHAT_ID, message, "");
}

void setup() {
  Serial.begin(115200);
  Blynk.begin(auth, ssid, pass);
  mhzSerial.begin(9600);
  pmsSerial.begin(9600);
  dht.begin();
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay();
  oled.setTextColor(WHITE);
  pinMode(VOC_PIN, INPUT);

  // Telegram certificate check disabled
  secured_client.setInsecure();

  Serial.println("Calibrating Sensors...");
  delay(10000);
  baseline = getBaseline();
  Serial.print("VOC Baseline: ");
  Serial.println(baseline);
}

void loop() {
  Blynk.run();

  // Read sensors
  int co2 = readCO2();
  int pm2_5 = -1;
  if (pmsSerial.available() >= FRAME_LENGTH && readPMSData()) {
    pm2_5 = (pmsData[12] << 8) + pmsData[13];
  }

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  int vocLevel = analogRead(VOC_PIN) - baseline;
  if (vocLevel < 0) vocLevel = 0;

  // AQI Calculations
  int aqiCO2 = calculateCO2AQI(co2);
  int aqiPM2_5 = calculatePM2_5AQI(pm2_5);
  int aqiVOC = calculateVOCaqi(vocLevel);
  int maxAQI = max(aqiCO2, max(aqiPM2_5, aqiVOC));

  // Air quality status
  String airQuality = "Good";
  if (maxAQI > 300) airQuality = "Hazardous";
  else if (maxAQI > 200) airQuality = "Very Unhealthy";
  else if (maxAQI > 150) airQuality = "Unhealthy";
  else if (maxAQI > 100) airQuality = "Unhealthy for Sensitive Groups";
  else if (maxAQI > 50) airQuality = "Moderate";

    // Send to Blynk
  Blynk.virtualWrite(V0, co2);
  Blynk.virtualWrite(V1, pm2_5);
  Blynk.virtualWrite(V2, vocLevel);
  Blynk.virtualWrite(V3, temperature);
  Blynk.virtualWrite(V4, humidity);
  Blynk.virtualWrite(V5, maxAQI);

  // Display on OLED
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.printf("CO2: %d ppm\n", co2);
  oled.printf("PM2.5: %d ug/m3\n", pm2_5);
  oled.printf("Temp: %.1f C\n", temperature);
  oled.printf("Humidity: %.1f %%\n", humidity);
  oled.printf("VOC: %d\n", vocLevel);
  oled.printf("AQI: %d\n", maxAQI);
  oled.printf("Status: %s\n", airQuality.c_str());
  oled.display();

  // Serial output
  Serial.println("====================================");
  Serial.printf("CO2: %d ppm\n", co2);
  Serial.printf("PM2.5: %d ug/m3\n", pm2_5);
  Serial.printf("Temp: %.1f °C\n", temperature);
  Serial.printf("Humidity: %.1f %%\n", humidity);
  Serial.printf("VOC: %d\n", vocLevel);
  Serial.printf("AQI: %d\n", maxAQI);
  Serial.printf("Air Quality: %s\n", airQuality.c_str());
  Serial.println("====================================");

  // Telegram alert logic
    // Telegram alert logic
  if (airQuality == "Good" || airQuality == "Moderate" || airQuality == "Unhealthy" || airQuality == "Unhealthy for Sensitive Group" || airQuality == "Very Unhealthy" || airQuality == "Hazardous") {
    if (lastSentAirQuality != airQuality) {
      String alert = "🚨 *Air Quality Alert!* 🚨\n";
      alert += "*Status:* " + airQuality + "\n";
      alert += "*AQI:* " + String(maxAQI) + "\n";
      alert += "*CO₂:* " + String(co2) + " ppm\n";
      alert += "*PM2.5:* " + String(pm2_5) + " µg/m³\n";
      alert += "*VOC:* " + String(vocLevel) + "\n";
      alert += "*Temp:* " + String(temperature, 1) + " °C\n";
      alert += "*Humidity:* " + String(humidity, 1) + " %";
      sendTelegramAlert(alert);
      lastSentAirQuality = airQuality;
    }
  } else {
    lastSentAirQuality = ""; // reset so it can send again later
  }


  delay(5000);
}

