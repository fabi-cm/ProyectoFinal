#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include "SensorHumedad.h"
#include "ActuadorRiego.h"
#include "SensorNivelAgua.h"
#include "config.h"

#define SOIL_SENSOR_PIN 39
#define RELAY_PIN 4
#define WATER_LEVEL_SENSOR_PIN 36
#define RESET_BUTTON_PIN 0 // Opcional: botón para borrar configuración WiFi

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
SensorHumedad soilSensor(SOIL_SENSOR_PIN);
ActuadorRiego waterPump(RELAY_PIN);
SensorNivelAgua waterLevelSensor(WATER_LEVEL_SENSOR_PIN, -1, 2000);

bool waterAlertSent = false;
unsigned long lastWaterAlertTime = 0;
const unsigned long waterAlertInterval = 3600000; // 1 hora

String currentMode = "AUTOMATIC";

void publishShadowState()
{
  StaticJsonDocument<512> doc;
  doc["state"]["reported"]["humidity"] = soilSensor.leerHumedad();
  doc["state"]["reported"]["pump"] = digitalRead(RELAY_PIN) ? "ON" : "OFF";
  doc["state"]["reported"]["water_level"] = waterLevelSensor.leerNivel();
  doc["state"]["reported"]["needs_refill"] = waterLevelSensor.necesitaRecarga();
  doc["state"]["reported"]["mode"] = currentMode;

  if (waterLevelSensor.necesitaRecarga() &&
      (millis() - lastWaterAlertTime > waterAlertInterval || !waterAlertSent))
  {
    doc["state"]["reported"]["alert"] = "LOW_WATER_LEVEL";
    waterAlertSent = true;
    lastWaterAlertTime = millis();
  }
  else if (!waterLevelSensor.necesitaRecarga() && waterAlertSent)
  {
    doc["state"]["reported"]["alert"] = "NORMAL_WATER_LEVEL";
    waterAlertSent = false;
  }

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  mqttClient.publish(SHADOW_UPDATE, jsonBuffer);
  Serial.println("📤 Shadow state published.");
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("📩 Message received on topic: ");
  Serial.println(topic);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error)
  {
    Serial.print("⚠️ JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  if (String(topic) == SHADOW_DELTA)
  {
    if (doc["state"].containsKey("pump"))
    {
      bool state = doc["state"]["pump"] == "ON";
      digitalWrite(RELAY_PIN, state ? HIGH : LOW);
      Serial.println("Pump " + String(state ? "ENABLED" : "DISABLED"));
    }

    if (doc["state"].containsKey("mode"))
    {
      currentMode = doc["state"]["mode"].as<String>();
      Serial.println("Mode updated to: " + currentMode);
    }

    publishShadowState();
  }
}

void reconnectMQTT()
{
  int attempts = 0;
  const int maxAttempts = 5;

  while (!mqttClient.connected() && attempts < maxAttempts)
  {
    Serial.print("🔌 Connecting to MQTT... ");
    if (mqttClient.connect(CLIENT_ID))
    {
      Serial.println("✅ Connected!");
      mqttClient.subscribe(SHADOW_DELTA);
      publishShadowState();
      break;
    }
    else
    {
      Serial.print("❌ Failed, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
      attempts++;
    }
  }

  if (attempts == maxAttempts)
  {
    Serial.println("💥 MQTT connection failed. Restarting...");
    ESP.restart();
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180); // 3 min timeout

  if (digitalRead(RESET_BUTTON_PIN) == LOW)
  {
    Serial.println("🔁 Reset button pressed: clearing WiFi credentials");
    wm.resetSettings();
    ESP.restart();
  }

  Serial.println("🌐 Connecting to WiFi...");
  if (!wm.autoConnect("SmartPot-Setup"))
  {
    Serial.println("⚠️ Failed to connect via WiFiManager. Restarting...");
    ESP.restart();
  }

  Serial.println("✅ WiFi connected!");
  Serial.println("IP address: " + WiFi.localIP().toString());

  secureClient.setCACert(AWS_ROOT_CA);
  secureClient.setCertificate(DEVICE_CERT);
  secureClient.setPrivateKey(PRIVATE_KEY);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(callback);
}

void loop()
{
  if (!mqttClient.connected())
  {
    reconnectMQTT();
  }

  mqttClient.loop();

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5000)
  {
    lastUpdate = millis();

    float humidity = soilSensor.leerHumedad();
    bool needsWater = waterLevelSensor.necesitaRecarga();
    bool pumpOn = digitalRead(RELAY_PIN);

    if (currentMode == "AUTOMATIC")
    {
      if (humidity < 30 && !needsWater && !pumpOn)
      {
        waterPump.comenzarRiego();
        Serial.println("🌿 Auto-irrigation ON: low humidity.");
      }
      else if ((humidity >= 40 || needsWater) && pumpOn)
      {
        waterPump.detenerRiego();
        Serial.println(needsWater ? "💧 Irrigation stopped: no water." : "✅ Irrigation stopped: humidity OK.");
      }
    }
    else
    {
      Serial.println("🛠 Manual mode active. No automatic irrigation.");
    }

    if (needsWater && pumpOn)
    {
      waterPump.detenerRiego();
      Serial.println("❌ Safety shutdown: water tank is empty!");
    }

    publishShadowState();

    Serial.println("===== System Report =====");
    Serial.print("Mode: ");
    Serial.println(currentMode);
    Serial.print("Soil Humidity: ");
    Serial.print(humidity);
    Serial.println("%");
    Serial.print("Pump: ");
    Serial.println(pumpOn ? "ON" : "OFF");
    Serial.print("Water Level (raw): ");
    Serial.println(waterLevelSensor.leerNivel());
    Serial.print("Float State: ");
    Serial.println(waterLevelSensor.estadoActual() ? "HIGH" : "LOW");
    Serial.print("Approx Voltage: ");
    Serial.println((float)analogRead(WATER_LEVEL_SENSOR_PIN) * 3.3 / 4095.0);
    Serial.println("=========================");
  }
}
