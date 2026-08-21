#include "MQTT.h"
#include "LEDs.h"
#include "Secret_keys.h"

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <cstring>

namespace {

constexpr int MQTT_PORT = 1883;

const char* TOPIC_LED1_SET       = "smartroom/workshop/led1/set";
const char* TOPIC_LED1_STATE     = "smartroom/workshop/led1/state";
const char* TOPIC_LED_BED_SET    = "smartroom/workshop/led_bed/set";
const char* TOPIC_LED_BED_STATE  = "smartroom/workshop/led_bed/state";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void publishLed1State()
{
    mqttClient.publish(TOPIC_LED1_STATE, isLed1Enabled() ? "1" : "0", true);
}

void publishLedBedState()
{
    mqttClient.publish(TOPIC_LED_BED_STATE, isLedBedEnabled() ? "1" : "0", true);
}

bool readOnOffPayload(const byte* payload, unsigned int length, bool& value)
{
    if (length != 1) {
        return false;
    }

    if (payload[0] == '1') {
        value = true;
        return true;
    }

    if (payload[0] == '0') {
        value = false;
        return true;
    }

    return false;
}

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    Serial.print("MQTT received: ");
    Serial.print(topic);
    Serial.print(" -> ");

    for (unsigned int i = 0; i < length; ++i) {
        Serial.print(static_cast<char>(payload[i]));
    }
    Serial.println();

    bool enabled = false;
    if (!readOnOffPayload(payload, length, enabled)) {
        Serial.println("Ignored MQTT payload. Expected 0 or 1.");
        return;
    }

    if (strcmp(topic, TOPIC_LED1_SET) == 0) {
        setLed1Enabled(enabled);
        publishLed1State();
    }
    else if (strcmp(topic, TOPIC_LED_BED_SET) == 0) {
        setLedBedEnabled(enabled);
        publishLedBedState();
    }
}

void connectMqtt()
{
    while (!mqttClient.connected()) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi not connected, reconnecting...");
            WiFi.reconnect();
            delay(1000);
            continue;
        }

        Serial.print("Connecting to MQTT... ");

        if (mqttClient.connect("SmartRoom_LED_STRIPS")) {
            Serial.println("connected");

            mqttClient.subscribe(TOPIC_LED1_SET);
            mqttClient.subscribe(TOPIC_LED_BED_SET);

            // STATE jest retained: po ponownym podłączeniu Node-RED od razu zna stan.
            publishLed1State();
            publishLedBedState();
        }
        else {
            Serial.print("failed, state = ");
            Serial.println(mqttClient.state());
            delay(1000);
        }
    }
}

} // namespace

void mqttBegin()
{
    mqttClient.setServer(mqttBroker_ip, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    connectMqtt();
}

void mqttLoop()
{
    if (!mqttClient.connected()) {
        connectMqtt();
    }

    mqttClient.loop();
}
