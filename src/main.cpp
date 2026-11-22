#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===================== ISI BAGIAN INI =====================
// WiFi (Wokwi pakai Wokwi-GUEST tanpa password)
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// MQTT EMQX (pakai user untuk ESP32, bukan user website)
const char* MQTT_HOST = "q5403f55.ala.asia-southeast1.emqxsl.com";
const int   MQTT_PORT = 8883;               // MQTT over TLS/SSL
const char* MQTT_USER = "Paus";
const char* MQTT_PASS = "12345678";
const char* TOPIC_DATA = "tempmon/data";    // dibaca oleh website
const char* TOPIC_CTRL = "tempmon/control"; // kirim "1" / "0" untuk AC manual

// Telegram (token dari BotFather, Chat ID dari @userinfobot)
const char* TG_TOKEN   = "9155116295:AAGs914q0dXI94fw9hzT2exPbmoY1x3v2EU";
const char* TG_CHAT_ID = "6948542551";
// ==========================================================

// Batas suhu (samakan dengan website)
const float TEMP_DINGIN = 20.0;
const float TEMP_PANAS  = 30.0;
const float TEMP_AC_ON  = 28.0;

const unsigned long READ_INTERVAL   = 2000;    // baca sensor tiap 2 detik
const unsigned long TG_MIN_GAP      = 30000;   // jeda minimal antar pesan Telegram
const unsigned long TG_REMINDER     = 300000;  // ingatkan tiap 5 menit selama PANAS

// DHT22
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Pin
#define LED_RED   12
#define LED_BLUE  14
#define LED_GREEN 27
#define BUZZER    25
#define RELAY     26

WiFiClientSecure mqttNet;
PubSubClient client(mqttNet);

bool manualAC = false;

// Status notifikasi Telegram
String notifiedStatus = "";
unsigned long lastTelegram = 0;
bool telegramSentOnce = false;

// ---------- MQTT ----------
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  if (String(topic) == TOPIC_CTRL) {
    manualAC = (message == "1");
    Serial.println("AC Manual: " + String(manualAC ? "ON" : "OFF"));
  }
}

// Tidak memblokir program: coba sambung ulang paling cepat tiap 5 detik
void mqttConnect() {
  static unsigned long lastTry = 0;
  if (client.connected() || WiFi.status() != WL_CONNECTED) return;
  if (lastTry != 0 && millis() - lastTry < 5000) return;
  lastTry = millis();

  String clientId = "esp32-tempmon-" + String(random(0xffff), HEX);
  if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    client.subscribe(TOPIC_CTRL);
    Serial.println("MQTT terhubung");
  } else {
    Serial.printf("MQTT gagal, rc=%d\n", client.state());
  }
}

// ---------- Telegram ----------
bool sendTelegram(const char* text) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure tg;
  tg.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);

  String url = String("https://api.telegram.org/bot") + TG_TOKEN + "/sendMessage";
  if (!http.begin(tg, url)) return false;
  http.addHeader("Content-Type", "application/json");

  char body[400];
  snprintf(body, sizeof(body), "{\"chat_id\":\"%s\",\"text\":\"%s\"}", TG_CHAT_ID, text);
  int code = http.POST(body);
  http.end();

  Serial.printf("Telegram HTTP %d\n", code);
  return code == 200;
}

void handleTelegram(const String& status, float t, bool acState) {
  bool gapOk = !telegramSentOnce || (millis() - lastTelegram >= TG_MIN_GAP);
  bool statusChanged = (status != notifiedStatus);
  bool reminder = (status == "PANAS" && telegramSentOnce &&
                   millis() - lastTelegram >= TG_REMINDER);

  if (!((statusChanged && gapOk) || reminder)) return;

  char msg[200];
  if (notifiedStatus == "") {
    snprintf(msg, sizeof(msg), "Sistem aktif.\\nSuhu %.1f °C, status %s.", t, status.c_str());
  } else if (status == "PANAS") {
    snprintf(msg, sizeof(msg), "⚠️ Suhu PANAS: %.1f °C\\nRelay pendingin: %s", t, acState ? "ON" : "OFF");
  } else if (status == "DINGIN") {
    snprintf(msg, sizeof(msg), "❄️ Suhu DINGIN: %.1f °C", t);
  } else {
    snprintf(msg, sizeof(msg), "✅ Suhu kembali NORMAL: %.1f °C", t);
  }

  if (sendTelegram(msg)) {
    notifiedStatus = status;
    lastTelegram = millis();
    telegramSentOnce = true;
  } else {
    // gagal kirim: tunggu jeda dulu sebelum mencoba lagi
    lastTelegram = millis();
    telegramSentOnce = true;
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED gagal!"));
    for (;;) {
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(F("Temp Monitor"));
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    display.print(".");
    display.display();
    attempts++;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.println(F("WiFi OK!"));
    display.print(F("IP: "));
    display.println(WiFi.localIP());
    Serial.println(WiFi.localIP());
  } else {
    display.println(F("WiFi Gagal!"));
  }
  display.display();
  delay(2000);

  // setInsecure: koneksi tetap terenkripsi, tetapi sertifikat server tidak diverifikasi
  mqttNet.setInsecure();
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);
}

// ---------- Loop ----------
void loop() {
  mqttConnect();
  client.loop();

  static unsigned long lastRead = 0;
  if (millis() - lastRead < READ_INTERVAL) return;
  lastRead = millis();

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(t) || isnan(h)) {
    Serial.println("Gagal baca DHT22!");
    return;
  }

  String status = (t > TEMP_PANAS) ? "PANAS" : (t < TEMP_DINGIN) ? "DINGIN" : "NORMAL";
  bool acState = (t > TEMP_AC_ON) || manualAC;

  // OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("Suhu: %.1f C\n", t);
  display.printf("Hum : %.1f %%\n", h);
  display.printf("AC  : %s\n", acState ? "ON " : "OFF");
  display.printf("MQTT: %s\n", client.connected() ? "OK" : "putus");
  display.setCursor(80, 50);
  display.print(status);
  display.display();

  // LED
  digitalWrite(LED_RED,   status == "PANAS"  ? HIGH : LOW);
  digitalWrite(LED_BLUE,  status == "DINGIN" ? HIGH : LOW);
  digitalWrite(LED_GREEN, status == "NORMAL" ? HIGH : LOW);

  // Buzzer (tidak memblokir)
  if (status == "PANAS") tone(BUZZER, 1000, 200);

  // Relay
  digitalWrite(RELAY, acState ? HIGH : LOW);

  // Kirim data ke website lewat MQTT (retain agar website langsung dapat data terakhir)
  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"temp\":%.1f,\"hum\":%.1f,\"status\":\"%s\",\"relay\":%s}",
           t, h, status.c_str(), acState ? "true" : "false");
  if (client.connected()) client.publish(TOPIC_DATA, payload, true);

  // Notifikasi Telegram
  handleTelegram(status, t, acState);

  Serial.printf("Suhu: %.1f°C | Hum: %.1f%% | %s | AC: %s\n",
                t, h, status.c_str(), acState ? "ON" : "OFF");
}
