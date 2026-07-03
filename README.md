# ESP32 3-Phase Smart Climate & Voltage Monitor

![ESP32 Smart Monitor](https://img.shields.io/badge/Board-ESP32--S3-blue.svg)
![Framework](https://img.shields.io/badge/Framework-Arduino-00979C.svg)
![Build](https://img.shields.io/badge/Build-PlatformIO-orange.svg)

A comprehensive smart monitoring system built on the ESP32-S3. This system combines 3-phase AC voltage monitoring, environmental tracking (temperature/humidity), automated fan control, and reliable 4G SMS alerts for power outages using an A7670E LTE module. It features a rich touchscreen UI for real-time monitoring and configuration.

## 🌟 Key Features

*   **⚡ 3-Phase Voltage Monitoring:** Independently monitors three AC voltage phases using three isolated ZMPT101B sensors.
*   **🌡️ Climate Control:** Reads data from multiple DHT22 sensors (2 indoor, 1 outdoor) and automatically controls a ventilation fan via relay based on customizable temperature and humidity thresholds.
*   **📱 4G SMS Alerts:** Integrates an A7670E LTE module to instantly send SMS alerts (up to 3 configurable phone numbers) when a power outage is detected on any phase, with periodic reminders and restoration messages.
*   **☁️ Weather Integration:** Fetches and displays real-time local weather data (temperature, humidity, conditions) via the OpenWeatherMap API.
*   **👆 Touchscreen UI:** Provides an intuitive multi-page interface (Main Dashboard, Settings, Keypad) on an ST7796 TFT display to view data and easily adjust configuration parameters without reprogramming.
*   **💾 Persistent Storage:** Saves all user settings (thresholds, phone numbers, fan modes, sensor toggles) to non-volatile memory.

## 🛠️ Hardware Requirements

*   **Microcontroller:** ESP32-S3 (e.g., ESP32-S3-DevKitC-1, N16R8)
*   **Display:** SPI TFT Display with Touch (ST7796 driver + XPT2046 touch controller)
*   **Voltage Sensors:** 3x ZMPT101B AC Voltage Sensor Modules
*   **Climate Sensors:** 3x DHT22 Temperature & Humidity Sensors
*   **Cellular Modem:** A7670E 4G LTE Module (for SMS)
*   **Actuator:** 1x 5V/12V Relay Module (for fan control)

## 📍 Pin Configuration

| Component | Pin (ESP32-S3) | Description |
| :--- | :--- | :--- |
| **TFT Display (SPI)** | `MOSI: 13`, `MISO: 12`, `SCLK: 14`, `CS: 4`, `DC: 9`, `RST: 15`, `BL: 11` | ST7796 Display |
| **Touch Controller** | `CS: 18` | XPT2046 (Shares SPI bus) |
| **DHT22 Sensors** | `Indoor 1: 25`, `Indoor 2: 26`, `Outdoor: 14` | Temperature & Humidity |
| **Voltage Sensors** | `L1: 32`, `L2: 33`, `L3: 34` | ZMPT101B Analog Inputs |
| **A7670E Modem** | `RX: 16`, `TX: 17` | Hardware Serial 2 |
| **Fan Relay** | `Relay: 27` | Active-LOW control |

## 🚀 Getting Started

### 1. Prerequisites

*   Install [PlatformIO](https://platformio.org/) in VS Code.
*   Get a free API key from [OpenWeatherMap](https://openweathermap.org/api).

### 2. Configuration

Before uploading, open `finial.ino` and update the following credentials:

```cpp
// --- WiFi & Weather ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
String openWeatherMapApiKey = "YOUR_OWM_API_KEY";
String city = "YourCity";
String countryCode = "YourCountryCode"; // e.g., "DE", "US"

// --- SIM Card ---
#define SIM_PIN "YOUR_SIM_PIN" // Replace with your actual SIM PIN
```

*Note: For the TFT display to work correctly, ensure your `User_Setup.h` (for TFT_eSPI) matches the pin configuration provided in `platformio.ini`.*

### 3. Build & Upload

Use PlatformIO to build and upload the firmware. The project utilizes a custom partition scheme (`huge_app.csv`) to accommodate the firmware and UI assets.

## 🖥️ User Interface Overview

*   **Main Dashboard:** Displays current indoor/outdoor climate data, live API weather, individual L1/L2/L3 voltage status, and active system states (Fan / Power). Features quick-access buttons for toggling Fan Mode (AUTO/ON/OFF) and entering Settings.
*   **Settings Menu:** Allows you to define up to three alert phone numbers, customize ON/OFF thresholds for temperature and humidity, and selectively enable/disable individual voltage sensors.
*   **Keypad:** An on-screen numeric keypad for seamless entry of values directly on the device.

## ⚠️ Important Notes

*   **Voltage Calibration:** The ZMPT101B sensors require calibration. Adjust the `calibration_constant` variable in the `readVoltageSensors()` function against a known multimeter reading for accurate voltage display.
*   **Modem Power:** The A7670E module can draw significant current spikes during transmission. Ensure it has a dedicated and robust power supply (usually 5V/2A or higher).
*   **Safety:** You are dealing with MAINS AC VOLTAGE (via the ZMPT101B sensors). Always exercise extreme caution, ensure proper isolation, and disconnect power before making any wiring changes.

## 📜 License

This project is open-source and available for personal and educational use.
