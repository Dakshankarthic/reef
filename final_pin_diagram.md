# ESP32-S3 N16R8 — Final Pin Diagram

> [!IMPORTANT]
> All 17 GPIOs used. Total: 6 for Display SPI, 2 for Touch, 3 for DHT22, 1 for Relay, 3 for Voltage, 2 for Modem UART.

---

## Complete Pin Mapping Table

| GPIO | Function | Connects To | Notes |
|------|----------|-------------|-------|
| **1** | ADC Input | ZMPT101B Phase 1 (Signal) | Voltage Sensor L1 |
| **2** | Digital Input | DHT22 Outdoor (Data) | 10kΩ pull-up to 3.3V |
| **10** | Digital Output | Relay Module (IN) | Active LOW, Fan control |
| **4** | SPI CS | ST7796 Display (CS) | Display chip select |
| **5** | Digital Input | DHT22 Indoor 1 (Data) | 10kΩ pull-up to 3.3V |
| **6** | Digital Input | DHT22 Indoor 2 (Data) | 10kΩ pull-up to 3.3V |
| **7** | ADC Input | ZMPT101B Phase 2 (Signal) | Voltage Sensor L2 |
| **8** | ADC Input | ZMPT101B Phase 3 (Signal) | Voltage Sensor L3 |
| **9** | SPI DC | ST7796 Display (DC) | Data/Command select |
| **11** | Digital Output | ST7796 Display (BL) | Backlight, driven HIGH |
| **12** | SPI MISO | ST7796 Display (SDO) + XPT2046 (DO) | Shared SPI bus |
| **13** | SPI MOSI | ST7796 Display (SDI) + XPT2046 (DIN) | Shared SPI bus |
| **14** | SPI SCLK | ST7796 Display (SCK) + XPT2046 (CLK) | Shared SPI bus |
| **15** | Digital Output | ST7796 Display (RST) | Display reset |
| **16** | UART1 RX | A7670E Modem (**TX** pin) | ⚡ Cross-wired! |
| **17** | UART1 TX | A7670E Modem (**RX** pin) | ⚡ Cross-wired! |
| **18** | SPI CS | XPT2046 Touch (CS) | Touch chip select |
| **21** | Digital Input | XPT2046 Touch (IRQ) | Optional touch interrupt |

---

## Wiring by Module

### 🖥️ ST7796 Display (SPI)
```
Display Pin    →    ESP32-S3 GPIO
─────────────────────────────────
VCC            →    3.3V
GND            →    GND
CS             →    GPIO 4
RST            →    GPIO 15
DC             →    GPIO 9
SDI (MOSI)     →    GPIO 13
SCK (SCLK)     →    GPIO 14
SDO (MISO)     →    GPIO 12
BL             →    GPIO 11
```

### 👆 XPT2046 Touch (Shared SPI)
```
Touch Pin      →    ESP32-S3 GPIO
─────────────────────────────────
VCC            →    3.3V
GND            →    GND
CS             →    GPIO 18
CLK            →    GPIO 14  (shared with display)
DIN            →    GPIO 13  (shared with display)
DO             →    GPIO 12  (shared with display)
IRQ            →    GPIO 21  (optional)
```

### 🌡️ DHT22 Sensors (×3)
```
Sensor         →    ESP32-S3 GPIO
─────────────────────────────────
Indoor 1 Data  →    GPIO 5   (+ 10kΩ pull-up to 3.3V)
Indoor 2 Data  →    GPIO 6   (+ 10kΩ pull-up to 3.3V)
Outdoor  Data  →    GPIO 2   (+ 10kΩ pull-up to 3.3V)
All VCC        →    3.3V
All GND        →    GND
```

### ⚡ ZMPT101B Voltage Sensors (×3)
```
Sensor         →    ESP32-S3 GPIO
─────────────────────────────────
Phase 1 (L1)   →    GPIO 1   (ADC)
Phase 2 (L2)   →    GPIO 7   (ADC)
Phase 3 (L3)   →    GPIO 8   (ADC)
All VCC        →    5V
All GND        →    GND
```

### 📱 A7670E SIM Modem (UART1)
```
Modem Pin      →    ESP32-S3 GPIO
─────────────────────────────────
TX             →    GPIO 16  (ESP RX ← Modem TX)
RX             →    GPIO 17  (ESP TX → Modem RX)
VCC            →    5V (or dedicated power supply)
GND            →    GND (common ground!)
```

> [!CAUTION]
> **UART is cross-wired!** Modem TX goes to ESP RX (GPIO16), Modem RX goes to ESP TX (GPIO17). Do NOT connect TX-to-TX!

### 🌀 Relay Module
```
Relay Pin      →    ESP32-S3 GPIO
─────────────────────────────────
IN             →    GPIO 10  (Active LOW)
VCC            →    5V
GND            →    GND
```

---

## Visual Board Layout

```
                    ESP32-S3 N16R8
                 ┌──────────────────┐
         GPIO 1  │●  [Volt L1]    ●│  GPIO 2  [DHT Outdoor]
         GPIO 3  │●              ●│  GPIO 42
         GPIO 4  │●  [Disp CS]   ●│  GPIO 41
         GPIO 5  │●  [DHT In1]   ●│  GPIO 40
         GPIO 6  │●  [DHT In2]   ●│  GPIO 39
         GPIO 7  │●  [Volt L2]   ●│  GPIO 38
        GPIO 15  │●  [Disp RST]  ●│  GPIO 37
        GPIO 16  │●  [Modem TX→] ●│  GPIO 36
        GPIO 17  │●  [←Modem RX] ●│  GPIO 35
        GPIO 18  │●  [Touch CS]  ●│  GPIO 0
         GPIO 8  │●  [Volt L3]   ●│  GPIO 45
         GPIO 3  │●              ●│  GPIO 46
         GPIO 46 │●              ●│  GPIO 48
         GPIO 9  │●  [Disp DC]   ●│  GPIO 47
        GPIO 10  │●  [Relay]     ●│  GPIO 21 [Touch IRQ]
        GPIO 11  │●  [Disp BL]   ●│  GPIO 20
        GPIO 12  │●  [SPI MISO]  ●│  GPIO 19
        GPIO 13  │●  [SPI MOSI]  ●│
        GPIO 14  │●  [SPI SCLK]  ●│
             5V  │●              ●│  5V
            GND  │●              ●│  GND
                 └──────────────────┘
```

---

## Power Summary

| Rail | Consumers |
|------|-----------|
| **3.3V** | Display, Touch, DHT22 ×3 |
| **5V** | ZMPT101B ×3, Relay, A7670E Modem |
| **GND** | All modules (common ground) |

> [!WARNING]
> The A7670E modem can draw up to **2A** during transmission. Use a separate 5V supply or a robust regulator — do NOT rely solely on the ESP32-S3's USB 5V pin for powering the modem.
