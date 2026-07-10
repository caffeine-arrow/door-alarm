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

// --- Config ---
const char* ssid = "IoT-Net";
const char* password = "IoT@26/P12";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

// --- Static IP ---
IPAddress local_IP(192, 168, 0, 117);
IPAddress gateway(192, 168, 0, 1);   
IPAddress subnet(255, 255, 255, 0);

const String botToken = "YOUR_TELEGRAM_BOT_TOKEN_HERE";
const String chatIdx = "YOUR_TELEGRAM_CHAT_ID_HERE";
const String doorName = "Back Door";

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
long lastUpdateId = 0;
unsigned long lastTelegramPoll = 0;

// --- Audio/LED Globals ---
int alarmVolume = 255;
int chimeVolume = 128;
int chimeTrigger = 0;
unsigned long audioTimer = 0;
unsigned long uiBeepTimer = 0;
bool uiBeepActive = false;
bool lastDoorState = false;

// --- HTML Interface (Google Sans Forced) ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@500;700&display=swap" rel="stylesheet">
<style>
  * { font-family: 'Google Sans', sans-serif !important; box-sizing: border-box; }
  body { background:#f7fbf3; padding:16px; display:flex; justify-content:center; }
  .c { width:100%; max-width:400px; display:flex; flex-direction:column; gap:12px; }
  .box { background:#e1e9dc; border-radius:20px; padding:16px; }
  .row { display:flex; justify-content:space-between; margin-bottom:10px; }
  button { width:100%; padding:14px; border:none; border-radius:16px; background:#2b6a41; color:#fff; font-weight:700; cursor:pointer; }
</style></head><body>
<div class="c"><h2>Security</h2><div class="box" id="status">Loading...</div>
<button onclick="fetch('/ac?cmd=arm')">Arm</button>
<button onclick="fetch('/ac?cmd=disarm')" style="background:#ba1a1a">Disarm</button>
</div><script>
  setInterval(()=>{fetch('/st').then(r=>r.json()).then(d=>{document.getElementById('status').innerHTML=`Door: ${d.d}<br>Status: ${d.s}`;})}, 2000);
</script></body></html>
)rawliteral";

// --- Helpers ---
void sendAlert(String action) {
  if (botToken.indexOf("YOUR_") == 0) return;
  String fullMessage = doorName + ": " + action;
  xQueueSend(telegramQueue, fullMessage.c_str(), 0);
}

// --- Telegram Task (Optimized for Memory) ---
void telegramNetworkTask(void * parameter) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  for(;;) {
    if (WiFi.status() == WL_CONNECTED) {
      char txBuffer[256];
      if (xQueueReceive(telegramQueue, &txBuffer, 0) == pdTRUE) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/sendMessage");
        http.addHeader("Content-Type", "application/json");
        http.POST("{\"chat_id\":\"" + chatIdx + "\",\"text\":\"" + String(txBuffer) + "\"}");
        http.end();
      }

      if (millis() - lastTelegramPoll > 5000) {
        http.begin(client, "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=" + String(lastUpdateId + 1));
        if (http.GET() == HTTP_CODE_OK) {
           String p = http.getString();
           if (p.indexOf("/status") != -1) sendAlert("System Active"); // Add logic here
           // Update lastUpdateId logic...
        }
        http.end();
        lastTelegramPoll = millis();
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// --- Setup ---
void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(ARMED_LED_PIN, OUTPUT);
  pinMode(DOOR_LED_PIN, OUTPUT);
  ledcAttach(BUZZER_PIN, 2400, 8);
  
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  
  telegramQueue = xQueueCreate(10, 256);
  xTaskCreatePinnedToCore(telegramNetworkTask, "TG", 8192, NULL, 1, NULL, 0);
  
  server.on("/", [](){ server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/st", [](){ server.send(200, "application/json", "{\"d\":\"OK\",\"s\":\"IDLE\"}"); });
  server.on("/ac", [](){ server.send(200, "text/plain", "OK"); });
  server.begin();
}

// --- Main Loop ---
void loop() {
  server.handleClient();
  
  // Logic
  bool currentDoorState = (digitalRead(REED_PIN) == HIGH);
  if (currentDoorState != lastDoorState) {
    isDoorOpen = currentDoorState;
    lastDoorState = currentDoorState;
    if(isArmed && isDoorOpen) isAlarmTriggered = true;
  }
  
  // Buzzer/LED Control logic...
  delay(10);
}
