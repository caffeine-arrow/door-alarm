#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "time.h"

// --- Hardware Pins ---
const int REED_PIN = 13;
const int BUZZER_PIN = 25;
const int ARMED_LED_PIN = 26;
const int DOOR_LED_PIN = 27;

// --- WiFi & Time Credentials ---
const char* ssid = "IoT-Net";
const char* password = "IoT@26/P12";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; // +7 Timezone
const int   daylightOffset_sec = 0;

WebServer server(80);

// --- Core System Variables ---
bool isArmed = false;          
bool bootTimeSet = false;
bool armPending = false;
bool isAlarmTriggered = false; 
bool isHushed = false;
bool isTestingAlarm = false;
bool isDoorOpen = false;

// --- Night Mode Variables ---
unsigned long nightDisarmTimer = 0;
bool nightTimerActive = false;
unsigned long lastTimeCheck = 0;

// --- One-Time Open Variables ---
bool oneTimeOpenActive = false;
bool oneTimeOpenUsed = false;
unsigned long oneTimeTimer = 0;

// --- Audio & PWM Variables ---
int alarmVolume = 128; 
int chimeVolume = 128;
int currentPwmFreq = 0;

// Audio State Machines
int audioState = 0;
int chimeTrigger = 0; // 0=Off, 1=Open, 2=Close
unsigned long audioTimer = 0;
unsigned long audioInterval = 0;

// UI Beep Variables
bool triggerUiBeep = false;
unsigned long uiBeepTimer = 0;

// --- WiFi Reconnect ---
unsigned long previousWifiMillis = 0;

// --- LED Constants ---
#define LED_OFF 0
#define LED_ON 1
#define AIRPLANE_BLINK 2
#define RAPID_BLINK 3
#define SLOW_BLINK 4

// --- Minified HTML (Refresh rate changed to 500ms) ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Security Core</title><link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@500;700&display=swap" rel="stylesheet"><style>body{font-family:'Google Sans',sans-serif;background:#f7fbf3;color:#191c18;margin:0;padding:16px;display:flex;justify-content:center} .c{width:100%;max-width:400px;display:flex;flex-direction:column;gap:12px} h2{text-align:center;color:#2b6a41;margin:5px} .box{background:#e1e9dc;border-radius:20px;padding:16px;display:flex;flex-direction:column;gap:10px} .row{display:flex;justify-content:space-between;align-items:center;font-weight:500} .bdg{padding:4px 12px;border-radius:20px;font-weight:700;font-size:13px} .bg{background:#d2e7d6;color:#0f2013} .br{background:#ffdad6;color:#ba1a1a} .bn{background:#ccc;color:#333} .g{display:grid;grid-template-columns:1fr 1fr;gap:10px} button{font-family:'Google Sans';font-weight:500;font-size:15px;padding:14px;border:none;border-radius:16px;cursor:pointer;transition:transform .1s} button:active{transform:scale(.94)} .b1{background:#2b6a41;color:#fff} .b2{background:transparent;border:1px solid #727970;color:#2b6a41} .b3{background:#d2e7d6;color:#0f2013} input[type=range]{-webkit-appearance:none;width:100%;height:6px;border-radius:3px;background:#727970} input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#2b6a41}</style></head><body>
<div class="c"><h2>System Core</h2><div class="box"><div class="row"><span>Door</span><span id="d" class="bdg bn">--</span></div><div class="row"><span>Alarm</span><span id="a" class="bdg bn">--</span></div><div class="row"><span>Status</span><span id="s" class="bdg bn">--</span></div></div>
<div class="g"><button class="b1" onclick="S('arm')">Arm</button><button class="b2" onclick="S('disarm')">Disarm</button><button class="b3" onclick="S('hush')">Hush</button><button class="b3" onclick="S('onetime')">One-Time</button></div>
<button class="b2" onclick="S('test')">Test Alarm</button>
<div class="box"><span>Alarm Vol</span><input type="range" id="vA" min="0" max="255" onchange="V('vol_alarm',this.value)"><span>Chime Vol</span><input type="range" id="vC" min="0" max="255" onchange="V('vol_chime',this.value)"></div></div>
<script>function U(){fetch('/st').then(r=>r.json()).then(d=>{let E=(i,t,c)=>{let e=document.getElementById(i);e.innerText=t;e.className='bdg '+c;};E('d',d.d,d.d==='OPEN'?'br':'bg');E('a',d.a,d.a==='ALARM'?'br':'bg');E('s',d.s,d.s==='ARMED'?'bg':'bn');document.getElementById('vA').value=d.va;document.getElementById('vC').value=d.vc;})} function S(c){fetch('/ac?cmd='+c);setTimeout(U,100)} function V(c,v){fetch(`/ac?cmd=${c}&val=${v}`)} setInterval(U,500);window.onload=U;</script></body></html>
)rawliteral";

// --- Helpers ---
void setLedState(int pin, int pattern) {
  if (pattern == LED_OFF) digitalWrite(pin, LOW);
  else if (pattern == LED_ON) digitalWrite(pin, HIGH);
  else if (pattern == AIRPLANE_BLINK) digitalWrite(pin, (millis() % 1050) < 50 ? HIGH : LOW);
  else if (pattern == RAPID_BLINK) digitalWrite(pin, (millis() / 150) % 2 ? HIGH : LOW);
  else if (pattern == SLOW_BLINK) digitalWrite(pin, (millis() / 500) % 2 ? HIGH : LOW);
}

void setBuzzer(int freq, int vol) {
  if (freq != currentPwmFreq && vol > 0) {
    ledcAttach(BUZZER_PIN, freq, 8);
    currentPwmFreq = freq;
  }
  ledcWrite(BUZZER_PIN, vol);
}

// --- Web Endpoints ---
void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleStatus() {
  String json = "{\"d\":\"" + String(isDoorOpen ? "OPEN" : "CLOSED") + 
                "\",\"a\":\"" + String((isAlarmTriggered && !isHushed) ? "ALARM" : (isTestingAlarm ? "TEST" : "OK")) + 
                "\",\"s\":\"" + String(armPending ? "PENDING" : (isArmed ? "ARMED" : "DISARMED")) + 
                "\",\"va\":" + String(alarmVolume) + ",\"vc\":" + String(chimeVolume) + "}";
  server.send(200, "application/json", json);
}

void handleAction() {
  String cmd = server.arg("cmd");
  
  // Trigger UI Beep for every action
  triggerUiBeep = true;
  uiBeepTimer = millis();

  if (cmd == "arm") {
    if (isDoorOpen) { armPending = true; isArmed = false; } 
    else { isArmed = true; armPending = false; }
    oneTimeOpenActive = false;
    isAlarmTriggered = false;
    isHushed = false;
    isTestingAlarm = false;
  } 
  else if (cmd == "disarm") {
    isArmed = false; armPending = false; isAlarmTriggered = false; isHushed = false;
    oneTimeOpenActive = false; isTestingAlarm = false;
    nightDisarmTimer = millis(); nightTimerActive = true; 
  } 
  else if (cmd == "hush") {
    isTestingAlarm = false; // Instantly kill test mode
    isHushed = true;        // Instantly kill alarm sound
    
    if (!isDoorOpen) {      // If door is closed, rearm right away
      isArmed = true;
      isAlarmTriggered = false;
      isHushed = false;
      armPending = false;
    }
  } 
  else if (cmd == "onetime") {
    isArmed = false; armPending = false; isAlarmTriggered = false; isHushed = false; isTestingAlarm = false;
    oneTimeOpenActive = true; oneTimeOpenUsed = false; oneTimeTimer = millis(); // Start strict 15s window
  } 
  else if (cmd == "test") {
    isTestingAlarm = !isTestingAlarm;
    if (isTestingAlarm) { isArmed = false; isAlarmTriggered = false; isHushed = false; } // Clear conflicts
  }
  else if (cmd == "vol_alarm") alarmVolume = server.arg("val").toInt();
  else if (cmd == "vol_chime") chimeVolume = server.arg("val").toInt();
  
  server.send(200, "text/plain", "OK");
}

void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(ARMED_LED_PIN, OUTPUT);
  pinMode(DOOR_LED_PIN, OUTPUT);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  if (MDNS.begin("dooralarm")) MDNS.addService("http", "tcp", 80);

  server.on("/", handleRoot);
  server.on("/st", handleStatus);
  server.on("/ac", handleAction);
  server.begin();
}

void loop() {
  unsigned long now = millis();
  server.handleClient();

  // 1. Connection Manager
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected && (now - previousWifiMillis >= 10000)) {
    WiFi.disconnect(); WiFi.begin(ssid, password);
    previousWifiMillis = now;
  }

  // 2. NTP & Time Based Logic
  if (now - lastTimeCheck > 10000) {
    lastTimeCheck = now;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) { 
      bool isNight = (timeinfo.tm_hour >= 0 && timeinfo.tm_hour < 4);
      
      if (!bootTimeSet) {
        isArmed = isNight; 
        bootTimeSet = true;
      }

      if (isNight && !isArmed && nightTimerActive) {
        if (now - nightDisarmTimer >= 3600000) { // 1 Hour Auto-Rearm
          isArmed = true; nightTimerActive = false;
        }
      } else if (!isNight) {
        nightTimerActive = false;
      }
    }
  }

  // 3. Physical Door Logic
  bool currentDoor = (digitalRead(REED_PIN) == HIGH);
  if (currentDoor != isDoorOpen) {
    isDoorOpen = currentDoor;
    
    // Arm Pending logic -> Solid Arm when closed
    if (armPending && !isDoorOpen) {
      isArmed = true; armPending = false;
    }

    // Trigger Alarm
    if (isArmed && isDoorOpen && !oneTimeOpenActive) {
      isAlarmTriggered = true; isHushed = false;
    }

    // Hush Waiting Logic -> Rearm when closed
    if (isHushed && !isDoorOpen) {
      isAlarmTriggered = false; isHushed = false; isArmed = true;
    }

    // Disarmed Door Chime Logic
    if (!isArmed && !isAlarmTriggered && !oneTimeOpenActive && !isTestingAlarm) {
      chimeTrigger = isDoorOpen ? 1 : 2; 
      audioState = 1; audioTimer = now; audioInterval = 0;
    }
  }

  // 4. Strict One-Time Open Engine
  if (oneTimeOpenActive) {
    if (isDoorOpen) {
      oneTimeOpenUsed = true; // Mark that the guest opened the door
    }
    
    // Re-arm immediately upon guest closing the door
    if (oneTimeOpenUsed && !isDoorOpen) {
      isArmed = true; 
      oneTimeOpenActive = false;
    }
    
    // Fallback: If 15 seconds pass, abort window and re-arm
    if (now - oneTimeTimer >= 15000) {
      isArmed = true; 
      oneTimeOpenActive = false;
    }
  }

  // 5. Audio Hierarchy (Strict Non-Blocking)
  
  // Priority 1: Main Alarm Sound (150ms ON / 50ms OFF = 200ms cycle)
  if ((isAlarmTriggered && !isHushed) || isTestingAlarm) {
    if ((now % 200) < 150) {
      setBuzzer(1000, alarmVolume); // 1000Hz Tone
    } else {
      setBuzzer(0, 0); // 50ms Silence
    }
    triggerUiBeep = false; // Cancel lower priority audio
    chimeTrigger = 0;
  } 
  // Priority 2: Web UI Action Beep (100ms short beep)
  else if (triggerUiBeep) {
    if (now - uiBeepTimer < 100) {
      setBuzzer(1500, chimeVolume);
    } else {
      setBuzzer(0, 0);
      triggerUiBeep = false;
    }
  }
  // Priority 3: Door Open/Close Chimes
  else if (chimeTrigger > 0) {
    if (now - audioTimer >= audioInterval) {
      audioState++;
      audioTimer = now;
      
      if (chimeTrigger == 1) { // OPEN: Low-High
        if (audioState == 2) { setBuzzer(400, chimeVolume); audioInterval = 100; }
        else if (audioState == 3) { setBuzzer(0, 0); audioInterval = 50; }
        else if (audioState == 4) { setBuzzer(800, chimeVolume); audioInterval = 150; }
        else { setBuzzer(0, 0); chimeTrigger = 0; }
      } 
      else if (chimeTrigger == 2) { // CLOSE: High-High
        if (audioState == 2) { setBuzzer(800, chimeVolume); audioInterval = 100; }
        else if (audioState == 3) { setBuzzer(0, 0); audioInterval = 50; }
        else if (audioState == 4) { setBuzzer(800, chimeVolume); audioInterval = 150; }
        else { setBuzzer(0, 0); chimeTrigger = 0; }
      }
    }
  } 
  // Silence if nothing is happening
  else {
    setBuzzer(0, 0); 
  }

  // 6. LED Drivers
  setLedState(DOOR_LED_PIN, isDoorOpen ? AIRPLANE_BLINK : LED_ON);

  if (!wifiConnected) setLedState(ARMED_LED_PIN, RAPID_BLINK);
  else if (isAlarmTriggered) setLedState(ARMED_LED_PIN, SLOW_BLINK);
  else if (isArmed) setLedState(ARMED_LED_PIN, LED_ON);
  else setLedState(ARMED_LED_PIN, AIRPLANE_BLINK);
}
