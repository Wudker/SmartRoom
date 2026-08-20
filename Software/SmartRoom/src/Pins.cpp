#include <Arduino.h>
#include <WiFi.h>
#include "Pins.h"
#include "Secret_keys.h"

void pins_init(){
Serial.begin(115200);

    pinMode(LED_STRIP_PIN, OUTPUT);

    pinMode(LAMP_PIN, OUTPUT);
    pinMode(MAIN_LIGHT_PIN, OUTPUT);

    digitalWrite(LED_STRIP_PIN, LOW);
    digitalWrite(LAMP_PIN, LOW);
    digitalWrite(MAIN_LIGHT_PIN, LOW);


}

void wifi_init(){
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
}