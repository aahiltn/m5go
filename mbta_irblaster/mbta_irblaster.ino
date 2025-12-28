#include <M5Stack.h>
#undef min
#undef max
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SinricPro.h>
#include <SinricProLight.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ================= CONFIGURATION =================
// --- WIFI & SINRIC CONFIGURATION ---
#define WIFI_SSID         "NURes-device"
#define WIFI_PASS         ""
#define APP_KEY           ""    
#define APP_SECRET        ""   
#define LIGHT_ID          ""


// Timezone: EST (-5 hours = -18000 seconds). Adjust for DST (-4h) if needed manually or use accurate TZ string.
#define GMT_OFFSET_SEC    -18000 
#define DST_OFFSET_SEC    3600

// API Limit increased to 6 to find trains that aren't "too close"
const char* url_green = "https://api-v3.mbta.com/predictions?filter[stop]=place-nwu&filter[route]=Green-E&filter[direction_id]=1&sort=departure_time&page[limit]=6";
const char* url_orange = "https://api-v3.mbta.com/predictions?filter[stop]=place-rugg&filter[route]=Orange&filter[direction_id]=1&sort=arrival_time&page[limit]=6";

// IR CONFIG (Port B = 26)
const uint16_t kIrLed = 26; 
IRsend irsend(kIrLed);

// IR CODES
#define IR_POWER_ON     0xF7C03F
#define IR_POWER_OFF    0xF740BF
#define IR_COLOR_RED    0xF720DF
#define IR_COLOR_GREEN  0xF7A05F
#define IR_COLOR_BLUE   0xF7609F
#define IR_BRIGHT_UP    0x000000 
#define IR_BRIGHT_DOWN  0x000000 
#define IR_COLOR_WHITE  0x000000 

// ================= GLOBALS =================
unsigned long lastMbtaUpdate = 0;
String statusNEU = "Loading...";
String statusRuggles = "Loading...";
bool heartbeatColor = false; 

// Forward Declarations
void drawInterface();
void showIRPopup(String msg, uint32_t color);

// ================= TIME HELPERS =================

// Convert MBTA ISO8601 string (2025-12-25T19:30:00-05:00) to Unix Timestamp
time_t parseMbtaTime(const char* isoStr) {
  struct tm tm = {0};
  // Manual parsing is safer than strptime on some ESP cores for ISO8601
  int y, M, d, h, m, s;
  sscanf(isoStr, "%d-%d-%dT%d:%d:%d", &y, &M, &d, &h, &m, &s);
  
  tm.tm_year = y - 1900;
  tm.tm_mon = M - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = m;
  tm.tm_sec = s;
  
  // Create timestamp (assuming input is local time as per API)
  return mktime(&tm);
}

// Format timestamp to 12H string (5:30 PM)
String formatTime12H(time_t t) {
  struct tm *tm = localtime(&t);
  int h = tm->tm_hour;
  String suffix = " AM";
  if (h >= 12) { suffix = " PM"; if (h > 12) h -= 12; } 
  else if (h == 0) { h = 12; }
  
  char buf[10];
  sprintf(buf, "%d:%02d", h, tm->tm_min);
  return String(buf) + suffix;
}

// ================= SINRIC CALLBACKS =================
bool onPowerState(const String &deviceId, bool &state) {
  irsend.sendNEC(state ? IR_POWER_ON : IR_POWER_OFF, 32);
  showIRPopup(state ? "POWER ON" : "POWER OFF", TFT_BLUE);
  return true;
}

bool onBrightness(const String &deviceId, int &brightness) {
  if (brightness > 50) {
    irsend.sendNEC(IR_BRIGHT_UP, 32);
    showIRPopup("BRIGHT UP", TFT_MAGENTA);
  } else {
    irsend.sendNEC(IR_BRIGHT_DOWN, 32);
    showIRPopup("BRIGHT DOWN", TFT_MAGENTA);
  }
  return true;
}

bool onColor(const String &deviceId, byte &r, byte &g, byte &b) {
  if (r > 200) irsend.sendNEC(IR_COLOR_RED, 32);
  else if (g > 200) irsend.sendNEC(IR_COLOR_GREEN, 32);
  else if (b > 200) irsend.sendNEC(IR_COLOR_BLUE, 32);
  else irsend.sendNEC(IR_COLOR_WHITE, 32);
  showIRPopup("COLOR SET", TFT_RED); 
  return true;
}

void showIRPopup(String msg, uint32_t color) {
  M5.Lcd.fillRect(40, 80, 240, 80, color);
  M5.Lcd.drawRect(40, 80, 240, 80, TFT_WHITE);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextDatum(MC_DATUM); 
  M5.Lcd.drawString("IR COMMAND:", 160, 100);
  M5.Lcd.drawString(msg, 160, 140);
  delay(1500); 
  drawInterface();
}

// ================= API FETCH LOGIC =================
String fetchNextTrain(const char* url, int paddingMinutes) {
  if (WiFi.status() != WL_CONNECTED) return "No WiFi";
  
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  String result = "Too Soon"; // Default if all trains are within padding window

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096); 
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // Get Current Time
      time_t now;
      time(&now);

      JsonArray data = doc["data"];
      
      // Loop through trains until we find one far enough away
      for (JsonObject v : data) {
        const char* arrival = v["attributes"]["arrival_time"]; 
        if (arrival == nullptr) arrival = v["attributes"]["departure_time"];
        if (arrival == nullptr) continue;
        
        // Parse Train Time
        time_t trainTime = parseMbtaTime(arrival);
        
        // Calculate difference in minutes
        double diffMinutes = difftime(trainTime, now) / 60.0;
        
        // Check Padding
        if (diffMinutes >= paddingMinutes) {
          result = formatTime12H(trainTime);
          break; // Found our train! Stop looking.
        }
      }
      
      // If we looped through all 6 trains and they were ALL too soon
      if (result == "Too Soon" && data.size() > 0) {
        result = "Walk Fast!"; 
      } else if (data.size() == 0) {
        result = "No Svc";
      }
    }
  } 
  http.end();
  return result;
}

void updateMBTA() {
  // 4 Minute Padding for Green Line
  statusNEU = fetchNextTrain(url_green, 5);
  
  // 12 Minute Padding for Orange Line
  statusRuggles = fetchNextTrain(url_orange, 14);
}

// ================= DISPLAY LOGIC =================
void drawInterface() {
  M5.Lcd.fillScreen(TFT_BLACK);
  
  // --- TOP: NORTHEASTERN (Medford/Tufts) ---
  M5.Lcd.fillRect(0, 0, 320, 120, TFT_BLACK);
  M5.Lcd.fillCircle(40, 60, 25, TFT_GREEN);
  M5.Lcd.setTextColor(TFT_BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.drawString("E", 40, 60);

  M5.Lcd.setTextColor(TFT_GREEN);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextDatum(TL_DATUM); 
  M5.Lcd.drawString("Medford/Tufts [NU]", 80, 40);
  M5.Lcd.drawString("(+5m pad)", 80, 95); // Visual Reminder
  
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.drawString(statusNEU, 80, 65);

  // --- DIVIDER ---
  M5.Lcd.drawLine(20, 120, 300, 120, 0x7BEF);

  // --- BOTTOM: RUGGLES (Oak Grove) ---
  M5.Lcd.fillRect(0, 121, 320, 120, TFT_BLACK);
  M5.Lcd.fillCircle(40, 180, 25, TFT_ORANGE);
  M5.Lcd.setTextColor(TFT_BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.drawString("O", 40, 180);

  M5.Lcd.setTextColor(TFT_ORANGE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.drawString("Oak Grove [Ruggles]", 80, 160);
  M5.Lcd.drawString("(+14m pad)", 80, 215); // Visual Reminder

  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.drawString(statusRuggles, 80, 185);

  // --- HEARTBEAT DOT ---
  heartbeatColor = !heartbeatColor;
  M5.Lcd.fillCircle(310, 10, 5, heartbeatColor ? TFT_GREEN : TFT_RED);
}

// ================= MAIN =================
void setup() {
  M5.begin();
  M5.Lcd.setRotation(1); 
  M5.Lcd.fillScreen(TFT_BLACK);
  
  irsend.begin();

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.println("\nConnected!");
  
  // --- NEW: Sync Time ---
  M5.Lcd.println("Syncing Clock...");
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  
  // Wait for time to be set
  time_t now = time(nullptr);
  while (now < 1000) {
    delay(500);
    M5.Lcd.print(".");
    now = time(nullptr);
  }
  M5.Lcd.println("\nTime Synced!");

  SinricProLight &myLight = SinricPro[LIGHT_ID];
  myLight.onPowerState(onPowerState);
  myLight.onBrightness(onBrightness);
  myLight.onColor(onColor);
  SinricPro.begin(APP_KEY, APP_SECRET);
  
  updateMBTA();
  drawInterface();
}

void loop() {
  SinricPro.handle();
  M5.update();

  // Changed to 120,000ms (2 minutes)
  if (millis() - lastMbtaUpdate >= 120000) {
    updateMBTA();
    drawInterface();
    lastMbtaUpdate = millis();
  }
}