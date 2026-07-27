/*
  Microgrid Watch — Node 01
  ESP32 firmware: reads 3x INA219 sensors (panel/battery/load), publishes
  telemetry to HiveMQ Cloud over MQTT/TLS, controls a relay (load shed)
  and a buzzer (alerts), and serves a local fallback web dashboard if
  WiFi/broker is unreachable.

  Libraries needed (Library Manager):
    - PubSubClient        by Nick O'Leary
    - Adafruit INA219      by Adafruit
    - ArduinoJson          by Benoit Blanchon
  Board: ESP32 core installed via Boards Manager.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <ArduinoJson.h>
#include <WebServer.h>

// ---------------- USER CONFIG ----------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";

// HiveMQ Cloud → Cluster details. Use the RAW MQTT/TLS port (8883) here,
// not the WebSocket port (8884) that the browser dashboard uses.
const char* MQTT_HOST     = "xxxxxxxx.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;
const char* MQTT_USER     = "YOUR_HIVEMQ_USERNAME";
const char* MQTT_PASS     = "YOUR_HIVEMQ_PASSWORD";
const char* MQTT_TOPIC    = "microgrid/site1/telemetry";
const char* MQTT_CLIENT_ID = "esp32-microgrid-node01";

const unsigned long PUBLISH_INTERVAL_MS = 5000;   // telemetry cadence

// Load-shed / alert thresholds (mirror the dashboard defaults; keep in sync
// if you change them there, or later swap this for values read over MQTT).
const float BATT_LOW_V    = 11.5;
const float BATT_CRIT_V   = 10.8;
const float LOAD_I_MAX    = 10.0;
const float LOAD_P_MAX    = 100.0;

// Pins
const int RELAY_PIN  = 26;   // drives the load-shed relay (active HIGH — adjust if your module is active LOW)
const int BUZZER_PIN = 27;

// INA219 I2C addresses (set via A0/A1 solder pads on the breakout)
const uint8_t ADDR_PANEL   = 0x41;
const uint8_t ADDR_BATTERY = 0x40;
const uint8_t ADDR_LOAD    = 0x44;

// ---------------- globals ----------------
Adafruit_INA219 inaPanel(ADDR_PANEL);
Adafruit_INA219 inaBattery(ADDR_BATTERY);
Adafruit_INA219 inaLoad(ADDR_LOAD);

WiFiClientSecure tlsClient;
PubSubClient mqttClient(tlsClient);
WebServer webServer(80);

unsigned long lastPublish = 0;
bool relayOn = true;

struct Reading {
  float panelV, panelI, panelP;
  float batteryV, batteryI, batteryP;
  float loadV, loadI, loadP;
} latest;

// ---------------- WiFi ----------------
void connectWiFi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed — will retry in loop()");
  }
}

// ---------------- MQTT ----------------
void connectMqtt(){
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) return;

  Serial.print("Connecting to HiveMQ Cloud...");
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    Serial.println(" connected.");
  } else {
    Serial.print(" failed, rc=");
    Serial.println(mqttClient.state());
  }
}

void publishTelemetry(){
  StaticJsonDocument<384> doc;
  doc["panelV"]   = latest.panelV;
  doc["panelI"]   = latest.panelI;
  doc["panelP"]   = latest.panelP;
  doc["batteryV"] = latest.batteryV;
  doc["batteryI"] = latest.batteryI;
  doc["batteryP"] = latest.batteryP;
  doc["loadV"]    = latest.loadV;
  doc["loadI"]    = latest.loadI;
  doc["loadP"]    = latest.loadP;
  doc["relayOn"]  = relayOn;

  char payload[384];
  size_t n = serializeJson(doc, payload);

  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC, payload, n);
  }
  Serial.println(payload);
}

// ---------------- sensors ----------------
void readSensors(){
  latest.panelV = inaPanel.getBusVoltage_V();
  latest.panelI = inaPanel.getCurrent_mA() / 1000.0;
  latest.panelP = latest.panelV * latest.panelI;

  latest.batteryV = inaBattery.getBusVoltage_V();
  latest.batteryI = inaBattery.getCurrent_mA() / 1000.0;
  latest.batteryP = latest.batteryV * latest.batteryI;

  latest.loadV = inaLoad.getBusVoltage_V();
  latest.loadI = inaLoad.getCurrent_mA() / 1000.0;
  latest.loadP = latest.loadV * latest.loadI;
}

// ---------------- protection logic ----------------
void evaluateProtection(){
  bool critical = latest.batteryV > 0 && latest.batteryV < BATT_CRIT_V;
  bool overCurrent = latest.loadI > LOAD_I_MAX || latest.loadP > LOAD_P_MAX;

  if (critical || overCurrent) {
    relayOn = false;
    digitalWrite(RELAY_PIN, LOW);   // shed the load
    tone(BUZZER_PIN, 2000, 300);
  } else {
    relayOn = true;
    digitalWrite(RELAY_PIN, HIGH);
  }

  if (latest.batteryV > 0 && latest.batteryV < BATT_LOW_V && !critical) {
    tone(BUZZER_PIN, 1200, 150);    // short low-battery chirp, not a full shed
  }
}

// ---------------- local fallback web dashboard ----------------
void handleRoot(){
  char html[900];
  snprintf(html, sizeof(html),
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Node 01 — local fallback</title>"
    "<style>body{font-family:monospace;background:#10161a;color:#edf0ec;padding:20px}"
    "h1{font-size:16px}div{margin:6px 0;padding:8px;background:#161f24;border-radius:8px}</style>"
    "<meta http-equiv='refresh' content='5'></head><body>"
    "<h1>MICROGRID·WATCH — local fallback (no broker)</h1>"
    "<div>Panel: %.2f V | %.2f A | %.1f W</div>"
    "<div>Battery: %.2f V | %.2f A | %.1f W</div>"
    "<div>Load: %.2f V | %.2f A | %.1f W</div>"
    "<div>Relay: %s</div>"
    "</body></html>",
    latest.panelV, latest.panelI, latest.panelP,
    latest.batteryV, latest.batteryI, latest.batteryP,
    latest.loadV, latest.loadI, latest.loadP,
    relayOn ? "closed (load powered)" : "open (load shed)"
  );
  webServer.send(200, "text/html", html);
}

// ---------------- setup / loop ----------------
void setup(){
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  Wire.begin();
  inaPanel.begin();
  inaBattery.begin();
  inaLoad.begin();

  connectWiFi();

  // HiveMQ Cloud uses a publicly trusted CA (Let's Encrypt / ISRG Root X1).
  // setInsecure() skips certificate validation for simplicity — fine for a
  // bench/rural deployment, but swap in setCACert() with the ISRG Root X1
  // PEM if you want full chain verification.
  tlsClient.setInsecure();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  webServer.on("/", handleRoot);
  webServer.begin();

  Serial.println("Setup complete.");
}

void loop(){
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if (!mqttClient.connected()) {
    connectMqtt();
  }
  mqttClient.loop();
  webServer.handleClient();

  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = millis();
    readSensors();
    evaluateProtection();
    publishTelemetry();
  }
}
