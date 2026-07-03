/*
 * ===============================================================================
 * ESP32 3-Phase Smart Climate & Voltage Monitor
 * FINAL, CONSOLIDATED VERSION (with A7670E Modem Upgrade)
 *
 * This version implements all requested features in a single, error-free file.
 * Key Features:
 * - Monitors three separate AC voltage phases using three ZMPT101B sensors.
 * - Multi-page UI for on-screen configuration of thresholds and phone numbers.
 * - Controls a fan relay based on temperature and humidity.
 * - Sends SMS alerts via a A7670E module for power outages. (MODIFIED)
 * - Fetches and displays local weather from OpenWeatherMap.
 * - All settings are saved to non-volatile memory.
 * ===============================================================================
*/

// =================================================================
// 1. LIBRARIES
// =================================================================
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "DHT.h"
#include <Preferences.h>

// =================================================================
// 2. CONFIGURATION & PINS
// =================================================================
// --- WiFi & Weather ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
String openWeatherMapApiKey = "YOUR_OWM_API_KEY";
String city = "Braunschweig";
String countryCode = "DE";

// --- Hardware Pins ---
#define DHTPIN_INDOOR_1   25
#define DHTPIN_INDOOR_2   26
#define DHTPIN_OUTDOOR    14
#define DHTTYPE           DHT22
#define RELAY_PIN         27
// MODIFIED: Renamed for clarity and added SIM PIN
#define A7670E_RX_PIN     16
#define A7670E_TX_PIN     17
#define SIM_PIN           "4176" // Your SIM PIN from previous success

// --- PINS FOR 3x ZMPT101B VOLTAGE SENSORS ---
#define VOLTAGE_SENSOR_1  32  // Phase 1
#define VOLTAGE_SENSOR_2  33  // Phase 2
#define VOLTAGE_SENSOR_3  34  // Phase 3

// --- Timers & Intervals ---
#define SENSOR_READ_INTERVAL    10000 // 10 seconds
#define WEATHER_UPDATE_INTERVAL 300000 // 5 minutes
#define SMS_INITIAL_DELAY       180000 // 3 minutes
#define SMS_REMINDER_INTERVAL   900000 // 15 minutes
#define SMS_RESTORE_DELAY       180000 // 3 minutes

// =================================================================
// 3. UI DEFINITIONS
// =================================================================
enum Screen { SCREEN_MAIN, SCREEN_SETTINGS, SCREEN_KEYPAD };
enum FanMode { FAN_AUTO, FAN_ON, FAN_OFF };

#define BG_COLOR        0x1129
#define TITLE_BG_COLOR  0x32AE
#define PANEL_BG_COLOR  0x226B
#define TEXT_COLOR      TFT_WHITE
#define DATA_COLOR      TFT_YELLOW
#define ACCENT_COLOR    TFT_CYAN
#define BTN_COLOR       0x5415
#define BTN_OK_COLOR    0x4528
#define BTN_DEL_COLOR   0xC2A5
#define BTN_ACK_COLOR   0xFB20 // Bright Orange

// =================================================================
// 4. GLOBAL VARIABLES & OBJECTS
// =================================================================
TFT_eSPI tft = TFT_eSPI();
Preferences preferences;
DHT dht_indoor1(DHTPIN_INDOOR_1, DHTTYPE);
DHT dht_indoor2(DHTPIN_INDOOR_2, DHTTYPE);
DHT dht_outdoor(DHTPIN_OUTDOOR, DHTTYPE);

// --- UI State ---
Screen currentScreen = SCREEN_MAIN;

// --- Configurable Settings ---
String phoneNumbers[3];
float hum_on_thresh, hum_off_thresh, temp_on_thresh, temp_off_thresh;
bool voltage_sensor_enabled[3];
FanMode fan_mode;

// --- Data Variables ---
float temp_indoor = NAN, hum_indoor = NAN;
float temp_outdoor = NAN, hum_outdoor = NAN;
float weather_temp = NAN, weather_hum = NAN;
String weather_desc = "Loading...";
float phase_voltages[3] = {0.0, 0.0, 0.0};
bool fan_on = false;
bool power_on = true;
bool modem_ready = false; // Set true only after full modem init succeeds

// --- State & Timers ---
String keypad_buffer = "";
String setting_to_edit = "";
unsigned long last_sensor_read = 0;
unsigned long last_weather_update = 0;
bool power_outage_active = false;
bool initial_alert_sent = false;
bool alarm_acknowledged = false;
unsigned long power_outage_start_time = 0;
unsigned long power_restored_start_time = 0;
unsigned long last_reminder_time = 0;

// =================================================================
// 5. FORWARD DECLARATIONS
// =================================================================
void drawMainScreenLayout();
void updateMainScreenData();
void drawSettingsScreenLayout();
void updateSettingsScreenData();
void drawKeypadScreenLayout();
void updateKeypadBuffer();
// MODIFIED: Added forward declarations for new modem functions
bool sendAT(const char* command, const char* expected_response, unsigned long timeout);
bool sendSMS(const char* number, const char* message);
void sendBulkSMS(String message);

// =================================================================
// 6. CORE FUNCTIONS (DATA, LOGIC, & STORAGE)
// =================================================================
void loadSettings() {
    preferences.begin("full-config", false);
    phoneNumbers[0] = preferences.getString("phone1", "+491704732362");
    phoneNumbers[1] = preferences.getString("phone2", "+4915771913737");
    phoneNumbers[2] = preferences.getString("phone3", "+491725668993");
    hum_on_thresh = preferences.getFloat("humOn", 55.0);
    hum_off_thresh = preferences.getFloat("humOff", 50.0);
    temp_on_thresh = preferences.getFloat("tempOn", 26.0);
    temp_off_thresh = preferences.getFloat("tempOff", 12.0);
    voltage_sensor_enabled[0] = preferences.getBool("vs1En", true);
    voltage_sensor_enabled[1] = preferences.getBool("vs2En", true);
    voltage_sensor_enabled[2] = preferences.getBool("vs3En", true);
    fan_mode = (FanMode)preferences.getInt("fanMode", FAN_AUTO);
    preferences.end();
}

void saveSettings() {
    preferences.begin("full-config", true);
    preferences.putString("phone1", phoneNumbers[0]);
    preferences.putString("phone2", phoneNumbers[1]);
    preferences.putString("phone3", phoneNumbers[2]);
    preferences.putFloat("humOn", hum_on_thresh);
    preferences.putFloat("humOff", hum_off_thresh);
    preferences.putFloat("tempOn", temp_on_thresh);
    preferences.putFloat("tempOff", temp_off_thresh);
    preferences.putBool("vs1En", voltage_sensor_enabled[0]);
    preferences.putBool("vs2En", voltage_sensor_enabled[1]);
    preferences.putBool("vs3En", voltage_sensor_enabled[2]);
    preferences.putInt("fanMode", fan_mode);
    preferences.end();
}

void readDhtSensors() {
    float t1 = dht_indoor1.readTemperature(); float h1 = dht_indoor1.readHumidity();
    float t2 = dht_indoor2.readTemperature(); float h2 = dht_indoor2.readHumidity();
    temp_outdoor = dht_outdoor.readTemperature(); hum_outdoor = dht_outdoor.readHumidity();

    if (!isnan(t1) && !isnan(t2)) temp_indoor = (t1 + t2) / 2.0;
    else if (!isnan(t1)) temp_indoor = t1; else temp_indoor = t2;
    if (!isnan(h1) && !isnan(h2)) hum_indoor = (h1 + h2) / 2.0;
    else if (!isnan(h1)) hum_indoor = h1; else hum_indoor = h2;
}

void readVoltageSensors() {
    // ❗ IMPORTANT: Calibrate this value! See instructions.
    float calibration_constant = 190;  
    
    int pins[] = {VOLTAGE_SENSOR_1, VOLTAGE_SENSOR_2, VOLTAGE_SENSOR_3};

    for (int i = 0; i < 3; i++) {
        if (voltage_sensor_enabled[i]) {
            int min_val = 4095, max_val = 0;
            uint32_t start_time = millis();
            while(millis() - start_time < 100) { // Read for 100ms
                int val = analogRead(pins[i]);
                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;
            }
            int difference = max_val - min_val;
            if (difference > 150) { // Check if phase is active
                phase_voltages[i] = difference * (3.3 / 4095.0) * calibration_constant;
            } else {
                phase_voltages[i] = 0.0;
            }
        } else {
            phase_voltages[i] = 0.0;
        }
    }
}

void getWeatherData() {
    if (WiFi.status() != WL_CONNECTED) { weather_desc = "WiFi Offline"; return; }
    HTTPClient http;
    String serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&units=metric&APPID=" + openWeatherMapApiKey;
    http.begin(serverPath.c_str());
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
        DynamicJsonDocument doc(2048);
        if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
            weather_temp = doc["main"]["temp"];
            weather_hum = doc["main"]["humidity"];
            weather_desc = doc["weather"][0]["description"].as<String>();
            weather_desc.setCharAt(0, toupper(weather_desc.charAt(0)));
        } else { weather_desc = "JSON Error"; }
    } else { weather_desc = "HTTP Error"; }
    http.end();
}


// =================================================================
// --- MODIFIED: A7670E SMS FUNCTIONS ---
// =================================================================
bool sendAT(const char* command, const char* expected_response, unsigned long timeout) {
    while (Serial2.available()) { Serial2.read(); } // Clear buffer
    Serial.print(">>> Sending: ");
    Serial.println(command);
    Serial2.println(command);
    unsigned long startTime = millis();
    String response = "";
    while (millis() - startTime < timeout) {
        if (Serial2.available()) {
            char c = Serial2.read();
            response += c;
            if (response.indexOf(expected_response) != -1) {
                Serial.print("<<< Received: ");
                Serial.println(response);
                return true;
            }
        }
    }
    Serial.print("<<< Timeout. No valid response. Full response: ");
    Serial.println(response);
    return false;
}

bool sendSMS(const char* number, const char* message) {
    Serial.println("\n--- Sending SMS ---");
    if (!sendAT("AT+CMGF=1", "OK", 2000)) {
        Serial.println("Failed to set modem to text mode.");
        return false;
    }
    String cmd = "AT+CMGS=\"";
    cmd += number;
    cmd += "\"";
    if (!sendAT(cmd.c_str(), ">", 5000)) {
        Serial.println("Failed to initiate SMS sending.");
        return false;
    }
    Serial.print(">>> Sending message: ");
    Serial.println(message);
    Serial2.print(message);
    delay(100);
    Serial2.write(26); // ASCII code for Ctrl+Z
    unsigned long startTime = millis();
    String response = "";
    while (millis() - startTime < 20000) {
        if (Serial2.available()) {
            char c = Serial2.read();
            response += c;
            if (response.indexOf("OK") != -1 || response.indexOf("+CMGS:") != -1) {
                Serial.print("<<< Received: ");
                Serial.println(response);
                return true;
            } else if (response.indexOf("ERROR") != -1) {
                Serial.print("<<< Received ERROR: ");
                Serial.println(response);
                return false;
            }
        }
    }
    Serial.println("Timeout waiting for send confirmation.");
    return false;
}

void sendBulkSMS(String message) {
    if (!modem_ready) {
        Serial.println("--- SMS skipped: modem not ready ---");
        return;
    }
    Serial.println("--- Preparing to send bulk SMS ---");
    for (int i = 0; i < 3; i++) {
        if (phoneNumbers[i].length() > 5) {
            Serial.println("Sending to number " + String(i+1));
            sendSMS(phoneNumbers[i].c_str(), message.c_str());
            delay(1000); // Small delay between sending to different numbers
        }
    }
}
// --- END OF MODIFIED SMS FUNCTIONS ---

void checkFanAndPower() {
    // --- Fan Logic ---
    if (fan_mode == FAN_AUTO) {
        if (fan_on == false && (hum_indoor > hum_on_thresh || temp_indoor > temp_on_thresh)) {
            fan_on = true;
        } else if (fan_on == true && (hum_indoor < hum_off_thresh && temp_indoor < temp_off_thresh)) {
            fan_on = false;
        }
    } else if (fan_mode == FAN_ON) {
        fan_on = true;
    } else { // FAN_OFF
        fan_on = false;
    }
    digitalWrite(RELAY_PIN, fan_on ? LOW : HIGH);

    // --- Power Outage Logic ---
    power_on = (phase_voltages[0] > 100 || phase_voltages[1] > 100 || phase_voltages[2] > 100);
}

void handleSmsLogic() {
    static bool last_power_state = true;

    if (power_on != last_power_state) {
        if (!power_on) { // Power has just failed
            power_outage_active = true;
            power_outage_start_time = millis();
            initial_alert_sent = false;
            alarm_acknowledged = false;
        } else { // Power has just been restored
            power_restored_start_time = millis();
        }
        last_power_state = power_on;
        if(currentScreen == SCREEN_MAIN) {
            drawMainScreenLayout();
            updateMainScreenData();
        }
    }

    if (power_outage_active) {
        if (!power_on) { // If power is still out...
            if (!initial_alert_sent && millis() - power_outage_start_time > SMS_INITIAL_DELAY) {
                sendBulkSMS("Achtung: Stromausfall seit 10 Minuten!");
                initial_alert_sent = true;
                last_reminder_time = millis();
            }
            if (initial_alert_sent && !alarm_acknowledged && millis() - last_reminder_time > SMS_REMINDER_INTERVAL) {
                sendBulkSMS("Erinnerung: Stromausfall besteht weiterhin.");
                last_reminder_time = millis();
            }
        } else { // If power is back on...
            if (millis() - power_restored_start_time > SMS_RESTORE_DELAY) {
                sendBulkSMS("Info: Stromversorgung wurde wiederhergestellt.");
                power_outage_active = false;
                 if(currentScreen == SCREEN_MAIN) {
                    drawMainScreenLayout();
                    updateMainScreenData();
                }
            }
        }
    }
}


// =================================================================
// 7. UI DRAWING & TOUCH FUNCTIONS
// =================================================================
// (This entire section is unchanged and remains as provided in your original code)
void drawMainScreenLayout() {
    tft.fillScreen(BG_COLOR);
    tft.fillRect(0, 0, 480, 40, TITLE_BG_COLOR);
    tft.setTextColor(TFT_WHITE, TITLE_BG_COLOR);
    tft.drawString(" Climate & Power Monitor System", 10, 8, 4);

    if (power_outage_active && !alarm_acknowledged) {
        tft.fillRoundRect(350, 5, 125, 30, 8, BTN_ACK_COLOR);
        tft.setTextColor(TFT_BLACK, BTN_ACK_COLOR);
        tft.drawCentreString("Acknowledge", 412, 13, 2);
    }

    tft.drawRoundRect(5, 45, 470, 40, 5, PANEL_BG_COLOR);
    tft.drawRoundRect(5, 90, 470, 40, 5, PANEL_BG_COLOR);
    tft.drawRoundRect(5, 135, 470, 40, 5, PANEL_BG_COLOR);
    tft.drawRoundRect(5, 180, 470, 40, 5, PANEL_BG_COLOR);
    tft.drawRoundRect(5, 225, 470, 40, 5, PANEL_BG_COLOR);

    tft.fillRoundRect(5, 275, 230, 40, 8, BTN_COLOR);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Fan Mode", 120, 287, 4);
    tft.fillRoundRect(245, 275, 230, 40, 8, BTN_COLOR);
    tft.drawCentreString("Settings", 360, 287, 4);
}

void updateMainScreenData() {
    char buf[128];
    tft.setTextFont(4);
    
    // Indoor
    tft.fillRect(150, 50, 320, 30, BG_COLOR);
    if (!isnan(temp_indoor)) sprintf(buf, "%.1f C | %.0f %% RH", temp_indoor, hum_indoor);
    else sprintf(buf, "No Sensor Data");
    tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.drawString("Indoor:", 15, 55);
    tft.setTextColor(DATA_COLOR, BG_COLOR); tft.drawString(buf, 150, 55);

    // Outdoor
    tft.fillRect(150, 95, 320, 30, BG_COLOR);
    if (!isnan(temp_outdoor)) sprintf(buf, "%.1f C | %.0f %% RH", temp_outdoor, hum_outdoor);
    else sprintf(buf, "No Sensor Data");
    tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.drawString("Outdoor:", 15, 100);
    tft.setTextColor(DATA_COLOR, BG_COLOR); tft.drawString(buf, 150, 100);

    // Weather
    tft.fillRect(150, 140, 320, 30, BG_COLOR);
    if (!isnan(weather_temp)) sprintf(buf, "%.1f C | %.0f %% | %s", weather_temp, weather_hum, weather_desc.c_str());
    else sprintf(buf, "%s", weather_desc.c_str());
    tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.drawString("Weather:", 15, 145);
    tft.setTextColor(DATA_COLOR, BG_COLOR); tft.drawString(buf, 150, 145);

    // Voltage
    tft.fillRect(150, 185, 320, 30, BG_COLOR);
    String voltage_str;
    if (voltage_sensor_enabled[0]) voltage_str += "L1:" + String(phase_voltages[0], 0) + "V ";
    if (voltage_sensor_enabled[1]) voltage_str += "L2:" + String(phase_voltages[1], 0) + "V ";
    if (voltage_sensor_enabled[2]) voltage_str += "L3:" + String(phase_voltages[2], 0) + "V";
    tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.drawString("Voltage:", 15, 190);
    tft.setTextColor(DATA_COLOR, BG_COLOR); tft.drawString(voltage_str, 150, 190);

    // System
    tft.fillRect(150, 230, 320, 30, BG_COLOR);
    String fan_str = (fan_mode == FAN_ON) ? "ON" : (fan_mode == FAN_OFF) ? "OFF" : "AUTO";
    sprintf(buf, "Fan: %s (%s) | Pwr: %s", fan_str.c_str(), fan_on ? "ON" : "OFF", power_on ? "ON" : "OFF");
    tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.drawString("System:", 15, 235);
    tft.setTextColor(DATA_COLOR, BG_COLOR); tft.drawString(buf, 150, 235);
}

void drawSettingsScreenLayout() {
    tft.fillScreen(BG_COLOR);
    tft.fillRect(0, 0, 480, 40, TITLE_BG_COLOR);
    tft.setTextColor(TFT_WHITE, TITLE_BG_COLOR);
    tft.drawCentreString("Settings", 240, 13, 4);

    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setTextFont(2);
    tft.drawString("Phone #1 (Sender):", 10, 50);
    tft.drawString("Phone #2:", 10, 80);
    tft.drawString("Phone #3:", 10, 110);
    tft.drawString("Humidity ON/OFF Thresh (%RH):", 10, 140);
    tft.drawString("Temp ON/OFF Thresh (C):", 10, 170);
    tft.drawString("Voltage Sensors (L1/L2/L3):", 10, 200);

    tft.fillRoundRect(355, 275, 120, 40, 8, BTN_OK_COLOR);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Save & Exit", 415, 287, 2);
}

void updateSettingsScreenData() {
    tft.setTextFont(2);
    char buf[40];
    tft.fillRect(100, 48, 370, 180, BG_COLOR);
    
    for(int i=0; i<3; i++){
        tft.setTextColor(DATA_COLOR, BG_COLOR);
        tft.drawString(phoneNumbers[i], 100, 50 + i*30);
        tft.fillRoundRect(410, 48 + i*30, 60, 25, 5, BTN_COLOR);
        tft.setTextColor(TFT_WHITE);
        tft.drawCentreString("Edit", 440, 59 + i*30, 2);
    }
    
    sprintf(buf, "%.1f / %.1f", hum_on_thresh, hum_off_thresh);
    tft.setTextColor(DATA_COLOR, BG_COLOR); tft.drawString(buf, 270, 140);
    tft.fillRoundRect(410, 138, 60, 25, 5, BTN_COLOR);
    tft.setTextColor(TFT_WHITE); tft.drawCentreString("Edit", 440, 149, 2);

    sprintf(buf, "%.1f / %.1f", temp_on_thresh, temp_off_thresh);
    tft.setTextColor(DATA_COLOR, BG_COLOR); tft.drawString(buf, 270, 170);
    tft.fillRoundRect(410, 168, 60, 25, 5, BTN_COLOR);
    tft.setTextColor(TFT_WHITE); tft.drawCentreString("Edit", 440, 179, 2);
    
    for(int i=0; i<3; i++){
      uint16_t color = voltage_sensor_enabled[i] ? BTN_OK_COLOR : BTN_DEL_COLOR;
      String text = voltage_sensor_enabled[i] ? "ON" : "OFF";
      tft.fillRoundRect(250 + i*75, 198, 65, 25, 5, color);
      tft.setTextColor(TFT_WHITE);
      tft.drawCentreString(text, 282 + i*75, 209, 2);
    }
}

void updateKeypadBuffer() {
    tft.setTextFont(4);
    tft.setTextColor(DATA_COLOR, TFT_BLACK);
    tft.fillRect(20, 42, 440, 30, TFT_BLACK);
    tft.drawString(keypad_buffer, 25, 45);
}

void drawKeypadScreenLayout() {
    tft.fillScreen(BG_COLOR);
    tft.fillRoundRect(10, 10, 460, 60, 8, TFT_BLACK);
    tft.drawRoundRect(10, 10, 460, 60, 8, ACCENT_COLOR);
    tft.setTextColor(TEXT_COLOR, TFT_BLACK);
    tft.drawString("Enter Value (use / for thresholds):", 20, 18, 2);
    
    const char* keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "/", "0", "."};
    int key_w = 110, key_h = 50, gap = 10;
    
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(4);
    for (int i = 0; i < 12; i++) {
        int r = i / 3, c = i % 3;
        int x = 10 + c * (key_w + gap), y = 80 + r * (key_h + gap);
        uint16_t color = (strcmp(keys[i], "/") == 0 || strcmp(keys[i], ".") == 0) ? BTN_DEL_COLOR : BTN_COLOR;
        tft.fillRoundRect(x, y, key_w, key_h, 8, color);
        tft.drawCentreString(keys[i], x + key_w / 2, y + 17, 4);
    }
    int back_x = 10 + 3 * (key_w + gap);
    tft.fillRoundRect(back_x, 80, key_w, key_h * 2 + gap, 8, BTN_DEL_COLOR);
    tft.drawCentreString("<-", back_x + key_w / 2, 80 + key_h - 8, 4);
    tft.fillRoundRect(back_x, 80 + (key_h + gap) * 2, key_w, key_h * 2 + gap, 8, BTN_OK_COLOR);
    tft.drawCentreString("OK", back_x + key_w / 2, 80 + (key_h + gap) * 2 + key_h - 8, 4);
}

void handleTouch() {
    uint16_t x, y;
    if (!tft.getTouch(&x, &y)) return;
    static uint32_t last_touch_time = 0;
    if (millis() - last_touch_time < 300) return; // Debounce delay
    last_touch_time = millis();
    

    if (tft.getRotation() == 3) {
      x = tft.width() - x;
    } else if (tft.getRotation() == 1) {
      y = tft.height() - y;
    }

    if (currentScreen == SCREEN_MAIN) {
        if (y > 275 && y < 315) {
            if (x > 5 && x < 235) { // Fan Mode button
                fan_mode = (FanMode)((fan_mode + 1) % 3);
                saveSettings(); updateMainScreenData();
            } else if (x > 245 && x < 475) { // Settings button
                currentScreen = SCREEN_SETTINGS;
                drawSettingsScreenLayout();
                updateSettingsScreenData();
            }
        }
        if (power_outage_active && !alarm_acknowledged && y < 40 && x > 350) { // Acknowledge button
            alarm_acknowledged = true;
            drawMainScreenLayout();
            updateMainScreenData();
        }
    } else if (currentScreen == SCREEN_SETTINGS) {
        if (y > 275 && y < 315 && x > 355 && x < 475) { // Save & Exit button
            saveSettings();
            currentScreen = SCREEN_MAIN;
            drawMainScreenLayout();
            updateMainScreenData(); return;
        }
        if (y > 198 && y < 223) { // Voltage Sensor Toggles
            if (x > 250 && x < 315) voltage_sensor_enabled[0] = !voltage_sensor_enabled[0];
            else if (x > 325 && x < 390) voltage_sensor_enabled[1] = !voltage_sensor_enabled[1];
            else if (x > 400 && x < 465) voltage_sensor_enabled[2] = !voltage_sensor_enabled[2];
            updateSettingsScreenData(); return;
        }
        if (x > 410 && x < 470) { // Edit buttons
            if (y > 48 && y < 73) { setting_to_edit = "phone1"; keypad_buffer = phoneNumbers[0]; }
            else if (y > 78 && y < 103) { setting_to_edit = "phone2"; keypad_buffer = phoneNumbers[1]; }
            else if (y > 108 && y < 133) { setting_to_edit = "phone3"; keypad_buffer = phoneNumbers[2]; }
            else if (y > 138 && y < 163) { setting_to_edit = "hum"; keypad_buffer = ""; }
            else if (y > 168 && y < 193) { setting_to_edit = "temp"; keypad_buffer = ""; }
            
            if (!setting_to_edit.isEmpty()) {
                currentScreen = SCREEN_KEYPAD;
                drawKeypadScreenLayout();
                updateKeypadBuffer();
            }
        }

    } else if (currentScreen == SCREEN_KEYPAD) {
        int key_w = 110, key_h = 50, gap = 10;
        const char* keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "/", "0", "."};
        
        for (int i = 0; i < 12; i++) {
            int r = i / 3, c = i % 3; int kx = 10 + c * (key_w + gap), ky = 80 + r * (key_h + gap);
            if (x > kx && x < kx + key_w && y > ky && y < ky + key_h) {
                bool is_numeric_field = (setting_to_edit == "hum" || setting_to_edit == "temp");
                if (!is_numeric_field && (strcmp(keys[i], "/") == 0 || strcmp(keys[i], ".") == 0)) return;

                if (keypad_buffer.length() < 25) keypad_buffer += keys[i];
                updateKeypadBuffer(); return;
            }
        }
        int side_x = 10 + 3 * (key_w + gap);
        if (x > side_x && x < side_x + key_w) {
            if (y > 80 && y < 80 + key_h * 2 + gap) { // Backspace
                if (keypad_buffer.length() > 0) keypad_buffer.remove(keypad_buffer.length() - 1);
                updateKeypadBuffer(); return;
            }
            if (y > 80 + (key_h + gap) * 2) { // OK button
                if (setting_to_edit == "phone1") phoneNumbers[0] = keypad_buffer;
                else if (setting_to_edit == "phone2") phoneNumbers[1] = keypad_buffer;
                else if (setting_to_edit == "phone3") phoneNumbers[2] = keypad_buffer;
                else if (setting_to_edit == "hum" || setting_to_edit == "temp") {
                    int slash_index = keypad_buffer.indexOf('/');
                    if (slash_index > 0) {
                        float val1 = keypad_buffer.substring(0, slash_index).toFloat();
                        float val2 = keypad_buffer.substring(slash_index + 1).toFloat();
                        if (setting_to_edit == "hum") { hum_on_thresh = val1; hum_off_thresh = val2; }
                        else { temp_on_thresh = val1; temp_off_thresh = val2; }
                    }
                }
                setting_to_edit = "";
                currentScreen = SCREEN_SETTINGS;
                drawSettingsScreenLayout();
                updateSettingsScreenData();
            }
        }
    }
}
// =================================================================
// 8. SETUP & LOOP
// =================================================================
void setup() {
    Serial.begin(115200);
    // MODIFIED: Updated Baud Rate for A7670E
    Serial2.begin(115200, SERIAL_8N1, A7670E_RX_PIN, A7670E_TX_PIN);
    
    tft.init();
    tft.setRotation(3);
    
    dht_indoor1.begin();
    dht_indoor2.begin();
    dht_outdoor.begin();

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH); // Relays off
    
    pinMode(VOLTAGE_SENSOR_1, INPUT);
    pinMode(VOLTAGE_SENSOR_2, INPUT);
    pinMode(VOLTAGE_SENSOR_3, INPUT);

    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Initializing...", tft.width()/2, tft.height()/2 - 40, 4);

    loadSettings();

    tft.drawCentreString("Connecting to WiFi...", tft.width()/2, tft.height()/2 - 10, 2);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {  
      delay(500);  
      Serial.print(".");
      attempts++;
    }

    // --- MODIFIED: A7670E MODEM INITIALIZATION (non-blocking) ---
    tft.drawCentreString("Initializing Modem...", tft.width()/2, tft.height()/2 + 10, 2);
    Serial.println("Initializing modem...");
    
    // Retry AT handshake up to 5 times (modem may need time to boot)
    bool modem_responded = false;
    for (int retry = 0; retry < 5; retry++) {
        if (sendAT("AT", "OK", 2000)) {
            modem_responded = true;
            break;
        }
        Serial.println("Modem not responding, retrying...");
        delay(1000);
    }
    
    if (!modem_responded) {
        tft.drawCentreString("MODEM FAILED - SMS disabled", tft.width()/2, tft.height()/2 + 30, 2);
        Serial.println("Modem init failed. Continuing without SMS.");
        delay(2000);
    } else {
        sendAT("ATE0", "OK", 1000); // Turn off echo

        // SIM PIN Handling
        bool sim_ok = false;
        if (sendAT("AT+CPIN?", "SIM PIN", 2000)) {
            tft.drawCentreString("Unlocking SIM...", tft.width()/2, tft.height()/2 + 30, 2);
            String pin_cmd = "AT+CPIN=\"";
            pin_cmd += SIM_PIN;
            pin_cmd += "\"";
            if (!sendAT(pin_cmd.c_str(), "OK", 5000)) {
                tft.drawCentreString("SIM UNLOCK FAILED", tft.width()/2, tft.height()/2 + 50, 2);
                Serial.println("SIM unlock failed. SMS disabled.");
                delay(2000);
            } else {
                delay(3000); // Give SIM time to fully initialize after PIN
                sim_ok = true;
            }
        } else if (sendAT("AT+CPIN?", "READY", 2000)) {
            // SIM has no PIN or already unlocked
            sim_ok = true;
        } else {
            tft.drawCentreString("SIM NOT DETECTED", tft.width()/2, tft.height()/2 + 30, 2);
            Serial.println("SIM not detected. SMS disabled.");
            delay(2000);
        }

        if (sim_ok) {
            // Check Network Registration (retry a few times)
            tft.drawCentreString("Registering on Network...", tft.width()/2, tft.height()/2 + 50, 2);
            bool registered = false;
            for (int retry = 0; retry < 3; retry++) {
                if (sendAT("AT+CGREG?", "+CGREG: 0,1", 5000) || sendAT("AT+CGREG?", "+CGREG: 0,5", 5000)) {
                    registered = true;
                    break;
                }
                delay(2000);
            }
            if (registered) {
                modem_ready = true;
                tft.drawCentreString("Modem Ready!", tft.width()/2, tft.height()/2 + 70, 2);
                Serial.println("Modem initialized successfully. SMS enabled.");
            } else {
                tft.drawCentreString("NO NETWORK - SMS disabled", tft.width()/2, tft.height()/2 + 70, 2);
                Serial.println("Network registration failed. SMS disabled.");
            }
        }
        delay(1000);
    }
    // --- END OF MODEM INITIALIZATION ---

    // Initial sensor reads and UI draw
    drawMainScreenLayout();
    readDhtSensors();
    readVoltageSensors();
    getWeatherData();
    checkFanAndPower();
    updateMainScreenData();
}

void loop() {
    handleTouch();
    handleSmsLogic();

    if (currentScreen == SCREEN_MAIN) {
        if (millis() - last_sensor_read >= SENSOR_READ_INTERVAL) {
            last_sensor_read = millis();
            readDhtSensors();
            readVoltageSensors();
            checkFanAndPower();
            updateMainScreenData();
        }
        if (millis() - last_weather_update >= WEATHER_UPDATE_INTERVAL) {
            last_weather_update = millis();
            getWeatherData();
            updateMainScreenData();
        }
    }
}