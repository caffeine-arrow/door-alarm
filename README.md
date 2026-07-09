# ESP32 Smart Security Core

[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-1B5E20?style=flat-square&logo=espressif&logoColor=white)](https://www.espressif.com/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-2E7D32?style=flat-square&logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![Language: C++](https://img.shields.io/badge/Language-C%2B%2B-388E3C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)
![Protocol: HTTP / mDNS](https://img.shields.io/badge/Protocol-HTTP%20%2F%20mDNS-43A047?style=flat-square)
![Status: Production Ready](https://img.shields.io/badge/Status-Production%20Ready-4CAF50?style=flat-square)

---

An ultra-lightweight, high-performance physical security engine developed specifically for the ESP32 architecture using optimized asynchronous loop structures and zero-blocking delays.

---

## System Architecture
+------------------------+      +-------------------+      +-----------------------+
|  Magnetic Reed Sensor  | ---> |   ESP32 Core      | ---> |  Active Piezo Buzzer  |
|  (GPIO 13 / Pullup)    |      |   State Engine    |      |  (GPIO 25 / PWM)      |
+------------------------+      +-------------------+      +-----------------------+
|
v
+---------------------+
|   Material 3 UI     |
|   (500ms Polling)   |
+---------------------+
### Technical Profile
* **Hardware Core**: Espressif Systems ESP32 NodeMCU
* **Audio Pipeline**: Non-blocking hardware LEDC PWM Generator
* **State Matrix**: Synchronized Atomic Core Loops with dynamic NTP time tracking
* **User Interface**: Minified Material 3 Dashboard with async AJAX data polling

---

## Core Features

### Night Mode Guarding Matrix
The core system implements an atomic background time fence. It shifts states dynamically without locking the processor:
* **Auto-Arm Range**: Active enforcement between 12:00 AM and 4:00 AM.
* **Dynamic Defeat Protection**: If a user manual Disarm occurs inside this timeframe, a persistent loop triggers to auto-rearm the architecture exactly 1 hour later.

### Advanced Non-Blocking Audio Framework
Traditional delays freeze system processing. This framework monitors loop tick-counts natively to multiplex audio alerts and chimes seamlessly:
* **Fire Alarm Alert**: Pulses an authoritative high-decibel frequency on an exact 150ms ON / 50ms OFF pattern during breach states or manual hardware diagnostics.
* **Door Open Chime**: Fires a descending High-Low frequency wave instantly when the reed layout breaks circuit.
* **Door Close Chime**: Fires an abrupt High-High sequence the precise microsecond the circuit closes.

### Persistent Memory Engine
Utilizes the onboard ESP32 non-volatile Flash layout (`Preferences.h`). Audio and system properties remain safely indexed during system reboots or unannounced power failures.

---

## Pin Configuration Mapping

| Component | Target ESP32 Pin | Interface Type | Standard Status |
| :--- | :--- | :--- | :--- |
| **Magnetic Reed Sensor** | GPIO 13 | Digital Input (Internal Pullup) | High = Open / Low = Closed |
| **Active Sound Buzzer** | GPIO 25 | LEDC PWM Engine Profile | Variable Duty Cycle Resolution |
| **Armed Status LED** | GPIO 26 | Digital Output Driver | Multiple Blink Profiles |
| **Door Sensor Status LED** | GPIO 27 | Digital Output Driver | Structural Indicator |

---

## UI Integration Details

The web server delivers an embedded, minified interface optimized to reduce transmission size down to individual bytes. The layout features an expanded full-width "Hush Alarm" interface that scales gracefully to fit mobile and desktop viewports.

```javascript
// High-efficiency asynchronous query loop
setInterval(function UpdateUI() {
  fetch('/st')
    .then(response => response.json())
    .then(data => {
      // High-frequency UI status rendering engine updates at 500ms
    });
}, 500);
