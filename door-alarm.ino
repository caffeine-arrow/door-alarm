#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "time.h"

// =================================================================
// 1. CONFIGURATION (Edit these)
// =================================================================
const char* ssid = "IoT-Net";
const char* password = "IoT@26/P12";

// Telegram (Placeholders)
const String botToken = "YOUR_TELEGRAM_BOT_TOKEN_HERE";
const String chatIdx = "-100YOUR_GROUP_ID_HERE"; 

// Network (Static IP for speed)
IPAddress espIp(192, 168, 0, 117);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// Schedule (Auto-Arm)
const int nightStartHour = 23;
const int nightStartMin = 30;
const int nightEndHour = 5;
const int nightEndMin = 30;

// Hardware Pins
const int REED_PIN = 13;
const int BUZZER_PIN = 25;
const int ARMED_LED_PIN = 26;
const int DOOR_LED_PIN = 27;

// =================================================================
// 2. SYSTEM VARIABLES & RTOS
// =================================================================
WebServer server(80);
Preferences preferences;
QueueHandle_t telegramQueue;

volatile bool isArmed = false;
volatile bool armPending = false;
volatile bool isAlarmTriggered = false;
volatile bool isHushed = false;
volatile bool isDoorOpen = false;
bool ntpInitialized = false;
long lastUpdateId = 0;
unsigned long lastTelegramPoll = 0;
bool lastDoorState = false;

// HTML Panel (Material 3 Minified)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><title>Alarm</title><style>body{background:#f7fbf3;font-family:sans-serif;padding:20px;text-align:center}.c{max-width:400px;margin:auto}.box{background:#e1e9dc;padding:20px;border-radius:20px}.bdg{padding:5px 15px;border-radius:15px;font-weight:bold}button{padding:15px;width:100%;margin-top:10px;border:none;border-radius:10px;background:#2b6a41;color:white;font-size:16px}</style></head><body>
<div class="c"><h2>Security</h2><div class="box"><p>Door: <span id="d" class="bdg">--</span></p><p>Status: <span id="s" class="bdg">--</span></p></div>
<button onclick="S('arm')">Arm</button><button onclick="S('disarm')">Disarm</button><button onclick="S('hush')">Hush</button></div>
<script>function U(){fetch('/st').then(r=>r.json()).then(d=>{document.getElementById('d').innerText=d.d;document.getElementById('s').innerText=d.s;})} function S(c){fetch('/ac?cmd='+c);setTimeout(U,200)} setInterval(U,1000);</script></body></html>
)rawliteral";

// =================================================================
// 3. TELEGRAM NETWORKING TASK (CORE 0)
// =================================================================
void telegramNetworkTask(void * parameter) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  for(;;) {
    if (WiFi.status() == WL_CONNECTED && botToken.indexOf("YOUR_") == -1) {
      // Outgoing Messages
      char txBuffer[256];
      if (xQueueReceive(telegramQueue, &txBuffer, 0) == pdTRUE) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/sendMessage");
        http.addHeader("Content-Type", "application/json");
        http.POST("{\"chat_id\":\"" + chatIdx + "\",\"text\":\"" + String(txBuffer) + "\"}");
        http.end();
      }

      // Incoming Commands (Polling)
      if (millis() - lastTelegramPoll >= 3000) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=" + String(lastUpdateId + 1));
        if (http.GET() == HTTP_CODE_OK) {
          String p = http.getString();
          int uIdx = p.lastIndexOf("\"update_id\":");
          if (uIdx != -1) lastUpdateId = p.substring(uIdx + 12, p.indexOf(",", uIdx)).toInt();
          
          if (p.indexOf("\"chat\":{\"id\":" + chatIdx) != -1) {
            if (p.indexOf("/status") != -1) {
              String msg = "Status: " + String(isArmed?"Armed":"Disarmed") + " | Door: " + (isDoorOpen?"Open":"Closed");
              xQueueSend(telegramQueue, &msg, 0);
            }
            else if (p.indexOf("/arm") != -1) { isArmed = !isDoorOpen; armPending = isDoorOpen; }
            else if (p.indexOf("/disarm") != -1) { isArmed = false; isAlarmTriggered = false; isHushed = false; }
            else if (p.indexOf("/hush") != -1) { isHushed = true; }
          }
        }
        http.end();
        lastTelegramPoll = millis();
      }
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// =================================================================
// 4. SETUP & MAIN LOOP (CORE 1)
// =================================================================
void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(ARMED_LED_PIN, OUTPUT);
  pinMode(DOOR_LED_PIN, OUTPUT);
  ledcAttach(BUZZER_PIN, 2400, 8);
  
  WiFi.config(espIp, gateway, subnet);
  WiFi.begin(ssid, password);
  
  telegramQueue = xQueueCreate(10, 256);
  xTaskCreatePinnedToCore(telegramNetworkTask, "TG", 16384, NULL, 1, NULL, 0);

  server.on("/", [](){ server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/st", [](){
     String s = "{\"d\":\"" + String(isDoorOpen?"OPEN":"CLOSED") + "\",\"s\":\"" + (isArmed?"ARMED":"DISARMED") + "\"}";
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
  configTime(7 * 3600, 0, "pool.ntp.org");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // 1. Hardware State
  bool currentDoorState = (digitalRead(REED_PIN) == HIGH);
  if (currentDoorState != lastDoorState) {
    isDoorOpen = currentDoorState;
    if (isArmed && isDoorOpen) isAlarmTriggered = true;
    lastDoorState = currentDoorState;
  }

  // 2. Audio & LED
  if (isAlarmTriggered && !isHushed) {
    ledcWrite(BUZZER_PIN, (now % 500 < 250) ? 255 : 0);
    digitalWrite(ARMED_LED_PIN, (now / 200) % 2);
  } else {
    ledcWrite(BUZZER_PIN, 0);
    digitalWrite(ARMED_LED_PIN, isArmed ? HIGH : LOW);
  }
  digitalWrite(DOOR_LED_PIN, isDoorOpen ? HIGH : LOW);

  // 3. Scheduler
  struct tm t;
  if (getLocalTime(&t)) {
    ntpInitialized = true;
    int cur = t.tm_hour * 60 + t.tm_min;
    int start = nightStartHour * 60 + nightStartMin;
    int end = nightEndHour * 60 + nightEndMin;
    bool isNight = (start > end) ? (cur >= start || cur < end) : (cur >= start && cur < end);
    if (isNight && !isArmed) isArmed = true; 
  }
}
