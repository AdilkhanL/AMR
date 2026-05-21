#include <WiFi.h>

const char* ssid = "TEST_ESP32";
const char* password = "12345678";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting WiFi...");

  WiFi.mode(WIFI_AP);
  bool result = WiFi.softAP(ssid, password);

  if (result) {
    Serial.println("✅ WiFi STARTED");
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("❌ WiFi FAILED");
  }
}

void loop() {
}