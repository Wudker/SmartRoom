#include <Arduino.h>
#include "Pins.h"
#include "LEDs.h"
#include "MQTT.h"

void setup()
{
    pins_init();
    wifi_init();
    leds_init();
    mqtt_begin();
}

void loop()
{
    mqtt_loop();
    leds_loop();
}
