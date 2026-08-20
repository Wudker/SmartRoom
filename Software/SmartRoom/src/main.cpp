#include <Arduino.h>
#include <WiFi.h>
#include "MQTT.h"
#include "Pins.h"

void setup()
{
    pins_init();
    wifi_init();
    mqttBegin();
}

void loop()
{
    mqttLoop();
}