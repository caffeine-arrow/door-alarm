# ESP32 Web-Controlled Door Security System

![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-1B5E20)
![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-2E7D32)
![Language: C++](https://img.shields.io/badge/Language-C%2B%2B-388E3C)
![Setup: DIY Friendly](https://img.shields.io/badge/Setup-DIY%20Friendly-4CAF50)

A simple, fast door security system built for the ESP32. It monitors a magnetic door sensor, hosts a local web interface with live updates, and triggers a buzzer with distinct chimes and alarm patterns without slowing down the core loop.

---

## Features

* Live Web UI: Responsive control dashboard that refreshes every 500ms for accurate real-time door status.
* Smart Hush: Instantly silences the alarm. If the door is already closed, it clears the alert and re-arms immediately.
* Custom Chimes: Non-blocking High-Low sound pattern when opened, and a High-High pattern when closed.
* Alarm Cadence: Fast 150ms ON / 50ms OFF buzzer pattern when the alarm is triggered or tested.
* Memory Persistence: Saves your alarm and chime volume settings to flash memory so they stay the same after a reboot.
* Night Mode: Automatically arms itself between 12:00 AM and 4:00 AM. If you manually disarm it during these hours, it waits 1 hour before auto-arming again.

---

## Pinout Mapping

| Component | ESP32 Pin | Type | Default State |
| :--- | :--- | :--- | :--- |
| Magnetic Reed Switch | GPIO 13 | Input (Internal Pullup) | High = Open / Low = Closed |
| Active Buzzer | GPIO 25 | Output (LEDC PWM) | Frequency & Volume Modulated |
| Armed Status LED | GPIO 26 | Output | System State Indicator |
| Door Status LED | GPIO 27 | Output | Hardware Reed Copy Indicator |

---

## Quick Setup

1. Open the code in the Arduino IDE and select your board as **ESP32 Dev Module**.
2. Change the `ssid` and `password` variables to match your local network.
3. Flash the code to your ESP32.
4. Open a browser and type `http://dooralarm.local` or the ESP32's IP address to access the dashboard.
