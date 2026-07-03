/*
 * ESP32-S3 N16R8 — Smart Ventilation & Power Monitor + Global MQTT
 * Display: ST7796 480x320 | Touch: XPT2046 | Sensors: 3xDHT22, 3xZMPT101B
 * Modem: A7670E (UART1) | Relay: GPIO3
 *
 * Fan: AND logic — ALL sensor thresholds + weather must pass. 5s ON delay.
 * Power: SMS after 3min, reminders 5min, restore after 1min.
 * MQTT: Publishes to broker.hivemq.com for global web dashboard access.
 */

#include "DHT.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "splash.h"

// ─── WiFi (stored in Preferences) ───
String wifiSSID = "dk";
String wifiPASS = "dakshan11";
bool wifiReconnect = false;

// ─── Weather API ───
String apiKey = "dcab3201221d829ec13178a2d01c6201";
String city = "Braunschweig", country = "DE";

// ─── MQTT (Global Access) ───
const char *MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORT = 1883;
String mqttId, mqttDataTopic, mqttCmdTopic;

// ─── Pins ───
#define DHT_OUT 2
#define DHT_IN1 5
#define DHT_IN2 6
#define RELAY_PIN 10
#define BL_PIN 11
#define VSENS1 1
#define VSENS2 7
#define VSENS3 8
#define MODEM_RX 16
#define MODEM_TX 17
#define SIM_CODE "4176"

// ─── Timing (ms) ───
#define SENS_INT 10000
#define WEATH_INT 300000
#define SMS_INIT 180000 // 3 min
#define SMS_REM 300000  // 5 min
#define SMS_REST 60000  // 1 min
#define MODEM_INT 60000
#define MQTT_INT 10000

// ─── UI ───
enum Page { PG_MAIN, PG_SET1, PG_SET2, PG_KEYPAD, PG_WIFI, PG_QWERTY };
enum FanMode { FM_AUTO, FM_ON, FM_OFF };

#define C_BG 0x1129
#define C_TITLE 0x32AE
#define C_PANEL 0x226B
#define C_TEXT TFT_WHITE
#define C_DATA TFT_YELLOW
#define C_ACC TFT_CYAN
#define C_BTN 0x5415
#define C_OK 0x4528
#define C_DEL 0xC2A5
#define C_ACK 0xFB20

// ─── Objects ───
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
WiFiClient wifiCli;
PubSubClient mqtt;
DHT dhtIn1(DHT_IN1, DHT22), dhtIn2(DHT_IN2, DHT22), dhtOut(DHT_OUT, DHT22);

// ─── Sensor Thresholds (min/max) ───
struct Thresh {
  float tMin, tMax, hMin, hMax;
};
Thresh thOut = {0, 40, 0, 100}, thIn1 = {0, 40, 0, 100},
       thIn2 = {0, 40, 0, 100};

// ─── Settings ───
String phones[3];
bool vSensEn[3] = {true, true, true};
FanMode fanMode = FM_AUTO;
float fanDelay = 5.0;
int weatherGate = 800;

// ─── Sensor Data ───
float tI1 = NAN, hI1 = NAN, tI2 = NAN, hI2 = NAN, tO = NAN, hO = NAN;
float wTemp = NAN, wHum = NAN;
int wCode = 0;
String wDesc = "Loading...";
float volts[3] = {0, 0, 0};

// ─── State ───
Page curPage = PG_MAIN;
bool fanOn = false, pwrOn = true, modemOK = false;
bool fanWaiting = false;
unsigned long fanWaitT = 0;
String kpBuf = "", editKey = "";
bool kbCaps = false;         // QWERTY keyboard caps state
Page kbReturnPage = PG_SET1; // page to return to after keyboard
unsigned long lastSens = 0, lastWeath = 0, lastMqtt = 0, lastModem = 0;
bool pwrOut = false, smsInitSent = false, alarmAck = false;
unsigned long pwrOutT = 0, pwrRestT = 0, lastRem = 0;

// ─── Forward Declarations ───
void drawMain();
void updateMain();
void drawSet1();
void drawSet2();
void updateSetData();
void drawKeypad();
void updateKpBuf();
void drawWifi();
void drawQwerty();
void updateQwertyBuf();
void reconnectWifi();
bool atCmd(const char *c, const char *e, unsigned long t);
bool sendSMS(const char *n, const char *m);
void bulkSMS(String m);
void smsLogic();
void modemCheck();
void mqttCb(char *t, byte *p, unsigned int l);
void mqttPub();
void mqttConn();

// Helper: draw a settings row
void settingsRow(int row, const char *label, String val, bool edit = true) {
  int y = 48 + row * 30;
  tft.fillRect(10, y, 460, 25, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(label, 10, y + 2, 2);
  tft.setTextColor(C_DATA, C_BG);
  tft.drawString(val, 220, y + 2, 2);
  if (edit) {
    tft.fillRoundRect(410, y, 60, 25, 5, C_BTN);
    tft.setTextColor(C_TEXT);
    tft.drawCentreString("Edit", 440, y + 5, 2);
  }
}

// ═══════════════════════════════════════════════════════════════
// LOAD / SAVE SETTINGS
// ═══════════════════════════════════════════════════════════════
void loadSettings() {
  prefs.begin("cfg", false);
  wifiSSID = prefs.getString("ssid", "dk");
  wifiPASS = prefs.getString("wpass", "dakshan11");
  phones[0] = prefs.getString("ph1", "+491704732362");
  phones[1] = prefs.getString("ph2", "+4915771913737");
  phones[2] = prefs.getString("ph3", "+491725668993");
  thOut.tMin = prefs.getFloat("oTn", 0);
  thOut.tMax = prefs.getFloat("oTx", 40);
  thOut.hMin = prefs.getFloat("oHn", 0);
  thOut.hMax = prefs.getFloat("oHx", 100);
  thIn1.tMin = prefs.getFloat("1Tn", 0);
  thIn1.tMax = prefs.getFloat("1Tx", 40);
  thIn1.hMin = prefs.getFloat("1Hn", 0);
  thIn1.hMax = prefs.getFloat("1Hx", 100);
  thIn2.tMin = prefs.getFloat("2Tn", 0);
  thIn2.tMax = prefs.getFloat("2Tx", 40);
  thIn2.hMin = prefs.getFloat("2Hn", 0);
  thIn2.hMax = prefs.getFloat("2Hx", 100);
  vSensEn[0] = prefs.getBool("v1", true);
  vSensEn[1] = prefs.getBool("v2", true);
  vSensEn[2] = prefs.getBool("v3", true);
  fanMode = (FanMode)prefs.getInt("fm", FM_AUTO);
  fanDelay = prefs.getFloat("fd", 5.0);
  weatherGate = prefs.getInt("wg", 800);
  prefs.end();
}

void saveSettings() {
  prefs.begin("cfg", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("wpass", wifiPASS);
  prefs.putString("ph1", phones[0]);
  prefs.putString("ph2", phones[1]);
  prefs.putString("ph3", phones[2]);
  prefs.putFloat("oTn", thOut.tMin);
  prefs.putFloat("oTx", thOut.tMax);
  prefs.putFloat("oHn", thOut.hMin);
  prefs.putFloat("oHx", thOut.hMax);
  prefs.putFloat("1Tn", thIn1.tMin);
  prefs.putFloat("1Tx", thIn1.tMax);
  prefs.putFloat("1Hn", thIn1.hMin);
  prefs.putFloat("1Hx", thIn1.hMax);
  prefs.putFloat("2Tn", thIn2.tMin);
  prefs.putFloat("2Tx", thIn2.tMax);
  prefs.putFloat("2Hn", thIn2.hMin);
  prefs.putFloat("2Hx", thIn2.hMax);
  prefs.putBool("v1", vSensEn[0]);
  prefs.putBool("v2", vSensEn[1]);
  prefs.putBool("v3", vSensEn[2]);
  prefs.putInt("fm", fanMode);
  prefs.putFloat("fd", fanDelay);
  prefs.putInt("wg", weatherGate);
  prefs.end();
}

// ═══════════════════════════════════════════════════════════════
// SENSOR READING
// ═══════════════════════════════════════════════════════════════
void readDHT() {
  tI1 = dhtIn1.readTemperature();
  hI1 = dhtIn1.readHumidity();
  tI2 = dhtIn2.readTemperature();
  hI2 = dhtIn2.readHumidity();
  tO = dhtOut.readTemperature();
  hO = dhtOut.readHumidity();
}

void readVoltage() {
  float cal = 190.0;
  int pins[] = {VSENS1, VSENS2, VSENS3};
  for (int i = 0; i < 3; i++) {
    if (!vSensEn[i]) {
      volts[i] = 0;
      continue;
    }
    int mn = 4095, mx = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 100) {
      int v = analogRead(pins[i]);
      if (v < mn)
        mn = v;
      if (v > mx)
        mx = v;
    }
    volts[i] = (mx - mn > 150) ? (mx - mn) * (3.3f / 4095.0f) * cal : 0;
  }
}

void getWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    wDesc = "WiFi Off";
    return;
  }
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city +
               "," + country + "&units=metric&APPID=" + apiKey;
  http.begin(url);
  int rc = http.GET();
  if (rc > 0) {
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, http.getString())) {
      wTemp = doc["main"]["temp"];
      wHum = doc["main"]["humidity"];
      wCode = doc["weather"][0]["id"];
      wDesc = doc["weather"][0]["description"].as<String>();
      if (wDesc.length() > 0)
        wDesc.setCharAt(0, toupper(wDesc.charAt(0)));
    } else
      wDesc = "JSON Err";
  } else
    wDesc = "HTTP Err";
  http.end();
}

// ═══════════════════════════════════════════════════════════════
// MODEM / SMS
// ═══════════════════════════════════════════════════════════════
bool atCmd(const char *command, const char *expect, unsigned long timeout) {
  while (Serial1.available())
    Serial1.read();
  Serial.printf(">>> %s\n", command);
  Serial1.println(command);
  unsigned long t0 = millis();
  String resp = "";
  while (millis() - t0 < timeout) {
    if (Serial1.available()) {
      resp += (char)Serial1.read();
      if (resp.indexOf(expect) != -1)
        return true;
    }
  }
  Serial.printf("<<< Timeout: %s\n", resp.c_str());
  return false;
}

bool sendSMS(const char *number, const char *message) {
  if (!atCmd("AT+CMGF=1", "OK", 2000))
    return false;
  String cmd = "AT+CMGS=\"";
  cmd += number;
  cmd += "\"";
  if (!atCmd(cmd.c_str(), ">", 5000))
    return false;
  Serial1.print(message);
  delay(100);
  Serial1.write(26);
  unsigned long t0 = millis();
  String resp = "";
  while (millis() - t0 < 20000) {
    if (Serial1.available()) {
      resp += (char)Serial1.read();
      if (resp.indexOf("OK") != -1 || resp.indexOf("+CMGS:") != -1)
        return true;
      if (resp.indexOf("ERROR") != -1)
        return false;
    }
  }
  return false;
}

void bulkSMS(String msg) {
  if (!modemOK)
    return;
  for (int i = 0; i < 3; i++) {
    if (phones[i].length() > 5) {
      sendSMS(phones[i].c_str(), msg.c_str());
      delay(1000);
    }
  }
}

void modemCheck() {
  if (millis() - lastModem < MODEM_INT)
    return;
  lastModem = millis();
  bool was = modemOK;
  modemOK = atCmd("AT", "OK", 2000);
  if (was != modemOK && curPage == PG_MAIN)
    updateMain();
}

// ═══════════════════════════════════════════════════════════════
// FAN & POWER LOGIC
// ═══════════════════════════════════════════════════════════════
bool allConditionsMet() {
  if (isnan(tO) || isnan(hO) || isnan(tI1) || isnan(hI1) || isnan(tI2) ||
      isnan(hI2))
    return false;
  if (tO < thOut.tMin || tO > thOut.tMax || hO < thOut.hMin || hO > thOut.hMax)
    return false;
  if (tI1 < thIn1.tMin || tI1 > thIn1.tMax || hI1 < thIn1.hMin ||
      hI1 > thIn1.hMax)
    return false;
  if (tI2 < thIn2.tMin || tI2 > thIn2.tMax || hI2 < thIn2.hMin ||
      hI2 > thIn2.hMax)
    return false;
  if (weatherGate > 0 && wCode > 0 && wCode < weatherGate)
    return false;
  return true;
}

void fanAndPower() {
  if (fanMode == FM_AUTO) {
    if (allConditionsMet()) {
      if (!fanWaiting) {
        fanWaiting = true;
        fanWaitT = millis();
      }
      if (millis() - fanWaitT >= (unsigned long)(fanDelay * 1000))
        fanOn = true;
    } else {
      fanOn = false;
      fanWaiting = false;
    }
  } else if (fanMode == FM_ON) {
    fanOn = true;
  } else {
    fanOn = false;
    fanWaiting = false;
  }
  digitalWrite(RELAY_PIN, fanOn ? LOW : HIGH);
  Serial.printf("[RELAY] fanMode=%d fanOn=%d pin=%d GPIO=%d\n", fanMode, fanOn,
                RELAY_PIN, digitalRead(RELAY_PIN));
  pwrOn = (volts[0] > 100 || volts[1] > 100 || volts[2] > 100);
}

// ═══════════════════════════════════════════════════════════════
// SMS ALERT LOGIC
// ═══════════════════════════════════════════════════════════════
void smsLogic() {
  static bool lastPwr = true;
  if (pwrOn != lastPwr) {
    if (!pwrOn) {
      pwrOut = true;
      pwrOutT = millis();
      smsInitSent = false;
      alarmAck = false;
    } else {
      pwrRestT = millis();
    }
    lastPwr = pwrOn;
    if (curPage == PG_MAIN) {
      drawMain();
      updateMain();
    }
  }
  if (!pwrOut)
    return;
  if (!pwrOn) {
    if (!smsInitSent && millis() - pwrOutT > SMS_INIT) {
      bulkSMS("Attention: Power outage for 3 minutes!");
      smsInitSent = true;
      lastRem = millis();
    }
    if (smsInitSent && !alarmAck && millis() - lastRem > SMS_REM) {
      bulkSMS("Attention, power outage still ongoing!");
      lastRem = millis();
    }
  } else {
    if (millis() - pwrRestT > SMS_REST) {
      bulkSMS("All clear, power restored!");
      pwrOut = false;
      if (curPage == PG_MAIN) {
        drawMain();
        updateMain();
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// MQTT (GLOBAL WEB ACCESS)
// ═══════════════════════════════════════════════════════════════
void mqttCb(char *topic, byte *payload, unsigned int length) {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, payload, length))
    return;
  String cmd = doc["cmd"] | "";
  if (cmd == "fan_mode") {
    String v = doc["val"] | "";
    if (v == "AUTO")
      fanMode = FM_AUTO;
    else if (v == "ON")
      fanMode = FM_ON;
    else if (v == "OFF")
      fanMode = FM_OFF;
    saveSettings();
  } else if (cmd == "set") {
    String k = doc["key"] | "";
    float v = doc["val"] | 0;
    if (k == "oTn")
      thOut.tMin = v;
    else if (k == "oTx")
      thOut.tMax = v;
    else if (k == "oHn")
      thOut.hMin = v;
    else if (k == "oHx")
      thOut.hMax = v;
    else if (k == "1Tn")
      thIn1.tMin = v;
    else if (k == "1Tx")
      thIn1.tMax = v;
    else if (k == "1Hn")
      thIn1.hMin = v;
    else if (k == "1Hx")
      thIn1.hMax = v;
    else if (k == "2Tn")
      thIn2.tMin = v;
    else if (k == "2Tx")
      thIn2.tMax = v;
    else if (k == "2Hn")
      thIn2.hMin = v;
    else if (k == "2Hx")
      thIn2.hMax = v;
    else if (k == "fd")
      fanDelay = v;
    else if (k == "wg")
      weatherGate = (int)v;
    saveSettings();
  } else if (cmd == "ack_alarm") {
    alarmAck = true;
  } else if (cmd == "set_phone") {
    int idx = doc["idx"] | -1;
    String v = doc["val"] | "";
    if (idx >= 0 && idx < 3 && v.length() > 3) {
      phones[idx] = v;
      saveSettings();
    }
  }
  if (curPage == PG_MAIN)
    updateMain();
}

void mqttPub() {
  if (millis() - lastMqtt < MQTT_INT)
    return;
  lastMqtt = millis();
  if (!mqtt.connected())
    return;
  DynamicJsonDocument doc(1536);
  doc["outdoor"]["t"] = tO;
  doc["outdoor"]["h"] = hO;
  doc["indoor1"]["t"] = tI1;
  doc["indoor1"]["h"] = hI1;
  doc["indoor2"]["t"] = tI2;
  doc["indoor2"]["h"] = hI2;
  doc["weather"]["t"] = wTemp;
  doc["weather"]["h"] = wHum;
  doc["weather"]["code"] = wCode;
  doc["weather"]["desc"] = wDesc;
  doc["voltage"]["l1"] = volts[0];
  doc["voltage"]["l2"] = volts[1];
  doc["voltage"]["l3"] = volts[2];
  String fm = (fanMode == FM_ON) ? "ON" : (fanMode == FM_OFF) ? "OFF" : "AUTO";
  doc["fan"]["mode"] = fm;
  doc["fan"]["on"] = fanOn;
  doc["fan"]["waiting"] = fanWaiting;
  doc["power"]["on"] = pwrOn;
  doc["power"]["outage"] = pwrOut;
  doc["modem"] = modemOK;
  doc["thresh"]["out"]["tMin"] = thOut.tMin;
  doc["thresh"]["out"]["tMax"] = thOut.tMax;
  doc["thresh"]["out"]["hMin"] = thOut.hMin;
  doc["thresh"]["out"]["hMax"] = thOut.hMax;
  doc["thresh"]["in1"]["tMin"] = thIn1.tMin;
  doc["thresh"]["in1"]["tMax"] = thIn1.tMax;
  doc["thresh"]["in1"]["hMin"] = thIn1.hMin;
  doc["thresh"]["in1"]["hMax"] = thIn1.hMax;
  doc["thresh"]["in2"]["tMin"] = thIn2.tMin;
  doc["thresh"]["in2"]["tMax"] = thIn2.tMax;
  doc["thresh"]["in2"]["hMin"] = thIn2.hMin;
  doc["thresh"]["in2"]["hMax"] = thIn2.hMax;
  doc["fanDelay"] = fanDelay;
  doc["weatherGate"] = weatherGate;
  JsonArray ph = doc.createNestedArray("phones");
  ph.add(phones[0]);
  ph.add(phones[1]);
  ph.add(phones[2]);
  String json;
  serializeJson(doc, json);
  mqtt.publish(mqttDataTopic.c_str(), json.c_str());
}

void mqttConn() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED)
    return;
  Serial.println("MQTT connecting...");
  if (mqtt.connect(mqttId.c_str())) {
    mqtt.subscribe(mqttCmdTopic.c_str());
    Serial.println("MQTT connected! Topic: " + mqttDataTopic);
  }
}

// ═══════════════════════════════════════════════════════════════
// UI — MAIN SCREEN
// ═══════════════════════════════════════════════════════════════
void drawMain() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 480, 40, C_TITLE);
  tft.setTextColor(C_TEXT, C_TITLE);
  tft.drawString(" Ventilation & Power Monitor", 10, 8, 4);

  if (pwrOut && !alarmAck) {
    tft.fillRoundRect(350, 5, 125, 30, 8, C_ACK);
    tft.setTextColor(TFT_BLACK, C_ACK);
    tft.drawCentreString("Acknowledge", 412, 13, 2);
  }

  for (int i = 0; i < 6; i++)
    tft.drawRoundRect(5, 45 + i * 38, 470, 35, 5, C_PANEL);

  // 3 bottom buttons: Fan Mode | WiFi | Settings
  tft.fillRoundRect(5, 280, 150, 35, 8, C_BTN);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Fan Mode", 80, 290, 2);
  tft.fillRoundRect(165, 280, 150, 35, 8,
                    WiFi.status() == WL_CONNECTED ? C_OK : C_DEL);
  tft.drawCentreString("WiFi", 240, 290, 2);
  tft.fillRoundRect(325, 280, 150, 35, 8, C_BTN);
  tft.drawCentreString("Settings", 400, 290, 2);
}

void updateMain() {
  char b[128];
  tft.setTextFont(2);

  // Row 0: Outdoor
  int y = 50;
  tft.fillRect(100, y, 370, 22, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Outdoor:", 10, y);
  if (!isnan(tO))
    sprintf(b, "%.1fC | %.0f%%RH", tO, hO);
  else
    strcpy(b, "No Data");
  tft.setTextColor(C_DATA, C_BG);
  tft.drawString(b, 100, y);

  // Row 1: Indoor 1
  y = 88;
  tft.fillRect(100, y, 370, 22, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Indoor 1:", 10, y);
  if (!isnan(tI1))
    sprintf(b, "%.1fC | %.0f%%RH", tI1, hI1);
  else
    strcpy(b, "No Data");
  tft.setTextColor(C_DATA, C_BG);
  tft.drawString(b, 100, y);

  // Row 2: Indoor 2
  y = 126;
  tft.fillRect(100, y, 370, 22, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Indoor 2:", 10, y);
  if (!isnan(tI2))
    sprintf(b, "%.1fC | %.0f%%RH", tI2, hI2);
  else
    strcpy(b, "No Data");
  tft.setTextColor(C_DATA, C_BG);
  tft.drawString(b, 100, y);

  // Row 3: Weather
  y = 164;
  tft.fillRect(100, y, 370, 22, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Weather:", 10, y);
  if (!isnan(wTemp))
    sprintf(b, "%.1fC %.0f%% %s [%d]", wTemp, wHum, wDesc.c_str(), wCode);
  else
    sprintf(b, "%s", wDesc.c_str());
  tft.setTextColor(C_DATA, C_BG);
  tft.drawString(b, 100, y);

  // Row 4: Voltage
  y = 202;
  tft.fillRect(100, y, 370, 22, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Voltage:", 10, y);
  String vs = "";
  if (vSensEn[0])
    vs += "L1:" + String(volts[0], 0) + "V ";
  if (vSensEn[1])
    vs += "L2:" + String(volts[1], 0) + "V ";
  if (vSensEn[2])
    vs += "L3:" + String(volts[2], 0) + "V";
  tft.setTextColor(C_DATA, C_BG);
  tft.drawString(vs, 100, y);

  // Row 5: System
  y = 240;
  tft.fillRect(100, y, 370, 22, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("System:", 10, y);
  String fm = (fanMode == FM_ON) ? "ON" : (fanMode == FM_OFF) ? "OFF" : "AUTO";
  sprintf(b, "Fan:%s(%s) Pwr:%s SIM:%s MQTT:%s", fm.c_str(),
          fanOn ? "ON" : "OFF", pwrOn ? "OK" : "OUT", modemOK ? "OK" : "--",
          mqtt.connected() ? "OK" : "--");
  tft.setTextColor(pwrOn ? C_DATA : TFT_RED, C_BG);
  tft.drawString(b, 100, y);
}

// ═══════════════════════════════════════════════════════════════
// UI — SETTINGS PAGE 1 (Phones, Fan Delay, Weather, Voltage)
// ═══════════════════════════════════════════════════════════════
void drawSetHeader(const char *title) {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 480, 40, C_TITLE);
  tft.setTextColor(C_TEXT, C_TITLE);
  tft.drawCentreString(title, 240, 13, 4);
  // Save & Exit button
  tft.fillRoundRect(355, 275, 120, 40, 8, C_OK);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Save & Exit", 415, 287, 2);
}

void drawSet1() {
  drawSetHeader("Settings (1/2)");
  // Next page button
  tft.fillRoundRect(5, 275, 120, 40, 8, C_BTN);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Next >>", 65, 287, 2);
  updateSetData();
}

void drawSet2() {
  drawSetHeader("Settings (2/2)");
  // Back button
  tft.fillRoundRect(5, 275, 120, 40, 8, C_BTN);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("<< Back", 65, 287, 2);
  updateSetData();
}

void updateSetData() {
  char b[40];
  if (curPage == PG_SET1) {
    settingsRow(0, "Phone #1:", phones[0]);
    settingsRow(1, "Phone #2:", phones[1]);
    settingsRow(2, "Phone #3:", phones[2]);
    sprintf(b, "%.1f sec", fanDelay);
    settingsRow(3, "Fan ON Delay:", b);
    sprintf(b, "%d (>=%d=good)", weatherGate, weatherGate);
    settingsRow(4, "Weather Gate:", b);
    // Voltage sensor toggles (row 5)
    int y = 48 + 5 * 30;
    tft.fillRect(10, y, 460, 25, C_BG);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString("Voltage Sensors:", 10, y + 2, 2);
    for (int i = 0; i < 3; i++) {
      uint16_t c = vSensEn[i] ? C_OK : C_DEL;
      tft.fillRoundRect(250 + i * 75, y, 65, 25, 5, c);
      tft.setTextColor(C_TEXT);
      tft.drawCentreString(vSensEn[i] ? "ON" : "OFF", 282 + i * 75, y + 5, 2);
    }
  } else if (curPage == PG_SET2) {
    sprintf(b, "%.0f / %.0f", thOut.tMin, thOut.tMax);
    settingsRow(0, "OUT Temp (C):", b);
    sprintf(b, "%.0f / %.0f", thOut.hMin, thOut.hMax);
    settingsRow(1, "OUT Hum (%):", b);
    sprintf(b, "%.0f / %.0f", thIn1.tMin, thIn1.tMax);
    settingsRow(2, "IN1 Temp (C):", b);
    sprintf(b, "%.0f / %.0f", thIn1.hMin, thIn1.hMax);
    settingsRow(3, "IN1 Hum (%):", b);
    sprintf(b, "%.0f / %.0f", thIn2.tMin, thIn2.tMax);
    settingsRow(4, "IN2 Temp (C):", b);
    sprintf(b, "%.0f / %.0f", thIn2.hMin, thIn2.hMax);
    settingsRow(5, "IN2 Hum (%):", b);
  }
}

// ═══════════════════════════════════════════════════════════════
// UI — KEYPAD
// ═══════════════════════════════════════════════════════════════
void drawKeypad() {
  tft.fillScreen(C_BG);
  tft.fillRoundRect(10, 10, 460, 60, 8, TFT_BLACK);
  tft.drawRoundRect(10, 10, 460, 60, 8, C_ACC);
  tft.setTextColor(C_TEXT, TFT_BLACK);
  tft.drawString("Enter value (min/max use /):", 20, 18, 2);

  // Context-aware special key: + for phones, / for thresholds
  bool isPhone = (editKey == "ph1" || editKey == "ph2" || editKey == "ph3");
  const char *keys[12] = {"1", "2", "3", "4", "5",
                          "6", "7", "8", "9", isPhone ? "+" : "/",
                          "0", "."};
  int kw = 110, kh = 50, gap = 10;
  tft.setTextColor(C_TEXT);
  tft.setTextFont(4);
  for (int i = 0; i < 12; i++) {
    int r = i / 3, c = i % 3;
    int x = 10 + c * (kw + gap), y = 80 + r * (kh + gap);
    uint16_t clr = (i == 9 || i == 11) ? C_DEL : C_BTN;
    tft.fillRoundRect(x, y, kw, kh, 8, clr);
    tft.drawCentreString(keys[i], x + kw / 2, y + 17, 4);
  }
  int bx = 10 + 3 * (kw + gap);
  tft.fillRoundRect(bx, 80, kw, kh * 2 + gap, 8, C_DEL);
  tft.drawCentreString("<-", bx + kw / 2, 80 + kh - 8, 4);
  tft.fillRoundRect(bx, 80 + (kh + gap) * 2, kw, kh * 2 + gap, 8, C_OK);
  tft.drawCentreString("OK", bx + kw / 2, 80 + (kh + gap) * 2 + kh - 8, 4);
}

void updateKpBuf() {
  tft.setTextFont(4);
  tft.setTextColor(C_DATA, TFT_BLACK);
  tft.fillRect(20, 42, 440, 30, TFT_BLACK);
  tft.drawString(kpBuf, 25, 45);
}

// ═══════════════════════════════════════════════════════════════
// UI — WIFI SETTINGS PAGE
// ═══════════════════════════════════════════════════════════════
void drawWifi() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 480, 40, C_TITLE);
  tft.setTextColor(C_TEXT, C_TITLE);
  tft.drawCentreString("WiFi Settings", 240, 13, 4);

  // WiFi status indicator
  bool connected = (WiFi.status() == WL_CONNECTED);
  tft.fillRoundRect(10, 50, 460, 40, 8, connected ? C_OK : C_DEL);
  tft.setTextColor(C_TEXT);
  if (connected) {
    tft.drawCentreString("Connected: " + WiFi.localIP().toString(), 240, 62, 2);
  } else {
    tft.drawCentreString("Not Connected", 240, 62, 2);
  }

  // SSID row
  tft.fillRoundRect(10, 100, 460, 40, 8, C_PANEL);
  tft.setTextColor(C_TEXT, C_PANEL);
  tft.drawString(" SSID:", 20, 112, 2);
  tft.setTextColor(C_DATA, C_PANEL);
  tft.drawString(wifiSSID, 120, 112, 2);
  tft.fillRoundRect(400, 105, 60, 30, 5, C_BTN);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Edit", 430, 112, 2);

  // Password row
  tft.fillRoundRect(10, 150, 460, 40, 8, C_PANEL);
  tft.setTextColor(C_TEXT, C_PANEL);
  tft.drawString(" Pass:", 20, 162, 2);
  tft.setTextColor(C_DATA, C_PANEL);
  // Show masked password
  String masked = "";
  for (unsigned int i = 0; i < wifiPASS.length(); i++)
    masked += "*";
  tft.drawString(masked, 120, 162, 2);
  tft.fillRoundRect(400, 155, 60, 30, 5, C_BTN);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Edit", 430, 162, 2);

  // Connect button
  tft.fillRoundRect(10, 210, 220, 50, 8, C_OK);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Connect", 120, 228, 2);

  // Back button
  tft.fillRoundRect(250, 210, 220, 50, 8, C_DEL);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Back", 360, 228, 2);

  // MQTT status
  tft.fillRoundRect(10, 270, 460, 40, 8, C_PANEL);
  tft.setTextColor(C_TEXT, C_PANEL);
  String mqttStr = "MQTT: ";
  mqttStr += mqtt.connected() ? "Connected" : "Disconnected";
  mqttStr += "  |  ID: " + mqttId;
  tft.drawCentreString(mqttStr, 240, 282, 2);
}

// ═══════════════════════════════════════════════════════════════
// UI — FULL QWERTY KEYBOARD (for WiFi SSID/Password)
// ═══════════════════════════════════════════════════════════════
void drawQwerty() {
  tft.fillScreen(TFT_BLACK);
  // Input field at top
  tft.fillRoundRect(5, 2, 470, 36, 6, 0x18E3);  // dark grey bg
  tft.drawRoundRect(5, 2, 470, 36, 6, C_ACC);
  tft.setTextColor(C_ACC, 0x18E3);
  String label = (editKey == "ssid") ? "SSID:" : "PASS:";
  tft.drawString(label, 12, 10, 2);
  updateQwertyBuf();

  // Key dimensions
  int kw = 45, kh = 42, gap = 2;

  // Row 0: numbers 1-0 (10 keys)
  int sy = 42;
  const char *row0 = "1234567890";
  for (int i = 0; i < 10; i++) {
    int x = 5 + i * (kw + gap);
    tft.fillRoundRect(x, sy, kw, kh, 5, 0x3186);  // medium grey
    tft.setTextColor(C_TEXT);
    char ch[2] = {row0[i], 0};
    tft.drawCentreString(ch, x + kw / 2, sy + 12, 4);
  }

  // Row 1: QWERTYUIOP (10 keys)
  sy = 42 + (kh + gap);
  const char *row1 = kbCaps ? "QWERTYUIOP" : "qwertyuiop";
  for (int i = 0; i < 10; i++) {
    int x = 5 + i * (kw + gap);
    tft.fillRoundRect(x, sy, kw, kh, 5, C_BTN);
    tft.setTextColor(C_TEXT);
    char ch[2] = {row1[i], 0};
    tft.drawCentreString(ch, x + kw / 2, sy + 12, 4);
  }

  // Row 2: ASDFGHJKL (9 keys, centered)
  sy = 42 + (kh + gap) * 2;
  const char *row2 = kbCaps ? "ASDFGHJKL" : "asdfghjkl";
  int xoff = 28;
  for (int i = 0; i < 9; i++) {
    int x = xoff + i * (kw + gap);
    tft.fillRoundRect(x, sy, kw, kh, 5, C_BTN);
    tft.setTextColor(C_TEXT);
    char ch[2] = {row2[i], 0};
    tft.drawCentreString(ch, x + kw / 2, sy + 12, 4);
  }

  // Row 3: CAP + ZXCVBNM + DEL
  sy = 42 + (kh + gap) * 3;
  const char *row3 = kbCaps ? "ZXCVBNM" : "zxcvbnm";
  // CAP button (wider)
  tft.fillRoundRect(5, sy, 55, kh, 5, kbCaps ? C_ACC : 0x4A49);
  tft.setTextColor(kbCaps ? TFT_BLACK : C_TEXT);
  tft.drawCentreString("CAP", 32, sy + 14, 2);
  // 7 letter keys
  for (int i = 0; i < 7; i++) {
    int x = 64 + i * (kw + gap);
    tft.fillRoundRect(x, sy, kw, kh, 5, C_BTN);
    tft.setTextColor(C_TEXT);
    char ch[2] = {row3[i], 0};
    tft.drawCentreString(ch, x + kw / 2, sy + 12, 4);
  }
  // Backspace button (wider)
  int delX = 64 + 7 * (kw + gap);
  tft.fillRoundRect(delX, sy, 475 - delX, kh, 5, C_DEL);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("DEL", delX + (475 - delX) / 2, sy + 14, 2);

  // Row 4: special + SPACE + OK
  sy = 42 + (kh + gap) * 4;
  // . key
  tft.fillRoundRect(5, sy, kw, kh, 5, 0x3186);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString(".", 5 + kw / 2, sy + 12, 4);
  // @ key
  tft.fillRoundRect(5 + kw + gap, sy, kw, kh, 5, 0x3186);
  tft.drawCentreString("@", 5 + kw + gap + kw / 2, sy + 12, 4);
  // - key
  tft.fillRoundRect(5 + (kw + gap) * 2, sy, kw, kh, 5, 0x3186);
  tft.drawCentreString("-", 5 + (kw + gap) * 2 + kw / 2, sy + 12, 4);
  // SPACE bar (wide)
  int spX = 5 + (kw + gap) * 3;
  int spW = 190;
  tft.fillRoundRect(spX, sy, spW, kh, 5, 0x2945);
  tft.drawCentreString("SPACE", spX + spW / 2, sy + 14, 2);
  // OK button (wide, green)
  int okX = spX + spW + gap;
  tft.fillRoundRect(okX, sy, 475 - okX, kh, 5, C_OK);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("OK", okX + (475 - okX) / 2, sy + 12, 4);
}

void updateQwertyBuf() {
  tft.setTextColor(C_DATA, 0x18E3);
  tft.fillRect(60, 8, 410, 24, 0x18E3);
  // Show text, mask password
  if (editKey == "wpass") {
    String masked = "";
    for (unsigned int i = 0; i < kpBuf.length(); i++)
      masked += "*";
    tft.drawString(masked + "_", 65, 10, 4);
  } else {
    tft.drawString(kpBuf + "_", 65, 10, 4);
  }
}

// ═══════════════════════════════════════════════════════════════
// WIFI RECONNECT (auto-reload MQTT + display)
// ═══════════════════════════════════════════════════════════════
void reconnectWifi() {
  Serial.println("Reconnecting WiFi to: " + wifiSSID);
  // Show connecting status
  if (curPage == PG_WIFI) {
    tft.fillRoundRect(10, 50, 460, 40, 8, C_ACK);
    tft.setTextColor(TFT_BLACK, C_ACK);
    tft.drawCentreString("Connecting...", 240, 62, 2);
  }
  // Disconnect existing
  mqtt.disconnect();
  WiFi.disconnect(true);
  delay(500);
  // Connect with new credentials
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  int att = 0;
  while (WiFi.status() != WL_CONNECTED && att < 20) {
    delay(500);
    att++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
    // Re-establish MQTT
    mqtt.setClient(wifiCli);
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCb);
    mqtt.setBufferSize(1024);
    mqttConn();
  } else {
    Serial.println("WiFi connection failed!");
    wDesc = "WiFi Off";
  }
  // Refresh current page
  if (curPage == PG_WIFI)
    drawWifi();
  else if (curPage == PG_MAIN) {
    drawMain();
    updateMain();
  }
}

// ═══════════════════════════════════════════════════════════════
// TOUCH HANDLER
// ═══════════════════════════════════════════════════════════════
void handleTouch() {
  uint16_t x, y;
  if (!tft.getTouch(&x, &y))
    return;
  static uint32_t lastT = 0;
  if (millis() - lastT < 300)
    return;
  lastT = millis();
  if (tft.getRotation() == 1)
    y = tft.height() - y;

  Serial.printf("Touch: x=%d y=%d page=%d\n", x, y, curPage);

  if (curPage == PG_MAIN) {
    // Bottom buttons: Fan Mode (5-155) | WiFi (165-315) | Settings (325-475)
    if (y > 280 && y < 315) {
      if (x > 5 && x < 155) {
        fanMode = (FanMode)((fanMode + 1) % 3);
        saveSettings();
        fanAndPower();
        updateMain();
      } else if (x > 165 && x < 315) {
        curPage = PG_WIFI;
        drawWifi();
      } else if (x > 325 && x < 475) {
        curPage = PG_SET1;
        drawSet1();
      }
    }
    // Acknowledge power outage
    if (pwrOut && !alarmAck && y < 40 && x > 350) {
      alarmAck = true;
      drawMain();
      updateMain();
    }
  } else if (curPage == PG_WIFI) {
    // Edit SSID
    if (x > 400 && x < 460 && y > 100 && y < 140) {
      editKey = "ssid";
      kpBuf = wifiSSID;
      kbCaps = false;
      kbReturnPage = PG_WIFI;
      curPage = PG_QWERTY;
      drawQwerty();
      return;
    }
    // Edit Password
    if (x > 400 && x < 460 && y > 150 && y < 190) {
      editKey = "wpass";
      kpBuf = wifiPASS;
      kbCaps = false;
      kbReturnPage = PG_WIFI;
      curPage = PG_QWERTY;
      drawQwerty();
      return;
    }
    // Connect button
    if (x > 10 && x < 230 && y > 210 && y < 260) {
      saveSettings();
      reconnectWifi();
      return;
    }
    // Back button
    if (x > 250 && x < 470 && y > 210 && y < 260) {
      curPage = PG_MAIN;
      drawMain();
      updateMain();
      return;
    }
  } else if (curPage == PG_SET1) {
    // Save & Exit
    if (y > 275 && y < 315 && x > 355) {
      saveSettings();
      curPage = PG_MAIN;
      drawMain();
      updateMain();
      return;
    }
    // Next page
    if (y > 275 && y < 315 && x < 125) {
      curPage = PG_SET2;
      drawSet2();
      return;
    }
    // Voltage toggles (row 5)
    int vy = 48 + 5 * 30;
    if (y > vy && y < vy + 25) {
      if (x > 250 && x < 315)
        vSensEn[0] = !vSensEn[0];
      else if (x > 325 && x < 390)
        vSensEn[1] = !vSensEn[1];
      else if (x > 400 && x < 465)
        vSensEn[2] = !vSensEn[2];
      updateSetData();
      return;
    }
    // Edit buttons (rows 0-4)
    if (x > 410 && x < 470) {
      for (int i = 0; i < 5; i++) {
        int ry = 48 + i * 30;
        if (y > ry && y < ry + 25) {
          if (i == 0) {
            editKey = "ph1";
            kpBuf = phones[0];
          } else if (i == 1) {
            editKey = "ph2";
            kpBuf = phones[1];
          } else if (i == 2) {
            editKey = "ph3";
            kpBuf = phones[2];
          } else if (i == 3) {
            editKey = "fd";
            kpBuf = "";
          } else if (i == 4) {
            editKey = "wg";
            kpBuf = "";
          }
          curPage = PG_KEYPAD;
          drawKeypad();
          updateKpBuf();
          return;
        }
      }
    }
  } else if (curPage == PG_SET2) {
    // Save & Exit
    if (y > 275 && y < 315 && x > 355) {
      saveSettings();
      curPage = PG_MAIN;
      drawMain();
      updateMain();
      return;
    }
    // Back
    if (y > 275 && y < 315 && x < 125) {
      curPage = PG_SET1;
      drawSet1();
      return;
    }
    // Edit buttons (rows 0-5: thresholds)
    if (x > 410 && x < 470) {
      const char *keys[] = {"oT", "oH", "1T", "1H", "2T", "2H"};
      for (int i = 0; i < 6; i++) {
        int ry = 48 + i * 30;
        if (y > ry && y < ry + 25) {
          editKey = keys[i];
          kpBuf = "";
          curPage = PG_KEYPAD;
          drawKeypad();
          updateKpBuf();
          return;
        }
      }
    }
  } else if (curPage == PG_KEYPAD) {
    int kw = 110, kh = 50, gap = 10;
    bool isPhone = (editKey == "ph1" || editKey == "ph2" || editKey == "ph3");
    const char *keys[12] = {"1", "2", "3", "4", "5",
                            "6", "7", "8", "9", isPhone ? "+" : "/",
                            "0", "."};

    for (int i = 0; i < 12; i++) {
      int r = i / 3, c = i % 3;
      int kx = 10 + c * (kw + gap), ky = 80 + r * (kh + gap);
      if (x > kx && x < kx + kw && y > ky && y < ky + kh) {
        if (kpBuf.length() < 25)
          kpBuf += keys[i];
        updateKpBuf();
        return;
      }
    }
    int sx = 10 + 3 * (kw + gap);
    if (x > sx && x < sx + kw) {
      // Backspace
      if (y > 80 && y < 80 + kh * 2 + gap) {
        if (kpBuf.length() > 0)
          kpBuf.remove(kpBuf.length() - 1);
        updateKpBuf();
        return;
      }
      // OK
      if (y > 80 + (kh + gap) * 2) {
        // Remember which page to return to before clearing editKey
        bool isThreshold =
            (editKey == "oT" || editKey == "oH" || editKey == "1T" ||
             editKey == "1H" || editKey == "2T" || editKey == "2H");
        // Apply value
        if (editKey == "ph1")
          phones[0] = kpBuf;
        else if (editKey == "ph2")
          phones[1] = kpBuf;
        else if (editKey == "ph3")
          phones[2] = kpBuf;
        else if (editKey == "fd") {
          fanDelay = kpBuf.toFloat();
          if (fanDelay < 0)
            fanDelay = 0;
        } else if (editKey == "wg") {
          weatherGate = kpBuf.toInt();
        } else {
          int sl = kpBuf.indexOf('/');
          if (sl > 0) {
            float v1 = kpBuf.substring(0, sl).toFloat();
            float v2 = kpBuf.substring(sl + 1).toFloat();
            if (editKey == "oT") {
              thOut.tMin = v1;
              thOut.tMax = v2;
            } else if (editKey == "oH") {
              thOut.hMin = v1;
              thOut.hMax = v2;
            } else if (editKey == "1T") {
              thIn1.tMin = v1;
              thIn1.tMax = v2;
            } else if (editKey == "1H") {
              thIn1.hMin = v1;
              thIn1.hMax = v2;
            } else if (editKey == "2T") {
              thIn2.tMin = v1;
              thIn2.tMax = v2;
            } else if (editKey == "2H") {
              thIn2.hMin = v1;
              thIn2.hMax = v2;
            }
          }
        }
        editKey = "";
        if (isThreshold) {
          curPage = PG_SET2;
          drawSet2();
        } else {
          curPage = PG_SET1;
          drawSet1();
        }
      }
    }
  } else if (curPage == PG_QWERTY) {
    int kw = 45, kh = 42, gap = 2;
    const char *row0 = "1234567890";
    const char *row1 = kbCaps ? "QWERTYUIOP" : "qwertyuiop";
    const char *row2 = kbCaps ? "ASDFGHJKL" : "asdfghjkl";
    const char *row3 = kbCaps ? "ZXCVBNM" : "zxcvbnm";

    // Row 0: numbers
    int sy = 42;
    for (int i = 0; i < 10; i++) {
      int kx = 5 + i * (kw + gap);
      if (x > kx && x < kx + kw && y > sy && y < sy + kh) {
        if (kpBuf.length() < 32) kpBuf += row0[i];
        updateQwertyBuf(); return;
      }
    }
    // Row 1: QWERTY
    sy = 42 + (kh + gap);
    for (int i = 0; i < 10; i++) {
      int kx = 5 + i * (kw + gap);
      if (x > kx && x < kx + kw && y > sy && y < sy + kh) {
        if (kpBuf.length() < 32) kpBuf += row1[i];
        updateQwertyBuf(); return;
      }
    }
    // Row 2: ASDF
    sy = 42 + (kh + gap) * 2;
    for (int i = 0; i < 9; i++) {
      int kx = 28 + i * (kw + gap);
      if (x > kx && x < kx + kw && y > sy && y < sy + kh) {
        if (kpBuf.length() < 32) kpBuf += row2[i];
        updateQwertyBuf(); return;
      }
    }
    // Row 3: CAP + ZXCVBNM + DEL
    sy = 42 + (kh + gap) * 3;
    if (x > 5 && x < 60 && y > sy && y < sy + kh) {
      kbCaps = !kbCaps; drawQwerty(); return;
    }
    for (int i = 0; i < 7; i++) {
      int kx = 64 + i * (kw + gap);
      if (x > kx && x < kx + kw && y > sy && y < sy + kh) {
        if (kpBuf.length() < 32) kpBuf += row3[i];
        updateQwertyBuf(); return;
      }
    }
    // DEL
    int delX = 64 + 7 * (kw + gap);
    if (x > delX && x < 475 && y > sy && y < sy + kh) {
      if (kpBuf.length() > 0) kpBuf.remove(kpBuf.length() - 1);
      updateQwertyBuf(); return;
    }
    // Row 4: . @ - SPACE OK
    sy = 42 + (kh + gap) * 4;
    // .
    if (x > 5 && x < 5 + kw && y > sy && y < sy + kh) {
      if (kpBuf.length() < 32) kpBuf += ".";
      updateQwertyBuf(); return;
    }
    // @
    if (x > 5 + kw + gap && x < 5 + (kw + gap) + kw && y > sy && y < sy + kh) {
      if (kpBuf.length() < 32) kpBuf += "@";
      updateQwertyBuf(); return;
    }
    // -
    if (x > 5 + (kw + gap) * 2 && x < 5 + (kw + gap) * 2 + kw && y > sy && y < sy + kh) {
      if (kpBuf.length() < 32) kpBuf += "-";
      updateQwertyBuf(); return;
    }
    // Space
    int spX = 5 + (kw + gap) * 3;
    if (x > spX && x < spX + 190 && y > sy && y < sy + kh) {
      if (kpBuf.length() < 32) kpBuf += " ";
      updateQwertyBuf(); return;
    }
    // OK
    int okX = spX + 190 + gap;
    if (x > okX && x < 475 && y > sy && y < sy + kh) {
      if (editKey == "ssid") wifiSSID = kpBuf;
      else if (editKey == "wpass") wifiPASS = kpBuf;
      editKey = "";
      curPage = kbReturnPage;
      if (curPage == PG_WIFI) drawWifi();
      else { drawMain(); updateMain(); }
      return;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Ventilation & Power Monitor ===");

  // UART1 for A7670E
  Serial1.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  // Backlight
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);

  // Display
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true); // Needed for properly displaying the color array

  // --- Display splash screen for 30 seconds ---
  tft.pushImage(0, 0, SPLASH_W, SPLASH_H, splash_img);
  delay(30000);

  // Sensors
  dhtIn1.begin();
  dhtIn2.begin();
  dhtOut.begin();

  // Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  // Voltage ADC
  pinMode(VSENS1, INPUT);
  pinMode(VSENS2, INPUT);
  pinMode(VSENS3, INPUT);

  // Init screen
  tft.fillScreen(C_BG);
  tft.setTextColor(C_TEXT);
  tft.drawCentreString("Initializing...", 240, 80, 4);

  loadSettings();

  // MQTT device ID from MAC
  mqttId = "reef_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  mqttDataTopic = "reef/" + mqttId + "/data";
  mqttCmdTopic = "reef/" + mqttId + "/cmd";
  Serial.println("MQTT ID: " + mqttId);
  Serial.println("MQTT Data Topic: " + mqttDataTopic);
  Serial.println("MQTT Cmd Topic: " + mqttCmdTopic);

  // WiFi (using saved credentials)
  tft.drawCentreString("Connecting WiFi...", 240, 110, 2);
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  int att = 0;
  while (WiFi.status() != WL_CONNECTED && att < 20) {
    delay(500);
    att++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    tft.drawCentreString("WiFi OK: " + WiFi.localIP().toString(), 240, 130, 2);
  } else {
    tft.drawCentreString("WiFi Offline", 240, 130, 2);
    wDesc = "WiFi Off";
  }

  // MQTT setup (setClient here, not in global constructor!)
  mqtt.setClient(wifiCli);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCb);
  mqtt.setBufferSize(1024);
  mqttConn();

  // Modem init
  tft.drawCentreString("Initializing Modem...", 240, 150, 2);
  bool modemUp = false;
  for (int i = 0; i < 5; i++) {
    if (atCmd("AT", "OK", 2000)) {
      modemUp = true;
      break;
    }
    delay(1000);
  }
  if (!modemUp) {
    tft.drawCentreString("MODEM FAILED", 240, 170, 2);
    delay(2000);
  } else {
    atCmd("ATE0", "OK", 1000);
    bool simOk = false;
    if (atCmd("AT+CPIN?", "SIM PIN", 2000)) {
      String pc = "AT+CPIN=\"";
      pc += SIM_CODE;
      pc += "\"";
      if (atCmd(pc.c_str(), "OK", 5000)) {
        delay(3000);
        simOk = true;
      }
    } else if (atCmd("AT+CPIN?", "READY", 2000)) {
      simOk = true;
    }
    if (simOk) {
      tft.drawCentreString("Registering...", 240, 170, 2);
      for (int i = 0; i < 3; i++) {
        if (atCmd("AT+CGREG?", "+CGREG: 0,1", 5000) ||
            atCmd("AT+CGREG?", "+CGREG: 0,5", 5000)) {
          modemOK = true;
          break;
        }
        delay(2000);
      }
    }
    tft.drawCentreString(modemOK ? "Modem Ready!" : "No Network", 240, 190, 2);
    delay(1000);
  }

  // Reboot SMS
  if (modemOK)
    bulkSMS("System rebooted! Ventilation Monitor online.");

  // Show MQTT info on init screen
  tft.drawCentreString("MQTT ID: " + mqttId, 240, 210, 2);
  delay(2000);

  // Initial reads
  readDHT();
  readVoltage();
  getWeather();
  fanAndPower();
  drawMain();
  updateMain();
  Serial.println("Setup complete.");
}

// ═══════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  handleTouch();
  smsLogic();
  modemCheck();
  mqtt.loop();
  mqttConn();
  mqttPub();

  if (curPage == PG_MAIN) {
    if (millis() - lastSens >= SENS_INT) {
      lastSens = millis();
      readDHT();
      readVoltage();
      fanAndPower();
      updateMain();
    }
    if (millis() - lastWeath >= WEATH_INT) {
      lastWeath = millis();
      getWeather();
      updateMain();
    }
  }
  delay(20);
}
