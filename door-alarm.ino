#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

// --- Static IP Configuration ---
IPAddress local_IP(192, 168, 0, 117);
IPAddress gateway(192, 168, 0, 1);   // Change this if your router IP is different
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);    // Essential for Telegram & NTP
IPAddress secondaryDNS(8, 8, 4, 4);

// --- Customization Configuration ---
const String doorName = "Back Door"; 

// --- Telegram Configuration ---
const String botToken = "YOUR_TELEGRAM_BOT_TOKEN_HERE";
const String chatIdx = "YOUR_TELEGRAM_CHAT_ID_HERE";

WebServer server(80);
Preferences preferences;

// --- Core System Variables (Volatile for Dual-Core Safety) ---
volatile bool isArmed = false;          
volatile bool armPending = false;
volatile bool isAlarmTriggered = false; 
volatile bool isHushed = false;
volatile bool isTestingAlarm = false;
volatile bool isDoorOpen = false;
volatile unsigned long nightDisarmTimer = 0;
volatile bool nightTimerActive = false;

// Standard Variables
bool bootTimeSet = false;
bool lastDoorState = false; 
bool ntpInitialized = false;
bool wasDisconnected = false; 
unsigned long lastTimeCheck = 0;
unsigned long previousWifiMillis = 0;

// --- Audio Variables ---
int alarmVolume = 255; 
int chimeVolume = 128;
int chimeTrigger = 0;  
unsigned long audioTimer = 0;
unsigned long uiBeepTimer = 0;
bool uiBeepActive = false;

// --- LED Constants ---
#define LED_OFF 0
#define LED_ON 1
#define AIRPLANE_BLINK 2
#define RAPID_BLINK 3
#define SLOW_BLINK 4

// --- Dual-Core Inter-Process Communication ---
QueueHandle_t telegramQueue;
long lastUpdateId = 0;
unsigned long lastTelegramPoll = 0;

// --- Minified Material 3 HTML/CSS (Forced Universal Google Sans) ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Door Alarm System Panel</title><link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght=500;700&display=swap" rel="stylesheet"><style>*{font-family:'Google Sans',sans-serif !important} body{background:#f7fbf3;color:#191c18;margin:0;padding:16px;display:flex;justify-content:center} .c{width:100%;max-width:400px;display:flex;flex-direction:column;gap:12px} h2{text-align:center;color:#2b6a41;margin:5px} .box{background:#e1e9dc;border-radius:20px;padding:16px;display:flex;flex-direction:column;gap:10px} .row{display:flex;justify-content:space-between;align-items:center;font-weight:500} .bdg{padding:4px 12px;border-radius:20px;font-weight:700;font-size:13px} .bg{background:#d2e7d6;color:#0f2013} .br{background:#ffdad6;color:#ba1a1a} .bn{background:#ccc;color:#333} .g{display:grid;grid-template-columns:1fr 1fr;gap:10px} button{font-weight:500;font-size:15px;padding:14px;border:none;border-radius:16px;cursor:pointer;transition:transform .1s} button:active{transform:scale(.94)} .b1{background:#2b6a41;color:#fff} .b2{background:transparent;border:1px solid #727970;color:#2b6a41} .b3{background:#d2e7d6;color:#0f2013} .f{grid-column:span 2} input[type=range]{-webkit-appearance:none;width:100%;height:6px;border-radius:3px;background:#727970} input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#2b6a41}</style></head><body>
<div class="c"><h2>Alarm System Panel</h2><div class="box"><div class="row"><span>Door</span><span id="d" class="bdg bn">--</span></div><div class="row"><span>Alarm</span><span id="a" class="bdg bn">--</span></div><div class="row"><span>Status</span><span id="s" class="bdg bn">--</span></div><div class="row"><span>System Time</span><span id="t" style="font-size:14px;color:#434940">--:--:--</span></div><div class="row"><span>Network</span><span id="w" class="bdg bn">--</span></div></div>
<div class="g"><button class="b1" onclick="S('arm')">Arm</button><button class="b2" onclick="S('disarm')">Disarm</button><button class="b3 f" onclick="S('hush')">Hush Alarm</button></div><button class="b2" onclick="S('test')">Test Alarm</button>
<div class="box"><span>Alarm Vol</span><input type="range" id="vA" min="0" max="255" onchange="V('vol_alarm',this.value)"><span>Chime Vol</span><input type="range" id="vC" min="0" max="255" onchange="V('vol_chime',this.value)"></div></div>
<script>function U(){fetch('/st').then(r=>r.json()).then(d=>{let E=(i,t,c)=>{let e=document.getElementById(i);e.innerText=t;e.className='bdg '+c;};E('d',d.d,d.d==='OPEN'?'br':'bg');E('a',d.a,d.a==='ALARM'?'br':'bg');E('s',d.s,d.s==='ARMED'?'bg':(d.s==='PENDING'?'br':'bn'));E('w',d.w,d.w==='ONLINE'?'bg':'br');document.getElementById('t').innerText=d.t;document.getElementById('vA').value=d.va;document.getElementById('vC').value=d.vc;}).catch(e=>console.error("Data Sync Error",e))} function S(c){fetch('/ac?cmd='+c);setTimeout(U,100)} function V(c,v){fetch(`/ac?cmd=${c}&val=${v}`)} setInterval(U,500);window.onload=U;</script></body></html>
)rawliteral";

// --- Hardware Helpers ---
void setLedState(int pin, int pattern) {
  if (pattern == LED_OFF) digitalWrite(pin, LOW);
  else if (pattern == LED_ON) digitalWrite(pin, HIGH);
  else if (pattern == AIRPLANE_BLINK) digitalWrite(pin, (millis() % 1050) < 50 ? HIGH : LOW);
  else if (pattern == RAPID_BLINK) digitalWrite(pin, (millis() / 150) % 2 ? HIGH : LOW);
  else if (pattern == SLOW_BLINK) digitalWrite(pin, (millis() / 500) % 2 ? HIGH : LOW);
}

void setBuzzerVolume(int vol) { ledcWrite(BUZZER_PIN, vol); }
void triggerUiFeedback() { uiBeepTimer = millis(); uiBeepActive = true; }

// --- Non-Blocking Telegram Transmitter (Pushes to RTOS Queue) ---
void sendAlert(String action) {
  if (botToken.indexOf("YOUR_") == 0) return;
  String fullMessage = doorName + ": " + action;
  char msgBuffer[256];
  strncpy(msgBuffer, fullMessage.c_str(), sizeof(msgBuffer) - 1);
  msgBuffer[sizeof(msgBuffer) - 1] = '\0';
  xQueueSend(telegramQueue, &msgBuffer, 0); 
}

// ====================================================================
// CORE 0 TASK: Dedicated Networking Thread 
// ====================================================================
void telegramNetworkTask(void * parameter) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  for(;;) {
    if (WiFi.status() == WL_CONNECTED && botToken.indexOf("YOUR_") != 0) {
      
      // 1. Process Outgoing Messages from the Queue
      char txBuffer[256];
      if (xQueueReceive(telegramQueue, &txBuffer, 0) == pdTRUE) {
        String url = "https://api.telegram.org/bot" + botToken + "/sendMessage";
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        String payload = "{\"chat_id\":\"" + chatIdx + "\",\"text\":\"" + String(txBuffer) + "\"}";
        http.POST(payload);
        http.end();
      }

      // 2. Poll for Incoming Commands (Every 3 seconds)
      unsigned long currentTaskTime = millis();
      if (currentTaskTime - lastTelegramPoll >= 3000) {
        String url = "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=3&timeout=0";
        http.begin(client, url);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          
          int updateIdx = payload.lastIndexOf("\"update_id\":");
          if (updateIdx != -1) {
            int endIdx = payload.indexOf(",", updateIdx);
            if (endIdx != -1) lastUpdateId = payload.substring(updateIdx + 12, endIdx).toInt();
          }

          if (payload.indexOf("\"chat\":{\"id\":" + chatIdx) != -1 || payload.indexOf("\"from\":{\"id\":" + chatIdx) != -1) {
            if (payload.indexOf("\"text\":\"/status\"") != -1) {
              String doorStr = isDoorOpen ? "OPEN" : "CLOSED";
              String alarmStr = (isAlarmTriggered && !isHushed) ? "ALARM" : (isTestingAlarm ? "ALARM" : "OK");
              String armStr = armPending ? "PENDING" : (isArmed ? "ARMED" : "DISARMED");
              String wifiStr = (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE";
              struct tm timeinfo;
              char timeBuf[12] = "--:--:--";
              if (ntpInitialized && getLocalTime(&timeinfo, 0)) sprintf(timeBuf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
              
              String statusReport = "Status Report\\nDoor: " + doorStr + "\\nAlarm: " + alarmStr + "\\nStatus: " + armStr + "\\nNetwork: " + wifiStr + "\\nTime: " + String(timeBuf);
              sendAlert(statusReport);
            }
            else if (payload.indexOf("\"text\":\"/arm\"") != -1) {
              if (isDoorOpen) { armPending = true; isArmed = false; sendAlert("Arming Pending"); } 
              else { isArmed = true; armPending = false; sendAlert("System Armed Manually"); }
            }
            else if (payload.indexOf("\"text\":\"/disarm\"") != -1) {
              isArmed = false; armPending = false; isAlarmTriggered = false; isHushed = false; isTestingAlarm = false;
              nightDisarmTimer = millis(); nightTimerActive = true; 
              sendAlert("System Disarmed Manually");
            }
            else if (payload.indexOf("\"text\":\"/hush\"") != -1) {
              isTestingAlarm = false; 
              if (isAlarmTriggered) { isHushed = true; sendAlert("Siren Hushed"); }
              if (!isDoorOpen) { isAlarmTriggered = false; isHushed = false; isArmed = true; }
            }
            else if (payload.indexOf("\"text\":\"/testalarmnow\"") != -1) {
              isTestingAlarm = !isTestingAlarm;
              if (isTestingAlarm) sendAlert("Local Siren Testing Triggered");
              else { isHushed = false; sendAlert("Local Siren Testing Terminated"); }
            }
            else if (payload.indexOf("\"text\":\"/rebootnow\"") != -1) {
              sendAlert("Rebooting System Now");
              delay(300); ESP.restart();
            }
          }
        }
        http.end();
        lastTelegramPoll = currentTaskTime;
      }
    }
    vTaskDelay(20 / portTICK_PERIOD_MS); 
  }
}
// ====================================================================

// --- Web Endpoints ---
void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }
void handleStatus() {
  String doorStr = isDoorOpen ? "OPEN" : "CLOSED";
  String alarmStr = (isAlarmTriggered && !isHushed) ? "ALARM" : (isTestingAlarm ? "ALARM" : "OK");
  String armStr = armPending ? "PENDING" : (isArmed ? "ARMED" : "DISARMED");
  String wifiStr = (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE";
  
  struct tm timeinfo;
  char timeBuf[12] = "--:--:--";
  if (ntpInitialized && getLocalTime(&timeinfo, 0)) {
    sprintf(timeBuf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }

  String json = "{\"d\":\"" + doorStr + "\",\"a\":\"" + alarmStr + "\",\"s\":\"" + armStr + "\",\"w\":\"" + wifiStr + "\",\"t\":\"" + String(timeBuf) + "\",\"va\":" + String(alarmVolume) + ",\"vc\":" + String(chimeVolume) + "}";
  server.send(200, "application/json", json);
}

void handleAction() {
  String cmd = server.arg("cmd");
  triggerUiFeedback(); 

  if (cmd == "arm") {
    if (isDoorOpen) { armPending = true; isArmed = false; sendAlert("Arming Pending"); } 
    else { isArmed = true; armPending = false; sendAlert("System Armed Manually"); }
  } 
  else if (cmd == "disarm") {
    isArmed = false; armPending = false; isAlarmTriggered = false; isHushed = false; isTestingAlarm = false;
    nightDisarmTimer = millis(); nightTimerActive = true; sendAlert("System Disarmed Manually");
  } 
  else if (cmd == "hush") {
    isTestingAlarm = false; 
    if (isAlarmTriggered) { isHushed = true; sendAlert("Siren Hushed"); }
    if (!isDoorOpen) { isAlarmTriggered = false; isHushed = false; isArmed = true; }
  } 
  else if (cmd == "test") {
    isTestingAlarm = !isTestingAlarm;
    if (isTestingAlarm) sendAlert("Local Siren Testing Triggered");
    else { isHushed = false; sendAlert("Local Siren Testing Terminated"); }
  } 
  else if (cmd == "vol_alarm") { alarmVolume = server.arg("val").toInt(); preferences.putInt("vol_alarm", alarmVolume); } 
  else if (cmd == "vol_chime") { chimeVolume = server.arg("val").toInt(); preferences.putInt("vol_chime", chimeVolume); }
  server.send(200, "text/plain", "OK");
}

void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(ARMED_LED_PIN, OUTPUT);
  pinMode(DOOR_LED_PIN, OUTPUT);
  
  isDoorOpen = (digitalRead(REED_PIN) == HIGH);
  lastDoorState = isDoorOpen;

  ledcAttach(BUZZER_PIN, 2400, 8); 

  preferences.begin("security", false);
  alarmVolume = preferences.getInt("vol_alarm", 255); 
  chimeVolume = preferences.getInt("vol_chime", 128);

  telegramQueue = xQueueCreate(15, 256);
  xTaskCreatePinnedToCore(telegramNetworkTask, "TelegramTask", 16384, NULL, 1, NULL, 0);

  // --- Apply Static IP Configuration ---
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); 
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);

  // Kept mDNS just in case, but you should navigate directly to 192.168.0.117
  if (MDNS.begin("dooralarm")) MDNS.addService("http", "tcp", 80);

  server.on("/", handleRoot);
  server.on("/st", handleStatus);
  server.on("/ac", handleAction);
  server.begin();
}

// ====================================================================
// CORE 1 TASK: Ultra-Fast Hardware Loop 
// ====================================================================
void loop() {
  unsigned long now = millis();
  server.handleClient(); 

  // 1. Connection Monitoring with Static IP Re-apply
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected) {
    wasDisconnected = true; 
    if (now - previousWifiMillis >= 4000) { 
      WiFi.disconnect(); 
      WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
      WiFi.begin(ssid, password);
      previousWifiMillis = now;
    }
  } else if (wasDisconnected) {
    sendAlert("Connection Restored.");
    wasDisconnected = false;
  }

  if (wifiConnected && !ntpInitialized) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ntpInitialized = true;
  }

  // 2. Schedule Evaluation Engine (11:30 PM to 5:30 AM)
  if (ntpInitialized && (now - lastTimeCheck > 5000)) { 
    lastTimeCheck = now;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) { 
      bool isNight = (timeinfo.tm_hour < 5) || (timeinfo.tm_hour == 5 && timeinfo.tm_min < 30) || (timeinfo.tm_hour == 23 && timeinfo.tm_min >= 30);

      if (!bootTimeSet) { 
        if (isNight) { if (isDoorOpen) armPending = true; else isArmed = true; sendAlert("Night Mode Active: System Scheduled Auto-Arm Enabled"); }
        bootTimeSet = true; 
      }
      if (isNight && !isArmed && !armPending && nightTimerActive) {
        if (now - nightDisarmTimer >= 3600000) { 
          if (isDoorOpen) armPending = true; else isArmed = true;
          nightTimerActive = false; sendAlert("Night Mode Refresher: 1-Hour Disarm Bypass Concluded. Re-armed.");
        }
      } else if (!isNight) nightTimerActive = false; 
    }
  }

  // 3. Hardware Edge Detection Matrix
  bool currentDoorState = (digitalRead(REED_PIN) == HIGH);
  if (currentDoorState != lastDoorState) {
    isDoorOpen = currentDoorState;
    
    if (!isArmed && !isAlarmTriggered && !isTestingAlarm) {
      chimeTrigger = isDoorOpen ? 1 : 2; 
      audioTimer = now;
      if (isDoorOpen) sendAlert("Door Opened"); else sendAlert("Door Closed");
    }

    if (!isDoorOpen) {
      if (armPending) { isArmed = true; armPending = false; sendAlert("Status Update: Door Secured. System Completely Armed."); }
      if (isAlarmTriggered && isHushed) { isAlarmTriggered = false; isHushed = false; isArmed = true; sendAlert("Status Update: Door Secured post-hush. System Re-armed."); }
    }

    if (isDoorOpen && (isArmed || armPending)) { 
      isAlarmTriggered = true; isHushed = false; isArmed = false; armPending = false; 
      sendAlert("ALERT: Alarm Activated! Security zone breached.");
    }
    lastDoorState = currentDoorState;
  }

  // 4. Audio Processing Engine 
  if (isAlarmTriggered && isHushed) { setBuzzerVolume(0); } 
  else if ((isAlarmTriggered && !isHushed) || isTestingAlarm) {
    if ((now % 200) < 150) setBuzzerVolume(alarmVolume); else setBuzzerVolume(0);
    chimeTrigger = 0; uiBeepActive = false; 
  } 
  else if (uiBeepActive) {
    if (now - uiBeepTimer < 60) setBuzzerVolume(chimeVolume);
    else { setBuzzerVolume(0); uiBeepActive = false; }
  }
  else if (chimeTrigger > 0) {
    unsigned long elapsed = now - audioTimer;
    if (chimeTrigger == 1) { 
      if (elapsed < 120) setBuzzerVolume(chimeVolume); else if (elapsed < 160) setBuzzerVolume(0); else if (elapsed < 280) setBuzzerVolume(chimeVolume / 2); else { setBuzzerVolume(0); chimeTrigger = 0; }
    } else if (chimeTrigger == 2) { 
      if (elapsed < 120) setBuzzerVolume(chimeVolume); else if (elapsed < 180) setBuzzerVolume(0); else if (elapsed < 300) setBuzzerVolume(chimeVolume); else { setBuzzerVolume(0); chimeTrigger = 0; }
    }
  } else setBuzzerVolume(0); 

  // 5. Flawless LED Driving Modules
  setLedState(DOOR_LED_PIN, isDoorOpen ? AIRPLANE_BLINK : LED_ON);
  if (!wifiConnected) setLedState(ARMED_LED_PIN, RAPID_BLINK);
  else if (isAlarmTriggered) setLedState(ARMED_LED_PIN, SLOW_BLINK);
  else if (isArmed) setLedState(ARMED_LED_PIN, LED_ON);
  else setLedState(ARMED_LED_PIN, AIRPLANE_BLINK);
}
