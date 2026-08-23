#pragma once

#include <Arduino.h>

enum class ShelfEffect : uint8_t {
    COMET,
    RAINBOW,
    REACTOR_GATE,
    ARC_REACTOR,
    REPULSOR_CHARGE,
    SUIT_BOOT,
    BREATH,
    COUNT
};

constexpr uint8_t SHELF_EFFECT_COUNT = static_cast<uint8_t>(ShelfEffect::COUNT);

void leds_init();
void leds_loop();

void set_led_shelf_enabled(bool enabled);
void set_led_bed_enabled(bool enabled);
bool is_led_shelf_enabled();
bool is_led_bed_enabled();

void set_led_shelf_brightness(uint8_t brightness);
void set_led_bed_brightness(uint8_t brightness);
uint8_t get_led_shelf_brightness();
uint8_t get_led_bed_brightness();

void set_led_bed_color(uint32_t rgb);
uint32_t get_led_bed_color();

void set_breath_period_s(uint8_t seconds);
uint8_t get_breath_period_s();

bool set_shelf_effect_enabled(ShelfEffect effect, bool enabled);
bool is_shelf_effect_enabled(ShelfEffect effect);
ShelfEffect get_current_shelf_effect();
const char* get_shelf_effect_name(ShelfEffect effect);
