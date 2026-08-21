#include <Arduino.h>
#include <WiFi.h>
#include "Pins.h"
#include "Secret_keys.h"

void pins_init()
{
    Serial.begin(115200);
    pinMode(PULSE_PIN, INPUT);
}

void wifi_init()
{
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(500);
    }

    Serial.println();
    Serial.println("Connected to WiFi");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
}
