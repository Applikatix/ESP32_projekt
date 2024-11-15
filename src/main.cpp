#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Filsystem
auto FS = LittleFS;
Preferences pref;
const char* wifiNamespace = "wifi-creds";
const char* ipNamespace = "ip-conf";

//Server management variabler
AsyncWebServer server(80);

const char* ap_ssid = "ESP-WIFI-MANAGER-JHH";
String ssid, password;
String ip, gateway, subnet;

const unsigned long wifiConnTimeout = 10000;
const unsigned long resetConfigDelay = 10000;

bool connectWiFi();
void startWebServer();
void startWiFiManager();
void loadWiFiCreds();
void loadIpConfig();
void saveWiFiCreds(String ssid, String password);
void saveIpConfig(String ip, String gateway, String subnet);
void resetWiFiConfig();

// //Touch variabler
const int TOUCH = T0;
const int touchThreshold = 35;

unsigned long touchStartTime = 0;
bool isTouched = false;

void touchSensor();
void logTouchData(unsigned long startTime, unsigned long endTime);

//Knap variabler
const int BTN = 16;
const unsigned long btnDebounceDelay = 50;

bool btnState;
unsigned long btnLastPress = 0;
unsigned long btnLastDebounce = 0;
bool btnChanging = false;

void IRAM_ATTR btnIsr() {
  btnLastDebounce = millis();
  btnChanging = true;
}
bool btnHeld();

unsigned long currentTime = 0;
unsigned long prevTime = 0;

//Display
Adafruit_SSD1306 display(128, 32);

void setup() {
  Serial.begin(115200);

  //Filesystem
  Serial.println("Mounting File System");
  if (!FS.begin(true)){
    Serial.println("- ERROR: Failed to mount file system.");
  }

  //Knap
  pinMode(BTN, INPUT_PULLUP);
  btnState = digitalRead(BTN);
  attachInterrupt(BTN, btnIsr, CHANGE);

  //Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Initialising...");
  display.display();

  Serial.println("Loading WiFi credentials and IP configuration into memory");
  loadWiFiCreds();
  loadIpConfig();

  Serial.println("Setting up WiFi");
  if (connectWiFi()) {
    Serial.println("Starting webserver");
    startWebServer();
  } else {
    Serial.println("Starting WiFi manager");
    startWiFiManager();
  }
}

void loop() {
  if (btnHeld()) {
    Serial.println("Reseting WiFi configuration");
    resetWiFiConfig();
  }
  touchSensor();
}

bool btnHeld() {
  if (btnChanging) {
    currentTime = millis();
    if (currentTime - btnLastDebounce > btnDebounceDelay) {
      btnChanging = false;
      btnState = digitalRead(BTN);
      if (!btnState) {
        btnLastPress = currentTime;
        Serial.print("Button pressed");
      } else {
        Serial.println();
        Serial.println("Button released");
      }
    }
  } else if (!btnState) {
    currentTime = millis();
    if (currentTime - btnLastPress > resetConfigDelay) {
      return true;
    } else if (currentTime - prevTime > 1000) {
      prevTime = currentTime;
      Serial.print(".");
    }
  }
  return false;
}

bool connectWiFi() {
  if (ssid.isEmpty()) {
    Serial.println("- SSID unspecified");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  IPAddress localIP, subnetIP, gatewayIP;
  bool staticIp = localIP.fromString(ip) && subnetIP.fromString(subnet) && gatewayIP.fromString(gateway);
  if (staticIp) {
    WiFi.config(localIP, gatewayIP, subnetIP);
  } else {
    Serial.println("- IP not configured");
  }
  
  Serial.println("- Connecting to WiFi...");
  const auto startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime >= wifiConnTimeout) {
      Serial.println("  - Connection timed out");
      return false;
    }
  }
  if (!staticIp) {
    ip = WiFi.localIP().toString();
    gateway = WiFi.gatewayIP().toString();
    subnet = WiFi.subnetMask().toString();
    saveIpConfig(ip, gateway, subnet);
  }
  Serial.print("- Network: ");
  Serial.println(ssid);
  Serial.print("- IP: ");
  Serial.println(ip);
  return true;
}

void startWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(FS, "/index.html", "text/html");
    Serial.println("~~~ Client accessed server");
  });

  server.begin();
  Serial.println("- Server online");

  display.clearDisplay();
  display.print("Network: ");
  display.println(ssid);
  display.print("IP: ");
  display.println(ip);
  display.display();
}

void startWiFiManager() {
  WiFi.softAP(ap_ssid);

  auto ap_ip = WiFi.softAPIP();
  Serial.print("- AP: ");
  Serial.println(ap_ssid);
  Serial.print("- IP: ");
  Serial.println(ap_ip);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(FS, "/wifimanager.html", "text/html");
    Serial.println("~~~ Client accessed AP");
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("~~~ Form sent");

    String ssid, password;
    ssid = request->getParam("ssid", true)->value();
    password = request->getParam("password", true)->value();
    saveWiFiCreds(ssid, password);

    String ip, gateway, subnet;
    ip = request->getParam("ip", true)->value();
    gateway = request->getParam("gateway", true)->value();
    subnet = request->getParam("subnet", true)->value();
    saveIpConfig(ip, gateway, subnet);

    request->send(200, "text/plain", "Settings saved. Restarting...");
    delay(3000);
    ESP.restart();
  });

  server.begin();
  Serial.println("- WiFi manager online");

  display.clearDisplay();
  display.print("AP: ");
  display.println(ap_ssid);
  display.print("IP: ");
  display.println(ap_ip);
  display.display();
}

void resetWiFiConfig() {
  display.clearDisplay();
  display.display();

  pref.begin(wifiNamespace);
  pref.clear();
  pref.end();
  pref.begin(ipNamespace);
  pref.clear();
  pref.end();

  Serial.println("- Data deleted. Restarting...");
  delay(1000);
  ESP.restart();
}

void touchSensor() {
  int touchVal = touchRead(TOUCH);
  if (!isTouched) {
    if (touchVal < touchThreshold) {
      touchStartTime = millis();
      isTouched = true;
      Serial.println("Sensor touched");
    }
  } else if (touchVal >= touchThreshold) {
    unsigned long touchEndTime = millis();
    isTouched = false;
    
    unsigned long duration = touchEndTime - touchStartTime;
    logTouchData(touchStartTime, duration);
  }
}

void logTouchData(unsigned long startTime, unsigned long duration) {
  File file = FS.open("/touch_log.txt", FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file 'touch_log.txt' for appending");
    return;
  }
  String data = "Start time:" + String(startTime) + ",Duration:" + String(duration);
  file.println(data);
  file.close();

  Serial.print("- Logged data: ");
  Serial.println(data);
}

void loadWiFiCreds() {
  pref.begin(wifiNamespace, true);
  ssid = pref.getString("ssid", String());
  Serial.print("  - SSID: ");
  Serial.println(ssid);
  password = pref.getString("password", String());
  Serial.print("  - Password: ");
  Serial.println(password);
  pref.end();
}

void loadIpConfig() {
  pref.begin(ipNamespace, true);
  ip = pref.getString("ip", String());
  Serial.print("  - IP Address: ");
  Serial.println(ip);
  gateway = pref.getString("gateway", String());
  Serial.print("  - Default Gateway: ");
  Serial.println(gateway);
  subnet = pref.getString("subnet", String());
  Serial.print("  - Subnet Mask: ");
  Serial.println(subnet);
  pref.end();
}

void saveWiFiCreds(String ssid, String password) {
  pref.begin(wifiNamespace, false);
  pref.putString("ssid", ssid);
  Serial.print("  - SSID: ");
  Serial.println(ssid);
  pref.putString("password", password);
  Serial.print("  - Password: ");
  Serial.println(password);
  pref.end();
}

void saveIpConfig(String ip, String gateway, String subnet) {
  pref.begin(ipNamespace, false);
  pref.putString("ip", ip);
  Serial.print("  - IP Address: ");
  Serial.println(ip);
  pref.putString("gateway", gateway);
  Serial.print("  - Default Gateway: ");
  Serial.println(gateway);
  pref.putString("subnet", subnet);
  Serial.print("  - Subnet Mask: ");
  Serial.println(subnet);
  pref.end();
}
