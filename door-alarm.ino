#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
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

// --- Telegram Configuration (Top Level Access) ---
const char* BOT_TOKEN = "YOUR_BOT_TOKEN_HERE";
const String CHAT_ID = "YOUR_CHAT_ID_HERE";
const String BOT_USERNAME = "YourBotUsername"; 
const String TG_PREFIX = "Back Door: ";

// --- Telegram Custom Messages (Title Case) ---
const String MSG_ARMED = "Alarm Armed.";
const String MSG_DISARMED = "Alarm Disarmed.";
const String MSG_HUSHED = "Alarm Hushed.";
const String MSG_REBOOTING = "Rebooting System.";
const String MSG_TESTING = "Local Alarm Testing Initiated.";
const String MSG_DOOR_OPEN = "Door Opened.";
const String MSG_DOOR_CLOSED = "Door Closed.";
const String MSG_ALARM_ACTIVATED = "Alarm Activated.";
const String MSG_CONNECTED = "Connection Restored.";
const String MSG_STATUS_TITLE = "Status Report";

// --- Schedule Configuration (Top Level Access) ---
const int scheduleStartHour = 21;
const int scheduleStartMinute = 30;
const int scheduleEndHour = 4;

WebServer server(80);
Preferences preferences;
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// --- FreeRTOS Architecture Variables ---
QueueHandle_t tgQueue;

// --- Core System Variables ---
volatile bool isArmed = false;          
bool bootTimeSet = false;
volatile bool armPending = false;
volatile bool isAlarmTriggered = false; 
volatile bool isHushed = false;
volatile bool isTestingAlarm = false;
volatile bool isDoorOpen = false;
bool lastDoorState = false; 
bool ntpInitialized = false;
volatile bool shouldReboot = false; 

// --- Schedule Variables ---
unsigned long nightDisarmTimer = 0;
bool nightTimerActive = false;
unsigned long lastTimeCheck = 0;

// --- Audio Variables ---
int alarmVolume = 255; 
int chimeVolume = 128;
int chimeTrigger = 0;  
unsigned long audioTimer = 0;

// --- UI Direct Feedback Beep ---
unsigned long uiBeepTimer = 0;
bool uiBeepActive = false;

// --- LED Constants ---
#define LED_OFF 0
#define LED_ON 1
#define AIRPLANE_BLINK 2
#define RAPID_BLINK 3
#define SLOW_BLINK 4

// --- Minified Material 3 HTML/CSS (Forced Universal Google Sans) ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Door Alarm System Panel</title><link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght=500;700&display=swap" rel="stylesheet"><style>*{font-family:'Google Sans',sans-serif !important} body{background:#f7fbf3;color:#191c18;margin:0;padding:16px;display:flex;justify-content:center} .c{width:100%;max-width:400px;display:flex;flex-direction:column;gap:12px} h2{text-align:center;color:#2b6a41;margin:5px} .box{background:#e1e9dc;border-radius:20px;padding:16px;display:flex;flex-direction:column;gap:10px} .row{display:flex;justify-content:space-between;align-items:center;font-weight:500} .bdg{padding:4px 12px;border-radius:20px;font-weight:700;font-size:13px} .bg{background:#d2e7d6;color:#0f2013} .br{background:#ffdad6;color:#ba1a1a} .bn{background:#ccc;color:#333} .g{display:grid;grid-template-columns:1fr 1fr;gap:10px} button{font-weight:500;font-size:15px;padding:14px;border:none;border-radius:16px;cursor:pointer;transition:transform .1s} button:active{transform:scale(.94)} .b1{background:#2b6a41;color:#fff} .b2{background:transparent;border:1px solid #727970;color:#2b6a41} .b3{background:#d2e7d6;color:#0f2013} .f{grid-column:span 2} input[type=range]{-webkit-appearance:none;width:100%;height:6px;border-radius:3px;background:#727970} input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#2b6a41}</style></head><body>
<div class="c"><h2>Alarm System Panel</h2><div class="box"><div class="row"><span>Door</span><span id="d" class="bdg bn">--</span></div><div class="row"><span>Alarm</span><span id="a" class="bdg bn">--</span></div><div class="row"><span>Status</span><span id="s" class="bdg bn">--</span></div><div class="row"><span>System Time</span><span id="t" style="font-size:14px;color:#434940">--:--:--</span></div><div class="row"><span>Network</span><span id="w" class="bdg bn">--</span></div></div>
<div class="g"><button class="b1" onclick="S('arm')">Arm</button><button class="b2" onclick="S('disarm')">Disarm</button><button class="b3 f" onclick="S('hush')">Hush Alarm</button></div><button class="b2" onclick="S('test')">Test Alarm</button>
<div class="box"><span>Alarm Vol</span><input type="range" id="vA" min="0" max="255" onchange="V('vol_alarm',this.value)"><span>Chime Vol</span><input type="range" id="vC" min="0" max="255" onchange="V('vol_chime',this.value)"></div></div>
<script>function U(){fetch('/st').then(r=>r.json()).then(d=>{let E=(i,t,c)=>{let e=document.getElementById(i);e.innerText=t;e.className='bdg '+c;};E('d',d.d,d.d==='OPEN'?'br':'bg');E('a',d.a,d.a==='ALARM'?'br':'bg');E('s',d.s,d.s==='ARMED'?'bg':(d.s==='PENDING'?'br':'bn'));E('w',d.w,d.w==='ONLINE'?'bg':'br');document.getElementById('t').innerText=d.t;document.getElementById('vA').value=d.va;document.getElementById('vC').value=d.vc;}).catch(e=>console.error("Data Sync Error",e))} function S(c){fetch('/ac?cmd='+c);setTimeout(U,100)} function V(c,v){fetch(`/ac?cmd=${c}&val=${v}`)} setInterval(U,500);window.onload=U;</script></body></html>
)rawliteral";

// --- Helpers ---
void setLedState(int pin, int pattern) {
  if (pattern == LED_OFF) digitalWrite(pin, LOW);
  else if (pattern == LED_ON) digitalWrite(pin, HIGH);
  else if (pattern == AIRPLANE_BLINK) digitalWrite(pin, (millis() % 1050) < 50 ? HIGH : LOW);
  else if (pattern == RAPID_BLINK) digitalWrite(pin, (millis() / 150) % 2 ? HIGH : LOW);
  else if (pattern == SLOW_BLINK) digitalWrite(pin, (millis() / 500) % 2 ? HIGH : LOW);
}

void setBuzzerVolume(int vol) {
  ledcWrite(BUZZER_PIN, vol);
}

void triggerUiFeedback() {
  uiBeepTimer = millis();
  uiBeepActive = true;
}

// --- FreeRTOS Queue Helper ---
void enqueueTgMsg(const String& msg) {
  if (tgQueue != NULL) {
    char buffer[64];
    strncpy(buffer, msg.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    xQueueSend(tgQueue, &buffer, (TickType_t)0);
  }
}

// --- Telegram Command Processor (Runs on Core 0) ---
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    if (String(bot.messages[i].chat_id) != CHAT_ID) continue; 

    String text = bot.messages[i].text;
    String mention = "@" + BOT_USERNAME;

    if (text == "/arm" || text == "/arm" + mention) {
      if (isDoorOpen) { armPending = true; isArmed = false; } 
      else { isArmed = true; armPending = false; }
      bot.sendMessage(CHAT_ID, TG_PREFIX + MSG_ARMED, "");
      triggerUiFeedback();
    } 
    else if (text == "/disarm" || text == "/disarm" + mention || text == "/disarmed" || text == "/disarmed" + mention) {
      isArmed = false; armPending = false; isAlarmTriggered = false; isHushed = false; isTestingAlarm = false;
      nightDisarmTimer = millis(); nightTimerActive = true; 
      bot.sendMessage(CHAT_ID, TG_PREFIX + MSG_DISARMED, "");
      triggerUiFeedback();
    } 
    else if (text == "/hush" || text == "/hush" + mention) {
      isTestingAlarm = false; 
      if (isAlarmTriggered) isHushed = true;
      if (!isDoorOpen) { 
        isAlarmTriggered = false; isHushed = false; isArmed = true;
      }
      bot.sendMessage(CHAT_ID, TG_PREFIX + MSG_HUSHED, "");
      triggerUiFeedback();
    } 
    else if (text == "/rebootnow" || text == "/rebootnow" + mention) {
      bot.sendMessage(CHAT_ID, TG_PREFIX + MSG_REBOOTING, "");
      
      // CRITICAL FIX: Explicitly acknowledge the reboot command to Telegram's servers immediately.
      // If we don't do this, the ESP32 restarts, Telegram thinks the command failed, and sends it again on boot (Infinite Bootloop).
      bot.getUpdates(bot.last_message_received + 1); 
      
      shouldReboot = true; // Safely handed off to main loop
    } 
    else if (text == "/testalarmnow" || text == "/testalarmnow" + mention) {
      isTestingAlarm = !isTestingAlarm;
      if (!isTestingAlarm) isHushed = false;
      bot.sendMessage(CHAT_ID, TG_PREFIX + MSG_TESTING, "");
      triggerUiFeedback();
    } 
    else if (text == "/status" || text == "/status" + mention) {
      String tDoor = isDoorOpen ? "Open" : "Closed";
      String tAlarm = (isAlarmTriggered && !isHushed) ? "Alarm" : (isTestingAlarm ? "Alarm" : "Ok");
      String tSys = armPending ? "Pending" : (isArmed ? "Armed" : "Disarmed");
      String tNet = (WiFi.status() == WL_CONNECTED) ? "Online" : "Offline";
      
      struct tm timeinfo;
      char timeBuf[12] = "--:--:--";
      if (ntpInitialized && getLocalTime(&timeinfo, 0)) {
        sprintf(timeBuf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      }

      String msg = MSG_STATUS_TITLE + "\n";
      msg += "Door: " + tDoor + "\n";
      msg += "Alarm: " + tAlarm + "\n";
      msg += "System: " + tSys + "\n";
      msg += "Network: " + tNet + "\n";
      msg += "Time: " + String(timeBuf);
      
      bot.sendMessage(CHAT_ID, TG_PREFIX + msg, "");
    }
  }
}

// --- FreeRTOS Telegram Background Task (Pinned to Core 0) ---
void telegramTask(void *pvParameters) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      // Process outgoing message queue from Core 1
      char outMsg[64];
      while (xQueueReceive(tgQueue, &outMsg, 0) == pdPASS) {
        bot.sendMessage(CHAT_ID, TG_PREFIX + String(outMsg), "");
        vTaskDelay(pdMS_TO_TICKS(100)); // Rate limit buffer
      }
      
      // Poll for incoming group chat commands
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Snappy 500ms delay
  }
}

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

  String json = "{";
  json += "\"d\":\"" + doorStr + "\",";
  json += "\"a\":\"" + alarmStr + "\",";
  json += "\"s\":\"" + armStr + "\",";
  json += "\"w\":\"" + wifiStr + "\",";
  json += "\"t\":\"" + String(timeBuf) + "\",";
  json += "\"va\":" + String(alarmVolume) + ",";
  json += "\"vc\":" + String(chimeVolume);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleAction() {
  String cmd = server.arg("cmd");
  triggerUiFeedback(); 

  if (cmd == "arm") {
    if (isDoorOpen) { armPending = true; isArmed = false; } 
    else { isArmed = true; armPending = false; }
  } 
  else if (cmd == "disarm") {
    isArmed = false; armPending = false; isAlarmTriggered = false; isHushed = false; isTestingAlarm = false;
    nightDisarmTimer = millis(); nightTimerActive = true; 
  } 
  else if (cmd == "hush") {
    isTestingAlarm = false; 
    if (isAlarmTriggered) isHushed = true;
    if (!isDoorOpen) { 
      isAlarmTriggered = false; isHushed = false; isArmed = true;
    }
  } 
  else if (cmd == "test") {
    isTestingAlarm = !isTestingAlarm;
    if (!isTestingAlarm) isHushed = false;
  } 
  else if (cmd == "vol_alarm") {
    alarmVolume = server.arg("val").toInt();
    preferences.putInt("vol_alarm", alarmVolume);
  } 
  else if (cmd == "vol_chime") {
    chimeVolume = server.arg("val").toInt();
    preferences.putInt("vol_chime", chimeVolume);
  }
  
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

  // CRITICAL FIX: Robust, instant Wi-Fi initiation. Clears corrupt states and uses hardware auto-reconnect.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true); 
  delay(100);
  WiFi.setHostname("DoorAlarm");
  WiFi.setSleep(false); 
  WiFi.setAutoReconnect(true); // Hardware level auto-reconnect (Zero latency, prevents loops)
  WiFi.begin(ssid, password);

  secured_client.setInsecure(); 

  tgQueue = xQueueCreate(15, sizeof(char[64]));

  xTaskCreatePinnedToCore(
    telegramTask,   
    "TelegramTask", 
    8192,           
    NULL,           
    1,              
    NULL,           
    0               
  );

  if (MDNS.begin("dooralarm")) MDNS.addService("http", "tcp", 80);

  server.on("/", handleRoot);
  server.on("/st", handleStatus);
  server.on("/ac", handleAction);
  server.begin();
}

void loop() {
  // Safe Reboot handling execution
  if (shouldReboot) {
    delay(1000); // Give the Core 0 task just enough time to breathe before execution
    ESP.restart();
  }

  unsigned long now = millis();
  server.handleClient();

  // 1. Connection Manager
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  static bool bootMessageSent = false;

  if (wifiConnected && !ntpInitialized) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ntpInitialized = true;
  }

  if (wifiConnected && !bootMessageSent) {
    enqueueTgMsg(MSG_CONNECTED);
    bootMessageSent = true;
  }

  // 2. Schedule Evaluation Engine
  if (ntpInitialized && (now - lastTimeCheck > 5000)) {
    lastTimeCheck = now;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) { 
      
      bool isNight = false;
      if (timeinfo.tm_hour > scheduleStartHour || timeinfo.tm_hour < scheduleEndHour) {
        isNight = true;
      } else if (timeinfo.tm_hour == scheduleStartHour && timeinfo.tm_min >= scheduleStartMinute) {
        isNight = true;
      }

      if (!bootTimeSet) { 
        if (isNight) {
          if (isDoorOpen) armPending = true;
          else isArmed = true;
        }
        bootTimeSet = true; 
      }

      if (isNight && !isArmed && !armPending && nightTimerActive) {
        if (now - nightDisarmTimer >= 3600000) { 
          if (isDoorOpen) armPending = true;
          else isArmed = true;
          nightTimerActive = false;
        }
      } else if (!isNight) {
        nightTimerActive = false; 
      }
    }
  }

  // 3. Hardware Edge Detection Matrix
  bool currentDoorState = (digitalRead(REED_PIN) == HIGH);
  if (currentDoorState != lastDoorState) {
    isDoorOpen = currentDoorState;
    
    // Telegram Trigger
    if (isDoorOpen) enqueueTgMsg(MSG_DOOR_OPEN);
    else enqueueTgMsg(MSG_DOOR_CLOSED);

    if (!isArmed && !isAlarmTriggered && !isTestingAlarm) {
      chimeTrigger = isDoorOpen ? 1 : 2; 
      audioTimer = now;
    }

    if (!isDoorOpen) {
      if (armPending) { isArmed = true; armPending = false; }
      if (isAlarmTriggered && isHushed) { isAlarmTriggered = false; isHushed = false; isArmed = true; }
    }

    if (isDoorOpen && (isArmed || armPending)) { 
      if (!isAlarmTriggered) enqueueTgMsg(MSG_ALARM_ACTIVATED); 
      isAlarmTriggered = true; 
      isHushed = false; 
      isArmed = false; 
      armPending = false; 
    }
    lastDoorState = currentDoorState;
  }

  // 4. Audio Processing Engine
  if ((isAlarmTriggered && !isHushed) || isTestingAlarm) {
    if ((now % 200) < 150) setBuzzerVolume(alarmVolume);
    else setBuzzerVolume(0);
    chimeTrigger = 0; uiBeepActive = false; 
  } 
  else if (uiBeepActive) {
    if (now - uiBeepTimer < 60) setBuzzerVolume(chimeVolume);
    else { setBuzzerVolume(0); uiBeepActive = false; }
  }
  else if (chimeTrigger > 0) {
    unsigned long elapsed = now - audioTimer;
    if (chimeTrigger == 1) { 
      if (elapsed < 120) setBuzzerVolume(chimeVolume);
      else if (elapsed < 160) setBuzzerVolume(0);
      else if (elapsed < 280) setBuzzerVolume(chimeVolume / 2);
      else { setBuzzerVolume(0); chimeTrigger = 0; }
    }
    else if (chimeTrigger == 2) { 
      if (elapsed < 120) setBuzzerVolume(chimeVolume);
      else if (elapsed < 180) setBuzzerVolume(0);
      else if (elapsed < 300) setBuzzerVolume(chimeVolume);
      else { setBuzzerVolume(0); chimeTrigger = 0; }
    }
  }
  else {
    setBuzzerVolume(0); 
  }

  // 5. LED Driving Modules
  setLedState(DOOR_LED_PIN, isDoorOpen ? AIRPLANE_BLINK : LED_ON);
  if (!wifiConnected) setLedState(ARMED_LED_PIN, RAPID_BLINK);
  else if (isAlarmTriggered) setLedState(ARMED_LED_PIN, SLOW_BLINK);
  else if (isArmed) setLedState(ARMED_LED_PIN, LED_ON);
  else setLedState(ARMED_LED_PIN, AIRPLANE_BLINK);
}
