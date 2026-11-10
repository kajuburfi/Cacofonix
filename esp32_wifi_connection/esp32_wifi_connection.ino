// AI Generated.
// Cross checked by @kajuburfi on 1st Nov, 2026

#include <WiFi.h>

// --- CONFIGURATION ---
const char *ssid = "UnoQ_Private";
const char *password = "arduino1234"; // Must be at least 8 characters long

// Define network structures using the mandatory C++ comma array format
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Initializing ESP32 Private Network ---");

  // Force the ESP32 to drop any active client roles and switch strictly to AP Mode
  WiFi.mode(WIFI_AP);

  // Apply the structural subnet layout profiles before launching the transmitter
  if (WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Local IP configuration applied successfully.");
  } else {
    Serial.println("Failed to apply network configuration configuration.");
  }

  // Launch the Access Point network
  // Parameters: (SSID, Password, Channel=1, Hidden=0, Max_Connections=4)
  if (WiFi.softAP(ssid, password, 1, 0, 4)) {
    Serial.println("Access Point successfully launched!");
    Serial.print("ESP32 Router IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Access Point Initialization Failed.");
  }
}

void loop() {
  // Print the number of active wireless clients hooked into our island network
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    Serial.print("Active wireless client stations connected: ");
    Serial.println(WiFi.softAPgetStationNum());
  }
}
