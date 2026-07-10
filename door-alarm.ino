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

// --- Network Configuration ---
const char* ssid = "IoT-Net";
const char* password = "IoT@26/P12";
IPAddress espIp(192, 168, 0, 117);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// --- Time Schedule Settings (Easy Control) ---
const int nightStartHour = 23;
const int nightStartMin = 30;
const int nightEndHour = 5;
const int nightEndMin = 30;

// --- Telegram Configuration (Placeholders) ---
const String botToken = "YOUR_TELEGRAM_BOT_TOKEN_HERE";
const String chatIdx = "YOUR_GROUP_CHAT_ID_HERE"; // Must include the -100 prefix

// --- System Variables ---
WebServer server(80);
Preferences preferences;
QueueHandle_t telegramQueue;

volatile bool isArmed = false;
volatile bool armPending = false;
volatile bool isAlarmTriggered = false;
volatile bool isHushed = false;
volatile bool isTestingAlarm = false;
volatile bool isDoorOpen = false;
volatile unsigned long nightDisarmTimer = 0;
volatile bool nightTimerActive = false;

bool bootTimeSet = false;
bool lastDoorState = false;
bool ntpInitialized = false;
bool wasDisconnected = false;
unsigned long lastTimeCheck = 0;
unsigned long lastTelegramPoll = 0;
long lastUpdateId = 0;
unsigned long previousWifiMillis = 0;

// --- Audio ---
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

// --- HTML Panel ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Alarm Panel</title><link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@500;700&display=swap" rel="stylesheet"><style>*{font-family:'Google Sans',sans-serif !important} body{background:#f7fbf3;color:#191c18;margin:0;padding:16px;display:flex;justify-content:center} .c{width:100%;max-width:400px;display:flex;flex-direction:column;gap:12px} h2{text-align:center;color:#2b6a41;margin:5px} .box{background:#e1e9dc;border-radius:20px;padding:16px;display:flex;flex-direction:column;gap:10px} .row{display:flex;justify-content:space-between;align-items:center;font-weight:500} .bdg{padding:4px 12px;border-radius:20px;font-weight:700;font-size:13px} .bg{background:#d2e7d6;color:#0f2013} .br{background:#ffdad6;color:#ba1a1a} .bn{background:#ccc;color:#333} .g{display:grid;grid-template-columns:1fr 1fr;gap:10px} button{font-weight:500;font-size:15px;padding:14px;border:none;border-radius:16px;cursor:pointer;transition:transform .1s} button:active{transform:scale(.94)} .b1{background:#2b6a41;color:#fff} .b2{background:transparent;border:1px solid #727970;color:#2b6a41} .b3{background:#d2e7d6;color:#0f2013} .f{grid-column:span 2} input[type=range]{-webkit-appearance:none;width:100%;height:6px;border-radius:3px;background:#727970} input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#2b6a41}</style></head><body>
<div class="c"><h2>Security Panel</h2><div class="box"><div class="row"><span>Door</span><span id="d" class="bdg bn">--</span></div><div class="row"><span>Alarm</span><span id="a" class="bdg bn">--</span></div><div class="row"><span>Status</span><span id="s" class="bdg bn">--</span></div></div>
<div class="g"><button class="b1" onclick="S('arm')">Arm</button><button class="b2" onclick="S('disarm')">Disarm</button><button class="b3 f" onclick="S('hush')">Hush</button></div>
<script>function U(){fetch('/st').then(r=>r.json()).then(d=>{let E=(i,t,c)=>{let e=document.getElementById(i);e.innerText=t;e.className='bdg '+c;};E('d',d.d,d.d==='OPEN'?'br':'bg');E('a',d.a,d.a==='ALARM'?'br':'bg');E('s',d.s,d.s==='ARMED'?'bg':(d.s==='PENDING'?'br':'bn'));}).catch(e=>console.error("Sync Error",e))} function S(c){fetch('/ac?cmd='+c);setTimeout(U,100)} setInterval(U,1000);window.onload=U;</script></body></html>
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

// --- Tasks ---
void sendAlert(String action) {
  if (botToken.indexOf("YOUR_") != -1) return;
  String msg = "Back Door: " + action;
  xQueueSend(telegramQueue, &msg, 0);
}

void telegramNetworkTask(void * parameter) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  for(;;) {
    if (WiFi.status() == WL_CONNECTED && botToken.indexOf("YOUR_") == -1) {
      char txBuffer[256];
      
      // 1. Send outgoing messages
      if (xQueueReceive(telegramQueue, &txBuffer, 0) == pdTRUE) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/sendMessage");
        http.addHeader("Content-Type", "application/json");
        http.POST("{\"chat_id\":\"" + chatIdx + "\",\"text\":\"" + String(txBuffer) + "\"}");
        http.end();
      }
      
      // 2. Poll for incoming messages (ADDED limit=2 TO PREVENT CRASH IN GROUPS)
      if (millis() - lastTelegramPoll >= 3000) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/getUpdates?limit=2&offset=" + String(lastUpdateId + 1));
        if (http.GET() == HTTP_CODE_OK) {
          String p = http.getString();
          int uIdx = p.lastIndexOf("\"update_id\":");
          
          if (uIdx != -1) {
            lastUpdateId = p.substring(uIdx + 12, p.indexOf(",", uIdx)).toInt();
            
            // Verifies the message came from your specific group chat ID
            if (p.indexOf("\"chat\":{\"id\":" + chatIdx) != -1) {
              if (p.indexOf("/status") != -1) sendAlert("Door: " + String(isDoorOpen?"OPEN":"CLOSED") + " | Status: " + (isArmed?"ARMED":"DISARMED"));
              else if (p.indexOf("/arm") != -1) { isArmed = !isDoorOpen; armPending = isDoorOpen; sendAlert("Arming requested."); }
              else if (p.indexOf("/disarm") != -1) { isArmed = false; isAlarmTriggered = false; isHushed = false; sendAlert("Disarmed."); }
              else if (p.indexOf("/hush") != -1) { isHushed = true; sendAlert("Siren Hushed."); }
            }
          }
        }
        http.end();
        lastTelegramPoll = millis();
      }
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(ARMED_LED_PIN, OUTPUT);
  pinMode(DOOR_LED_PIN, OUTPUT);
  ledcAttach(BUZZER_PIN, 2400, 8);
  
  isDoorOpen = (digitalRead(REED_PIN) == HIGH);
  lastDoorState = isDoorOpen;

  WiFi.config(espIp, gateway, subnet);
  WiFi.begin(ssid, password);
  
  telegramQueue = xQueueCreate(10, 256);
  xTaskCreatePinnedToCore(telegramNetworkTask, "TG", 16384, NULL, 1, NULL, 0);

  server.on("/", [](){ server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/st", [](){
     String s = "{\"d\":\"" + String(isDoorOpen?"OPEN":"CLOSED") + "\",\"a\":\"" + (isAlarmTriggered?"ALARM":"OK") + "\",\"s\":\"" + (isArmed?"ARMED":"DISARMED") + "\"}";
     server.send(200, "application/json", s);
  });
  server.on("/ac", [](){ 
     String c = server.arg("cmd");
     if(c=="arm") { isArmed = !isDoorOpen; armPending = isDoorOpen; }
     else if(c=="disarm") { isArmed = false; isAlarmTriggered = false; isHushed = false; }
     else if(c=="hush") isHushed = true;
     server.send(200, "text/plain", "OK");
  });
  server.begin();
}

void loop() {
  unsigned long now = millis();
  server.handleClient();
  
  // Connection Monitor
  if (WiFi.status() != WL_CONNECTED) {
    if (now - previousWifiMillis >= 5000) {
      WiFi.begin(ssid, password);
      previousWifiMillis = now;
    }
  } else if (!ntpInitialized) {
    configTime(0, 0, "pool.ntp.org");
    ntpInitialized = true;
  }

  // Schedule Logic (Uses the easy control variables at the top)
  if (now - lastTimeCheck > 10000 && ntpInitialized) {
    lastTimeCheck = now;
    struct tm t;
    if (getLocalTime(&t)) {
      int cur = t.tm_hour * 60 + t.tm_min;
      int start = nightStartHour * 60 + nightStartMin;
      int end = nightEndHour * 60 + nightEndMin;
      bool isNight = (start > end) ? (cur >= start || cur < end) : (cur >= start && cur < end);
      if (!bootTimeSet) { if(isNight) { isArmed = !isDoorOpen; armPending = isDoorOpen; } bootTimeSet = true; }
    }
  }

  // Hardware Logic
  bool currentDoorState = (digitalRead(REED_PIN) == HIGH);
  if (currentDoorState != lastDoorState) {
    isDoorOpen = currentDoorState;
    if (!isArmed && !isAlarmTriggered) { chimeTrigger = isDoorOpen ? 1 : 2; audioTimer = now; }
    if (isDoorOpen && (isArmed || armPending)) { isAlarmTriggered = true; isHushed = false; isArmed = false; sendAlert("ALERT: Alarm Activated!"); }
    lastDoorState = currentDoorState;
  }

  // Audio Processing
  if (isAlarmTriggered && isHushed) setBuzzerVolume(0);
  else if (isAlarmTriggered && !isHushed) {
    if ((now % 200) < 150) setBuzzerVolume(alarmVolume); else setBuzzerVolume(0);
  } else if (chimeTrigger > 0) {
    unsigned long elapsed = now - audioTimer;
    if (chimeTrigger == 1) { if (elapsed < 120) setBuzzerVolume(chimeVolume); else { setBuzzerVolume(0); chimeTrigger = 0; } }
    else if (chimeTrigger == 2) { if (elapsed < 120) setBuzzerVolume(chimeVolume); else { setBuzzerVolume(0); chimeTrigger = 0; } }
  } else setBuzzerVolume(0);

  // LED Logic
  setLedState(DOOR_LED_PIN, isDoorOpen ? AIRPLANE_BLINK : LED_ON);
  if (isAlarmTriggered) setLedState(ARMED_LED_PIN, SLOW_BLINK);
  else if (isArmed) setLedState(ARMED_LED_PIN, LED_ON);
  else setLedState(ARMED_LED_PIN, AIRPLANE_BLINK);
}
