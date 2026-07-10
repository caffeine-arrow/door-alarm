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
IPAddress gateway(192, 168, 0, 1);   
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);    
IPAddress secondaryDNS(8, 8, 4, 4);

// --- Customization ---
const String doorName = "Back Door"; 
const String botToken = "YOUR_TELEGRAM_BOT_TOKEN_HERE";
const String chatIdx = "YOUR_TELEGRAM_CHAT_ID_HERE";

WebServer server(80);
Preferences preferences;
QueueHandle_t telegramQueue;

// --- System State ---
volatile bool isArmed = false;         
volatile bool armPending = false;
volatile bool isAlarmTriggered = false; 
volatile bool isHushed = false;
volatile bool isTestingAlarm = false;
volatile bool isDoorOpen = false;
volatile bool nightTimerActive = false;
unsigned long nightDisarmTimer = 0;
bool ntpInitialized = false;
bool lastDoorState = false; 
bool wasDisconnected = false; 

// --- Audio ---
int alarmVolume = 255; 
int chimeVolume = 128;
int chimeTrigger = 0;  
unsigned long audioTimer = 0;
unsigned long uiBeepTimer = 0;
bool uiBeepActive = false;

// --- Telegram Task Variables ---
long lastUpdateId = 0;
unsigned long lastTelegramPoll = 0;

// --- Full Web UI with Forced CSS ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Security</title>
<link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@500;700&display=swap" rel="stylesheet">
<style>
  * { font-family: 'Google Sans', sans-serif !important; box-sizing: border-box; }
  body { background:#f7fbf3; color:#191c18; margin:0; padding:16px; display:flex; justify-content:center; }
  .c { width:100%; max-width:400px; display:flex; flex-direction:column; gap:12px; }
  h2 { text-align:center; color:#2b6a41; margin:5px; }
  .box { background:#e1e9dc; border-radius:20px; padding:16px; display:flex; flex-direction:column; gap:10px; }
  .row { display:flex; justify-content:space-between; align-items:center; font-weight:500; }
  .bdg { padding:4px 12px; border-radius:20px; font-weight:700; font-size:13px; }
  .bg { background:#d2e7d6; color:#0f2013; }
  .br { background:#ffdad6; color:#ba1a1a; }
  .bn { background:#ccc; color:#333; }
  .g { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
  button { font-weight:500; font-size:15px; padding:14px; border:none; border-radius:16px; cursor:pointer; transition:transform .1s; }
  button:active { transform:scale(.94); }
  .b1 { background:#2b6a41; color:#fff; }
  .b2 { background:transparent; border:1px solid #727970; color:#2b6a41; }
  .b3 { background:#d2e7d6; color:#0f2013; }
  .f { grid-column:span 2; }
  input[type=range] { -webkit-appearance:none; width:100%; height:6px; border-radius:3px; background:#727970; }
  input[type=range]::-webkit-slider-thumb { -webkit-appearance:none; width:20px; height:20px; border-radius:50%; background:#2b6a41; }
</style>
</head><body>
<div class="c"><h2>Alarm Panel</h2><div class="box"><div class="row"><span>Door</span><span id="d" class="bdg bn">--</span></div><div class="row"><span>Alarm</span><span id="a" class="bdg bn">--</span></div><div class="row"><span>Status</span><span id="s" class="bdg bn">--</span></div><div class="row"><span>System Time</span><span id="t" style="font-size:14px;color:#434940">--:--:--</span></div><div class="row"><span>Network</span><span id="w" class="bdg bn">--</span></div></div>
<div class="g"><button class="b1" onclick="S('arm')">Arm</button><button class="b2" onclick="S('disarm')">Disarm</button><button class="b3 f" onclick="S('hush')">Hush Alarm</button></div><button class="b2" onclick="S('test')">Test Alarm</button>
<div class="box"><span>Alarm Vol</span><input type="range" id="vA" min="0" max="255" onchange="V('vol_alarm',this.value)"><span>Chime Vol</span><input type="range" id="vC" min="0" max="255" onchange="V('vol_chime',this.value)"></div></div>
<script>
  function U(){fetch('/st').then(r=>r.json()).then(d=>{let E=(i,t,c)=>{let e=document.getElementById(i);e.innerText=t;e.className='bdg '+c;};E('d',d.d,d.d==='OPEN'?'br':'bg');E('a',d.a,d.a==='ALARM'?'br':'bg');E('s',d.s,d.s==='ARMED'?'bg':(d.s==='PENDING'?'br':'bn'));E('w',d.w,d.w==='ONLINE'?'bg':'br');document.getElementById('t').innerText=d.t;document.getElementById('vA').value=d.va;document.getElementById('vC').value=d.vc;}).catch(e=>console.error("Data Sync Error",e))}
  function S(c){fetch('/ac?cmd='+c);setTimeout(U,100)}
  function V(c,v){fetch(`/ac?cmd=${c}&val=${v}`)}
  setInterval(U,1000);window.onload=U;
</script></body></html>
)rawliteral";

// --- Helpers ---
void setLedState(int pin, int pattern) {
  if (pattern == 0) digitalWrite(pin, LOW);
  else if (pattern == 1) digitalWrite(pin, HIGH);
  else if (pattern == 2) digitalWrite(pin, (millis() % 1050) < 50 ? HIGH : LOW);
  else if (pattern == 3) digitalWrite(pin, (millis() / 150) % 2 ? HIGH : LOW);
  else if (pattern == 4) digitalWrite(pin, (millis() / 500) % 2 ? HIGH : LOW);
}

void setBuzzerVolume(int vol) { ledcWrite(BUZZER_PIN, vol); }
void triggerUiFeedback() { uiBeepTimer = millis(); uiBeepActive = true; }

void sendAlert(String action) {
  if (botToken.indexOf("YOUR_") == 0) return;
  char msgBuffer[256];
  snprintf(msgBuffer, sizeof(msgBuffer), "%s: %s", doorName.c_str(), action.c_str());
  xQueueSend(telegramQueue, &msgBuffer, 0); 
}

// --- Telegram Task ---
void telegramNetworkTask(void * parameter) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  for(;;) {
    if (WiFi.status() == WL_CONNECTED && botToken.indexOf("YOUR_") != 0) {
      char txBuffer[256];
      if (xQueueReceive(telegramQueue, &txBuffer, 0) == pdTRUE) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/sendMessage");
        http.addHeader("Content-Type", "application/json");
        http.POST("{\"chat_id\":\"" + chatIdx + "\",\"text\":\"" + String(txBuffer) + "\"}");
        http.end();
      }

      if (millis() - lastTelegramPoll >= 5000) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=3&timeout=0");
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          int updateIdx = payload.lastIndexOf("\"update_id\":");
          if (updateIdx != -1) {
            int endIdx = payload.indexOf(",", updateIdx);
            if (endIdx != -1) lastUpdateId = payload.substring(updateIdx + 12, endIdx).toInt();
          }

          if (payload.indexOf(chatIdx) != -1) {
            if (payload.indexOf("/status") != -1) {
               sendAlert("System Online");
            } else if (payload.indexOf("/arm") != -1) {
               isArmed = true; armPending = false; sendAlert("Armed");
            } else if (payload.indexOf("/disarm") != -1) {
               isArmed = false; isAlarmTriggered = false; sendAlert("Disarmed");
            }
          }
        }
        http.end();
        lastTelegramPoll = millis();
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS); 
  }
}

// --- Handlers ---
void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }
void handleStatus() {
  struct tm timeinfo;
  char timeBuf[12] = "--:--:--";
  if (getLocalTime(&timeinfo, 0)) sprintf(timeBuf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  String json = "{\"d\":\"" + String(isDoorOpen?"OPEN":"CLOSED") + "\",\"a\":\"" + String(isAlarmTriggered?"ALARM":"OK") + "\",\"s\":\"" + String(isArmed?"ARMED":"DISARMED") + "\",\"w\":\"ONLINE\",\"t\":\"" + String(timeBuf) + "\",\"va\":" + alarmVolume + ",\"vc\":" + chimeVolume + "}";
  server.send(200, "application/json", json);
}

void handleAction() {
  String cmd = server.arg("cmd");
  triggerUiFeedback();
  if (cmd == "arm") isArmed = true;
  else if (cmd == "disarm") isArmed = false;
  else if (cmd == "hush") isAlarmTriggered = false;
  else if (cmd == "vol_alarm") alarmVolume = server.arg("val").toInt();
  server.send(200, "text/plain", "OK");
}

void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(ARMED_LED_PIN, OUTPUT);
  pinMode(DOOR_LED_PIN, OUTPUT);
  ledcAttach(BUZZER_PIN, 2400, 8); 
  
  telegramQueue = xQueueCreate(15, 256);
  xTaskCreatePinnedToCore(telegramNetworkTask, "TelegramTask", 8192, NULL, 1, NULL, 0);
  
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);
  
  server.on("/", handleRoot); 
  server.on("/st", handleStatus); 
  server.on("/ac", handleAction); 
  server.begin();
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  server.handleClient();
  
  // WiFi Watchdog (No aggressive reconfiguration inside loop)
  static unsigned long lastWifiCheck = 0;
  if(WiFi.status() != WL_CONNECTED && (millis() - lastWifiCheck > 15000)) {
     WiFi.begin(ssid, password);
     lastWifiCheck = millis();
  }

  // Door Logic
  bool currentDoorState = (digitalRead(REED_PIN) == HIGH);
  if (currentDoorState != lastDoorState) {
    isDoorOpen = currentDoorState;
    if (isArmed && isDoorOpen) isAlarmTriggered = true;
    lastDoorState = currentDoorState;
  }

  // Buzzer/LED logic
  if (isAlarmTriggered) {
    setBuzzerVolume((millis() % 400 < 200) ? alarmVolume : 0);
    setLedState(ARMED_LED_PIN, 3);
  } else {
    setBuzzerVolume(0);
    setLedState(ARMED_LED_PIN, isArmed ? 1 : 2);
  }
  setLedState(DOOR_LED_PIN, isDoorOpen ? 2 : 1);
}
