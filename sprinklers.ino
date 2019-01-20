#include <MQTT.h>
#include <ArduinoOTA.h>

void mqttCb(String& topic, String& payload);

const char* MQTT_SERVER_IP = "192.168.0.4";  

const char* SWITCH_TOPIC_FORMAT = "/sprinklers/switch";
const char* SWITCH_CONFIRM_TOPIC_FORMAT = "/sprinklers/switchConfirm";
const char* SWITCH_AVAILABLE_TOPIC = "/sprinklers/available/";
String REBOOT_TOPIC = "/sprinklers/forceReboot";
const int FAILSAFE_TIME_LIMIT = 25; // Force a time limit (in munutes) just in case we loose the connection

/* Define in private.h
const char* WIFI_SSID 
const char* WIFI_PASSWORD 
const char* MQTT_USERNAME 
const char* MQTT_PASSWORD

*/
#include "private.h"

// Add the out pins you want to use 
const int switchOutputPins[] = {D0, D1, D2, D3};

WiFiClient net;
MQTTClient mqtt;

// Failsafe
unsigned long sprinklerStartTime;

void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    printf(". %d", 34);
  }
  
  Serial.println("Connected to wifi");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
}

void setupOTAUpdater() {
  ArduinoOTA.setHostname("Sprinklers");
  ArduinoOTA.begin();
  printf("OTA update system startup complete");
}

String buildTopic(int index, const char* topic) {
  String builtTopic = topic;
  builtTopic += String(index) + "/";
  return builtTopic;
}

void setupMQTT() {
  Serial.println("Connecting to MQTT");
  const String clientName = "sprinklers";

  mqtt.begin(MQTT_SERVER_IP, net);
  mqtt.onMessage(mqttCb);
  mqtt.setWill(SWITCH_AVAILABLE_TOPIC, "offline", true, 2);
  while (!mqtt.connect(clientName.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("");
  
  for (int i=0; i < sizeof(switchOutputPins)/sizeof(int); ++i) {
    const String topic = buildTopic(i, SWITCH_TOPIC_FORMAT);
    Serial.printf("Subscribing to topic %s\n", topic.c_str());
    mqtt.subscribe(topic);
  }

  mqtt.subscribe(REBOOT_TOPIC);

  // Send the available message
  Serial.printf("Sending the available message, %s\n", SWITCH_AVAILABLE_TOPIC);
  mqtt.publish(SWITCH_AVAILABLE_TOPIC, "online", true, 2);
}

void setup() {
  Serial.begin(115200);

  for (int i=0; i < sizeof(switchOutputPins)/sizeof(int); ++i) {
    pinMode(switchOutputPins[i], OUTPUT); 
    digitalWrite(switchOutputPins[i], LOW);
  }
  
  setupWifi();
  setupOTAUpdater();
  setupMQTT();
}

void publishConfirmation(const String& targetConfirmationTopic, const String& payload) {
    mqtt.publish(targetConfirmationTopic, payload, false, 0);
    Serial.printf("Sending confirmation message to: %s with payload %s\n", targetConfirmationTopic.c_str(), payload.c_str());
}

void checkFailsafe() {
  for (int i=0; i < sizeof(switchOutputPins)/sizeof(int); ++i) {
    if (digitalRead(switchOutputPins[i]) == HIGH) {
      // We are engaged
      if (sprinklerStartTime + (FAILSAFE_TIME_LIMIT * 60 * 1000) < millis()) {
         Serial.printf("FAILSAFE ENGAGED, SHUTTING DOWN PIN: %d\n", switchOutputPins[i]);
         digitalWrite(switchOutputPins[i], LOW);
      }
    }
  }
}

void checkConnection() {
  if (WiFi.status() != WL_CONNECTED || !mqtt.connected()) {
    setupWifi();
    setupOTAUpdater();
    setupMQTT();
  }
}


void loop() {
  mqtt.loop();
  ArduinoOTA.handle();
  delay(10); // WIFI is more stable with this
  checkFailsafe();
  checkConnection();
}

void mqttCb(String& topic, String& payload) {
  Serial.printf("Topic updated: %s, payload: %s\n", topic.c_str(), payload.c_str());
  if (topic == REBOOT_TOPIC) {
    ESP.restart();
  }
  
  for (int i=0; i < sizeof(switchOutputPins)/sizeof(int); ++i) {
    const String targetTopic = buildTopic(i, SWITCH_TOPIC_FORMAT);
    if (targetTopic == topic) {
      const String targetConfirmationTopic = buildTopic(i, SWITCH_CONFIRM_TOPIC_FORMAT);
      Serial.printf("Switch output %d to %s\n", switchOutputPins[i], payload.c_str());
      if (payload == "1") {
        sprinklerStartTime = millis();
        digitalWrite(switchOutputPins[i], HIGH);
        publishConfirmation(targetConfirmationTopic, "1");
      } else if (payload == "0") {
        digitalWrite(switchOutputPins[i], LOW);
        publishConfirmation(targetConfirmationTopic, "0");
      }
    }
  }
}
