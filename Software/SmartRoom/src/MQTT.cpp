#include "MQTT.h"
#include "Pins.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "Secret_keys.h"

namespace {

const int mqttPort = 1883;

const char* topic_led_strip_set = "smartroom/workshop/led_strip/set";
const char* topic_led_strip_state = "smartroom/workshop/led_strip/state";
const char* topic_lamp_set = "smartroom/workshop/lamp/set";
const char* topic_lamp_state = "smartroom/workshop/lamp/state";
const char* topic_main_light_set = "smartroom/workshop/main_light/set";
const char* topic_main_light_state = "smartroom/workshop/main_light/state";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


void mqttCallback(char* topic, byte* payload, unsigned int length)
{
	Serial.print("MQTT received: ");
	Serial.print(topic);
	Serial.print(" -> ");

	for (unsigned int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
	}

	Serial.println();

	if (strcmp(topic, topic_led_strip_set) == 0) {
		bool isOn = length > 0 && payload[0] == '1';
		digitalWrite(LED_STRIP_PIN, isOn ? HIGH : LOW);
		mqttClient.publish(topic_led_strip_state, isOn ? "1" : "0");
	}
	else if (strcmp(topic, topic_lamp_set) == 0) {
		bool isOn = length > 0 && payload[0] == '1';
		digitalWrite(LAMP_PIN, isOn ? HIGH : LOW);
		mqttClient.publish(topic_lamp_state, isOn ? "1" : "0");
	}
	else if (strcmp(topic, topic_main_light_set) == 0) {
		bool isOn = length > 0 && payload[0] == '1';
		digitalWrite(MAIN_LIGHT_PIN, isOn ? HIGH : LOW);
		mqttClient.publish(topic_main_light_state, isOn ? "1" : "0");
	}
}

void connectMqtt()
{
	while (!mqttClient.connected()) {
		Serial.print("Connecting to MQTT... ");

		if (mqttClient.connect("SmartRoom_LED_STRIP")) {
			Serial.println("connected");

			mqttClient.subscribe(topic_led_strip_set);
			mqttClient.subscribe(topic_lamp_set);
			mqttClient.subscribe(topic_main_light_set);

		}
		else {
            if(WiFi.status() != WL_CONNECTED){
                Serial.println("WiFi not connected, reconnecting...");
                WiFi.reconnect();
                delay(1000);
            }
			else {
            Serial.print("failed, state = ");
			Serial.println(mqttClient.state());
			delay(1000); 
            }
		}
	}
}

}

void mqttBegin()
{
	mqttClient.setServer(mqttBroker_ip, mqttPort);
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
