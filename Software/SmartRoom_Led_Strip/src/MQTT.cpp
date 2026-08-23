#include "MQTT.h"
#include "LEDs.h"
#include "Secret_keys.h"

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <cstring>
#include <cstdlib>

namespace {

constexpr int MQTT_PORT = 1883;

const char* TOPIC_LED_shelf_SET             = "smartroom/workshop/led_shelf/set";
const char* TOPIC_LED_shelf_STATE           = "smartroom/workshop/led_shelf/state";
const char* TOPIC_LED_BED_SET               = "smartroom/workshop/led_bed/set";
const char* TOPIC_LED_BED_STATE             = "smartroom/workshop/led_bed/state";
const char* TOPIC_LED_CONTROLLER_STATE      = "smartroom/workshop/led_controller/state";
const char* TOPIC_LED_shelf_BRIGHTNESS      = "smartroom/workshop/led_shelf/brightness";
const char* TOPIC_LED_BED_BRIGHTNESS        = "smartroom/workshop/led_bed/brightness";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void publish_led_shelf_state()
{
    mqttClient.publish(TOPIC_LED_shelf_STATE, is_led_shelf_enabled() ? "1" : "0", true);
}

void publish_led_bed_state()
{
    mqttClient.publish(TOPIC_LED_BED_STATE, is_led_bed_enabled() ? "1" : "0", true);
}

void publish_led_controller_state()
{
    mqttClient.publish(TOPIC_LED_CONTROLLER_STATE, is_led_controller_enabled() ? "1" : "0", true);
}

bool read_on_off_payload(const byte* payload, unsigned int length, bool& value)
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

bool read_brightness_payload(const byte* payload, unsigned int length, uint8_t& brightness)
{
    if (length == 0 || length > 3) {
        return false;
    }

    char buffer[4] = {};
    memcpy(buffer, payload, length);

    char* end = nullptr;
    const unsigned long value = strtoul(buffer, &end, 10);
    if (*end != '\0' || value > 255) {
        return false;
    }

    brightness = static_cast<uint8_t>(value);
    return true;
}

void publish_led_bed_brightness()
{
    char brightness[4];
    snprintf(brightness, sizeof(brightness), "%u", get_led_bed_brightness());
    mqttClient.publish(TOPIC_LED_BED_BRIGHTNESS, brightness, true);
}

void publish_led_shelf_brightness()
{
    char brightness[4];
    snprintf(brightness, sizeof(brightness), "%u", get_led_shelf_brightness());
    mqttClient.publish(TOPIC_LED_shelf_BRIGHTNESS, brightness, true);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
    Serial.print("MQTT received: ");
    Serial.print(topic);
    Serial.print(" -> ");

    for (unsigned int i = 0; i < length; ++i) {
        Serial.print(static_cast<char>(payload[i]));
    }
    Serial.println();

    if (strcmp(topic, TOPIC_LED_shelf_BRIGHTNESS) == 0 ||
        strcmp(topic, TOPIC_LED_BED_BRIGHTNESS) == 0) {
        uint8_t brightness = 0;
        if (!read_brightness_payload(payload, length, brightness)) {
            Serial.println("Ignored brightness payload. Expected 0-255.");
            return;
        }

        if (strcmp(topic, TOPIC_LED_shelf_BRIGHTNESS) == 0) {
            set_led_shelf_brightness(brightness);
            publish_led_shelf_brightness();
        }
        else {
            set_led_bed_brightness(brightness);
            publish_led_bed_brightness();
        }
        return;
    }

    bool enabled = false;
    if (!read_on_off_payload(payload, length, enabled)) {
        Serial.println("Ignored MQTT payload. Expected 0 or 1.");
        return;
    }

    if (strcmp(topic, TOPIC_LED_shelf_SET) == 0) {
        set_led_shelf_enabled(enabled);
        publish_led_shelf_state();
    }
    else if (strcmp(topic, TOPIC_LED_BED_SET) == 0) {
        set_led_bed_enabled(enabled);
        publish_led_bed_state();
    }
}

void connect_mqtt()
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

            mqttClient.subscribe(TOPIC_LED_shelf_SET);
            mqttClient.subscribe(TOPIC_LED_BED_SET);
            mqttClient.subscribe(TOPIC_LED_shelf_BRIGHTNESS);
            mqttClient.subscribe(TOPIC_LED_BED_BRIGHTNESS);

            // STATE jest retained: po ponownym podłączeniu Node-RED od razu zna stan.
            publish_led_shelf_state();
            publish_led_bed_state();
            publish_led_controller_state();
            publish_led_shelf_brightness();
            publish_led_bed_brightness();
        }
        else {
            Serial.print("failed, state = ");
            Serial.println(mqttClient.state());
            delay(1000);
        }
    }
}

} // namespace

void mqtt_begin()
{
    mqttClient.setServer(mqttBroker_ip, MQTT_PORT);
    mqttClient.setCallback(mqtt_callback);
    connect_mqtt();
}

void mqtt_loop()
{
    if (!mqttClient.connected()) {
        connect_mqtt();
    }

    mqttClient.loop();
}
