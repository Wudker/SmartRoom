#pragma once

#include <Arduino.h>

void leds_init();
void leds_loop();

void set_led_shelf_enabled(bool enabled);
void set_led_bed_enabled(bool enabled);
void set_led_controller_enabled(bool enabled);
void set_led_shelf_brightness(uint8_t brightness);
uint8_t get_led_shelf_brightness();
void set_led_bed_brightness(uint8_t brightness);
uint8_t get_led_bed_brightness();

bool is_led_shelf_enabled();
bool is_led_bed_enabled();
bool is_led_controller_enabled();
