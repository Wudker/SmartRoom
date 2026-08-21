#pragma once

#include <Arduino.h>

// Linie DATA pasków adresowalnych. Nie sterujemy już zasilaniem MOSFET-em.
constexpr uint8_t LED_PIN_1    = 5;
constexpr uint8_t LED_BED_PIN  = 6;
constexpr uint8_t DIAG_LED_PIN = 2;
constexpr uint8_t PULSE_PIN    = 8;

void pins_init();
void wifi_init();
