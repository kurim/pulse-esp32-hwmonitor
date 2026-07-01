#include "mqtt_handler.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient netClient;
static PubSubClient mqtt(netClient);
static unsigned long lastReconnectAttempt = 0;
static unsigned long lastHistoryPush = 0;
static const unsigned long HISTORY_PUSH_INTERVAL_MS = 1000;

static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  // Erwartetes JSON-Payload-Format (Beispiel vom PC-Client):
  // {"cpu_load":42.5,"cpu_temp":58.0,"cpu_power":65.0,
  //  "gpu_load":12.0,"gpu_temp":40.0,"gpu_power":25.0}
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("MQTT JSON parse error: %s\n", err.c_str());
    return;
  }

  if (doc["cpu_load"].is<float>())  hwInfo.cpuLoad  = doc["cpu_load"];
  if (doc["cpu_temp"].is<float>())  hwInfo.cpuTemp  = doc["cpu_temp"];
  if (doc["cpu_power"].is<float>()) hwInfo.cpuPower = doc["cpu_power"];
  if (doc["gpu_load"].is<float>())  hwInfo.gpuLoad  = doc["gpu_load"];
  if (doc["gpu_temp"].is<float>())  hwInfo.gpuTemp  = doc["gpu_temp"];
  if (doc["gpu_power"].is<float>()) hwInfo.gpuPower = doc["gpu_power"];

  hwInfo.lastUpdateMs = millis();
  hwInfo.everReceived = true;

  // Verlauf höchstens 1x/Sek aktualisieren (unabhängig von Publish-Rate des PCs)
  if (millis() - lastHistoryPush >= HISTORY_PUSH_INTERVAL_MS) {
    lastHistoryPush = millis();
    cpuHistory.push(hwInfo.cpuLoad, hwInfo.cpuTemp, hwInfo.cpuPower);
    gpuHistory.push(hwInfo.gpuLoad, hwInfo.gpuTemp, hwInfo.gpuPower);
  }
}

static bool connectMqtt() {
  if (strlen(appConfig.mqtt_host) == 0) return false;

  String clientId = "CYD-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok;
  if (strlen(appConfig.mqtt_user) > 0) {
    ok = mqtt.connect(clientId.c_str(), appConfig.mqtt_user, appConfig.mqtt_pass);
  } else {
    ok = mqtt.connect(clientId.c_str());
  }
  if (ok) {
    mqtt.subscribe(appConfig.mqtt_topic);
    Serial.printf("MQTT verbunden, abonniert: %s\n", appConfig.mqtt_topic);
  }
  return ok;
}

namespace MqttHandler {

void begin() {
  if (strlen(appConfig.mqtt_host) == 0) return;
  mqtt.setServer(appConfig.mqtt_host, appConfig.mqtt_port);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);
}

void reconfigure() {
  mqtt.disconnect();
  begin();
}

void loop() {
  if (strlen(appConfig.mqtt_host) == 0 || !wifiConnected) {
    mqttConnected = false;
    return;
  }

  if (!mqtt.connected()) {
    mqttConnected = false;
    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();
      if (connectMqtt()) {
        mqttConnected = true;
      }
    }
  } else {
    mqttConnected = true;
    mqtt.loop();
  }
}

} // namespace MqttHandler
