#include "SenseBoxAP.h"
#include "SoundSensor.h"

#include <math.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>

WiFiUDP udp;
IPAddress remoteIp;
bool udpEnabled = false;
String udpHost = "";
uint16_t udpPort = 0;
String udpFieldName = "loudness";
unsigned long lastUdpMeasurement = 0;
const unsigned long UDP_MEASUREMENT_INTERVAL = 1000; // ms

SoundSensor sensor;
const int OSM_MEASUREMENT_INTERVAL = 60000; // in milliseconds
SenseBoxAP configAP;
unsigned long lastOsmMeasurement = 0;

void setup() {
  Serial.begin(115200);
  configAP.begin("senseBox-Access-Point", "12345678");
  delay(1000);
  udpSetup();
  sensor.begin();
}

void loop() {
  configAP.handle();
  float log_min = 0.0;
  float log_max = 0.0;
  // Linear average of measurements in linear scale
  float lin_avg = 0.0;
  float lin_val = 0.0;
  unsigned long n_measurement = 0;

  if (millis() - lastUdpMeasurement >= UDP_MEASUREMENT_INTERVAL) {
    lastUdpMeasurement = millis();
    sensor.update();
  
    float val_avg = sensor.average();
    float val_min = sensor.min();
    float val_max = sensor.max();
      
    sendToUDP(val_avg);

    log_min = (n_measurement == 0) ? val_min : min(log_min, val_min);
    log_max = (n_measurement == 0) ? val_max : max(log_max, val_max);
    lin_val = pow(10, val_avg / 10);
    lin_avg = (n_measurement == 0) ? lin_val : lin_avg + lin_val;
    n_measurement++;
  }

  if (millis() - lastOsmMeasurement >= OSM_MEASUREMENT_INTERVAL) {
    lastOsmMeasurement = millis();
    if (n_measurement > 0) {
      float log_avg = 10 * log10(lin_avg / n_measurement);
      sendToOpenSenseMap(log_min, log_max, log_avg);

      n_measurement = 0;
    }
  }
}

static float roundToTwoDecimals(float v) {
  return roundf(v * 100.0f) / 100.0f;
}

  void sendToOpenSenseMap(float value1, float value2, float value3) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    Preferences prefs;
    prefs.begin("ids_storage", true);
    String senseboxId = prefs.getString("sensebox_id", "5f1c265de45a4f001cd2f000");
    String sensorId1 = prefs.getString("loudness_min", "default_min_id");
    String sensorId2 = prefs.getString("loudness_max", "default_max_id");
    String sensorId3 = prefs.getString("loudness_avg", "default_avg_id");
    prefs.end();
    
    String url = "https://ingress.opensensemap.org/boxes/" + senseboxId + "/data";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    String json = "[";
    json += "{\"sensor\": \"" + sensorId1 + "\", \"value\": " + String(roundToTwoDecimals(value1)) + "},";
    json += "{\"sensor\": \"" + sensorId2 + "\", \"value\": " + String(roundToTwoDecimals(value2)) + "},";
    json += "{\"sensor\": \"" + sensorId3 + "\", \"value\": " + String(roundToTwoDecimals(value3)) + "}";
    json += "]";
    
    Serial.println("URL: " + url);
    Serial.println("JSON: " + json);
    
    int httpResponseCode = http.POST(json);
    Serial.println("HTTP Antwortcode: " + String(httpResponseCode));
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Antwort: " + response);
      Serial.println("Daten erfolgreich gesendet");
    } else {
      Serial.println("Fehler beim Senden: " + String(httpResponseCode));
    }
    
    http.end();
  } else {
    Serial.println("Keine WLAN-Verbindung");
  }
}

static void udpSetup() {
  Serial.println("Lade UDP Konfiguration...");

  Preferences prefs;
  prefs.begin("udp_storage", true);
  bool enabled = prefs.isKey("udp_enable") ? prefs.getBool("udp_enable") : false;
  String host = prefs.getString("udp_host", "");
  String portStr = prefs.getString("udp_port", "0");
  uint16_t port = (uint16_t)portStr.toInt();
  String field = prefs.getString("udp_field_name", udpFieldName);
  prefs.end();

  udpEnabled = enabled;
  udpHost = host;
  udpPort = port;
  udpFieldName = field;

  // Resolve host (accept dotted IP or hostname)
  bool resolved = remoteIp.fromString(udpHost); // dotted decimal check
  if (!resolved) {
    if (!WiFi.hostByName(udpHost.c_str(), remoteIp)) {
      Serial.println("Fehler beim DNS lookup für Host: " + udpHost);
      return;
    }
  }

}

void sendToUDP(float value) {
  if (!udpEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (udpHost.length() == 0 || udpPort == 0) return;

  String payload = "{\"" + udpFieldName + "\":" + String(roundToTwoDecimals(value)) + "}";
  Serial.println("UDP -> " + udpHost + ":" + String(udpPort) + " payload: " + payload);

  // Send UDP packet
  udp.beginPacket(remoteIp, udpPort);
  udp.write((const uint8_t*)payload.c_str(), payload.length());
  udp.endPacket();
}

