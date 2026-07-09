#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// --- Hardware Pins ---
const int REED_PIN = 13;
const int BUZZER_PIN = 25;
const int ARMED_LED_PIN = 26;
const int DOOR_LED_PIN = 27;

// --- WiFi Credentials ---
const char* ssid = "IoT-Net";
const char* password = "IoT@26/P12";

// --- Web Server Instance ---
WebServer server(80);

// --- System State Variables ---
bool isArmed = true;           // Active fully armed state
bool armPending = false;       // Waiting for door to close before arming
bool isAlarmTriggered = false; 
bool isHushed = false;         // Silences buzzer until door closes
bool isTestingAlarm = false;
bool isDoorOpen = false;

// --- One-Time Open Feature Variables ---
bool oneTimeOpenActive = false;
bool oneTimeOpenUsed = false;
unsigned long oneTimeOpenTimer = 0;
const unsigned long ONE_TIME_TIMEOUT = 15000; // 15 seconds

// --- Volume Sliders (PWM Duty Cycle: 0 to 255) ---
int alarmVolume = 128; 
int chimeVolume = 128; // Reserved for future chime logic

// --- WiFi Reconnect Variables ---
unsigned long previousWifiMillis = 0;
const long wifiInterval = 10000;

// --- LED Pattern Enums ---
#define LED_OFF 0
#define LED_ON 1
#define AIRPLANE_BLINK 2
#define RAPID_BLINK 3
#define SLOW_BLINK 4

// --- HTML Layout with Material 3 Expressive UI ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Security Core</title>
    <link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@400;500;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --md-primary: #2b6a41;
            --md-on-primary: #ffffff;
            --md-primary-container: #d2e7d6;
            --md-on-primary-container: #0f2013;
            --md-surface: #f7fbf3;
            --md-surface-variant: #e1e9dc;
            --md-on-surface: #191c18;
            --md-outline: #727970;
            --md-error: #ba1a1a;
            --md-error-container: #ffdad6;
        }
        body {
            font-family: 'Google Sans', sans-serif;
            background-color: var(--md-surface);
            color: var(--md-on-surface);
            margin: 0;
            padding: 16px;
            display: flex;
            justify-content: center;
        }
        .container {
            width: 100%;
            max-width: 480px;
            display: flex;
            flex-direction: column;
            gap: 16px;
        }
        .header {
            text-align: center;
            font-weight: 700;
            font-size: 24px;
            color: var(--md-primary);
            margin: 8px 0;
        }
        .info-box {
            background-color: var(--md-surface-variant);
            border-radius: 28px;
            padding: 20px;
            display: flex;
            flex-direction: column;
            gap: 12px;
        }
        .status-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 16px;
            font-weight: 500;
        }
        .badge {
            padding: 6px 16px;
            border-radius: 100px;
            font-weight: 700;
            font-size: 14px;
            text-transform: uppercase;
        }
        .badge-green { background-color: var(--md-primary-container); color: var(--md-on-primary-container); }
        .badge-red { background-color: var(--md-error-container); color: var(--md-error); }
        .badge-neutral { background-color: #e0e0e0; color: #424242; }
        .grid-buttons {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }
        .btn {
            font-family: 'Google Sans', sans-serif;
            font-weight: 500;
            font-size: 15px;
            padding: 14px;
            border: none;
            border-radius: 20px;
            cursor: pointer;
            transition: all 0.2s cubic-bezier(0.2, 0, 0, 1);
            display: flex;
            align-items: center;
            justify-content: center;
            box-shadow: 0 1px 3px rgba(0,0,0,0.1);
        }
        .btn:active { transform: scale(0.94); }
        .btn-primary { background-color: var(--md-primary); color: var(--md-on-primary); }
        .btn-tonal { background-color: var(--md-primary-container); color: var(--md-on-primary-container); }
        .btn-outline { background-color: transparent; border: 1px solid var(--md-outline); color: var(--md-primary); }
        .slider-box {
            background-color: var(--md-surface-variant);
            border-radius: 24px;
            padding: 16px;
            display: flex;
            flex-direction: column;
            gap: 8px;
        }
        .slider-label { font-weight: 500; font-size: 14px; }
        input[type="range"] {
            -webkit-appearance: none;
            width: 100%;
            height: 8px;
            border-radius: 4px;
            background: var(--md-outline);
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: var(--md-primary);
            cursor: pointer;
            transition: transform 0.1s;
        }
        input[type="range"]::-webkit-slider-thumb:active { transform: scale(1.3); }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">System Core Panel</div>
        
        <!-- Info Box Top -->
        <div class="info-box">
            <div class="status-row">
                <span>Door Position</span>
                <span id="doorState" class="badge badge-neutral">Loading...</span>
            </div>
            <div class="status-row">
                <span>System Alarm Status</span>
                <span id="alarmState" class="badge badge-neutral">Loading...</span>
            </div>
            <div class="status-row">
                <span>Arming Context</span>
                <span id="armContext" class="badge badge-neutral">Loading...</span>
            </div>
        </div>

        <!-- Action Control Grid -->
        <div class="grid-buttons">
            <button class="btn btn-primary" onclick="sendAction('arm')">Arm System</button>
            <button class="btn btn-outline" onclick="sendAction('disarm')">Disarm</button>
            <button class="btn btn-tonal" onclick="sendAction('hush')">Hush Alarm</button>
            <button class="btn btn-tonal" onclick="sendAction('onetime')">One-Time Open</button>
        </div>
        <button class="btn btn-outline" style="width:100%" onclick="sendAction('test')">Toggle Test Alarm</button>

        <!-- Dynamic Control Sliders -->
        <div class="slider-box">
            <div class="slider-label">Alarm Sound Volume</div>
            <input type="range" id="alarmVol" min="0" max="255" onchange="sendSlider('vol_alarm', this.value)">
        </div>
        <div class="slider-box">
            <div class="slider-label">Chime Feed Volume</div>
            <input type="range" id="chimeVol" min="0" max="255" onchange="sendSlider('vol_chime', this.value)">
        </div>
    </div>

    <script>
        function updateStatus() {
            fetch('/status')
                .then(res => res.json())
                .then(data => {
                    const door = document.getElementById('doorState');
                    door.innerText = data.door;
                    door.className = 'badge ' + (data.door === 'OPEN' ? 'badge-red' : 'badge-green');

                    const alarm = document.getElementById('alarmState');
                    alarm.innerText = data.alarm;
                    alarm.className = 'badge ' + (data.alarm === 'ALARMING' ? 'badge-red' : 'badge-green');

                    const context = document.getElementById('armContext');
                    context.innerText = data.armState;
                    context.className = 'badge ' + (data.armState === 'ARMED' ? 'badge-green' : (data.armState === 'PENDING' ? 'badge-neutral' : 'badge-neutral'));
                    
                    document.getElementById('alarmVol').value = data.vAlarm;
                    document.getElementById('chimeVol').value = data.vChime;
                });
        }

        function sendAction(cmd) {
            fetch('/action?cmd=' + cmd);
            setTimeout(updateStatus, 150);
        }

        function sendSlider(cmd, val) {
            fetch(`/action?cmd=${cmd}&val=${val}`);
        }

        setInterval(updateStatus, 2000);
        window.onload = updateStatus;
    </script>
</body>
</html>
)rawliteral";

// --- Universal LED Controller Switch ---
void setLedState(int pin, int pattern) {
  if (pattern == LED_OFF) {
    digitalWrite(pin, LOW);
  } 
  else if (pattern == LED_ON) {
    digitalWrite(pin, HIGH);
  } 
  else if (pattern == AIRPLANE_BLINK) {
    digitalWrite(pin, (millis() % 1050) < 50 ? HIGH : LOW);
  } 
  else if (pattern == RAPID_BLINK) {
    digitalWrite(pin, (millis() / 150) % 2 ? HIGH : LOW);
  } 
  else if (pattern == SLOW_BLINK) {
    digitalWrite(pin, (millis() / 500) % 2 ? HIGH : LOW);
  }
}

// --- Web Server Endpoints ---
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String doorStr = isDoorOpen ? "OPEN" : "CLOSED";
  String alarmStr = (isAlarmTriggered && !isHushed) ? "ALARMING" : (isTestingAlarm ? "TESTING" : "OK");
  String armStr = armPending ? "PENDING" : (isArmed ? "ARMED" : "DISARMED");
  
  String json = "{\"door\":\"" + doorStr + "\",\"alarm\":\"" + alarmStr + "\",\"armState\":\"" + armStr + "\",\"vAlarm\":" + String(alarmVolume) + ",\"vChime\":" + String(chimeVolume) + "}";
  server.send(200, "application/json", json);
}

void handleAction() {
  String cmd = server.arg("cmd");
  
  if (cmd == "arm") {
    if (isDoorOpen) {
      armPending = true;
      isArmed = false;
    } else {
      isArmed = true;
      armPending = false;
    }
  } 
  else if (cmd == "disarm") {
    isArmed = false;
    armPending = false;
    isAlarmTriggered = false;
    isHushed = false;
    oneTimeOpenActive = false;
    oneTimeOpenUsed = false;
    isTestingAlarm = false;
  } 
  else if (cmd == "hush") {
    if (isAlarmTriggered) {
      isHushed = true;
    }
  } 
  else if (cmd == "onetime") {
    oneTimeOpenActive = true;
    oneTimeOpenUsed = false;
    oneTimeOpenTimer = millis();
    isArmed = false;
    armPending = false;
  } 
  else if (cmd == "test") {
    isTestingAlarm = !isTestingAlarm;
  } 
  else if (cmd == "vol_alarm") {
    alarmVolume = server.arg("val").toInt();
  } 
  else if (cmd == "vol_chime") {
    chimeVolume = server.arg("val").toInt();
  }
  server.send(200, "text/plain", "OK");
}

void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(ARMED_LED_PIN, OUTPUT);
  pinMode(DOOR_LED_PIN, OUTPUT);
  
  // Set up safe hardware PWM for the IRLZ44N driving the buzzer
  ledcAttach(BUZZER_PIN, 2000, 8); // 2kHz Frequency, 8-bit resolution

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Set up local domain endpoint
  if (MDNS.begin("dooralarm")) {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/action", handleAction);
  server.begin();
}

void loop() {
  unsigned long currentMillis = millis();
  server.handleClient();

  // --- 1. Core Hardware Status ---
  isDoorOpen = (digitalRead(REED_PIN) == HIGH);
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);

  // --- 2. Non-Blocking Connection Guard ---
  if (!wifiConnected && (currentMillis - previousWifiMillis >= wifiInterval)) {
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    previousWifiMillis = currentMillis;
  }

  // --- 3. Smart Automation Logic Matrix ---
  
  // Automatic arming handler once door closes
  if (armPending && !isDoorOpen) {
    isArmed = true;
    armPending = false;
  }

  // Alarm Trigger Event
  if (isArmed && isDoorOpen && !oneTimeOpenActive) {
    isAlarmTriggered = true;
  }

  // Hush Auto-Rearm Reset
  if (isAlarmTriggered && isHushed && !isDoorOpen) {
    isAlarmTriggered = false;
    isHushed = false;
    isArmed = true; 
  }

  // One-Time Open Lifecycle Handler
  if (oneTimeOpenActive) {
    if (!oneTimeOpenUsed && isDoorOpen) {
      oneTimeOpenUsed = true;
    }
    // Scenario A: Door cycled completely -> Close & Auto-rearm
    if (oneTimeOpenUsed && !isDoorOpen) {
      isArmed = true;
      oneTimeOpenActive = false;
      oneTimeOpenUsed = false;
    }
    // Scenario B: Timeout protection -> Auto-rearm back if never opened
    if (!oneTimeOpenUsed && (currentMillis - oneTimeOpenTimer >= ONE_TIME_TIMEOUT)) {
      isArmed = true;
      oneTimeOpenActive = false;
    }
  }

  // --- 4. Buzzer Switching Output ---
  if ((isAlarmTriggered && !isHushed) || isTestingAlarm) {
    ledcWrite(BUZZER_PIN, alarmVolume);
  } else {
    ledcWrite(BUZZER_PIN, 0);
  }

  // --- 5. Door Status LED Driving ---
  if (isDoorOpen) {
    setLedState(DOOR_LED_PIN, AIRPLANE_BLINK);
  } else {
    setLedState(DOOR_LED_PIN, LED_ON);
  }

  // --- 6. Arm/Status LED Priority Ladder ---
  if (!wifiConnected) {
    setLedState(ARMED_LED_PIN, RAPID_BLINK);   // P1: Connection drops override everything
  } 
  else if (isAlarmTriggered) {
    setLedState(ARMED_LED_PIN, SLOW_BLINK);    // P2: Active sounding alarm state
  } 
  else if (isArmed) {
    setLedState(ARMED_LED_PIN, LED_ON);        // P3: System is locked down
  } 
  else {
    setLedState(ARMED_LED_PIN, AIRPLANE_BLINK); // P4: System is safe/disarmed/pending
  }
}
