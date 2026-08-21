#include <Arduino.h>
#include "Pins.h"
#include "LEDs.h"
#include "MQTT.h"

void setup()
{
    pins_init();
    wifi_init();
    ledsInit();
    mqttBegin();
}

void loop()
{
    mqttLoop();
    ledsLoop();
}
