#include "LEDs.h"
#include "Pins.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <cmath>

namespace {

constexpr uint16_t LED_COUNT = 12;
constexpr uint16_t LED_COUNT_BED = 20;
constexpr unsigned long EFFECT_TIME_MS = 5000UL;
constexpr unsigned long BED_FRAME_MS = 25UL;

Adafruit_NeoPixel strip_shelf(LED_COUNT, LED_PIN_1, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel strip_bed(LED_COUNT_BED, LED_BED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel diag_led(1, DIAG_LED_PIN, NEO_GRB + NEO_KHZ800);

bool led_shelf_active = true;
bool led_bed_active = true;
uint8_t led_shelf_brightness = 90;
uint8_t led_bed_brightness = 150;
uint32_t led_bed_color = 0xFFFFFF;
uint8_t breath_period_s = 6;

bool effect_enabled[SHELF_EFFECT_COUNT] = {
    true, true, true, true, true, true, false
};
ShelfEffect current_effect = ShelfEffect::COMET;
unsigned long last_effect_change = 0;
unsigned long last_bed_frame = 0;

volatile bool pulse_interrupt_flag = false;
unsigned long last_pulse_handled = 0;
constexpr unsigned long PULSE_DEBOUNCE_TIME = 2000UL;
unsigned long diag_led_off_time = 0;

int comet_pos = 0;
uint8_t rainbow_offset = 0;
int gate_stage = 0;
int gate_step = 0;
int gate_fade = 0;
int gate_hold = 0;
int arc_brightness = 25;
int arc_dir = 4;
int rep_phase = 0;
int rep_radius = 0;
int rep_flash = 0;
int rep_fade = 255;
int boot_phase = 0;
int boot_step = 0;
int boot_flash = 0;

void IRAM_ATTR pulse_isr()
{
    pulse_interrupt_flag = true;
}

uint32_t iron_red() { return strip_shelf.Color(255, 20, 0); }
uint32_t dark_red() { return strip_shelf.Color(45, 0, 0); }
uint32_t gold() { return strip_shelf.Color(255, 160, 0); }
uint32_t hot_gold() { return strip_shelf.Color(255, 220, 40); }
uint32_t arc_blue() { return strip_shelf.Color(0, 140, 255); }
uint32_t arc_white() { return strip_shelf.Color(255, 255, 255); }

uint8_t red(uint32_t rgb) { return (rgb >> 16) & 0xFF; }
uint8_t green(uint32_t rgb) { return (rgb >> 8) & 0xFF; }
uint8_t blue(uint32_t rgb) { return rgb & 0xFF; }

float breath_level(unsigned long now)
{
    if (breath_period_s == 0) {
        return 1.0f;
    }

    const unsigned long period_ms = breath_period_s * 1000UL;
    const float phase = static_cast<float>(now % period_ms) / period_ms;
    return 0.08f + 0.92f * (0.5f - 0.5f * cosf(2.0f * PI * phase));
}

uint32_t scaled_color(Adafruit_NeoPixel& strip, uint32_t rgb, float level)
{
    return strip.Color(
        static_cast<uint8_t>(red(rgb) * level),
        static_cast<uint8_t>(green(rgb) * level),
        static_cast<uint8_t>(blue(rgb) * level));
}

void clear_led_shelf()
{
    strip_shelf.clear();
    strip_shelf.show();
}

void clear_led_bed()
{
    strip_bed.clear();
    strip_bed.show();
}

void set_all(uint32_t color)
{
    for (uint16_t i = 0; i < LED_COUNT; ++i) {
        strip_shelf.setPixelColor(i, color);
    }
}

void set_pixel(int index, uint32_t color)
{
    if (index >= 0 && index < LED_COUNT) {
        strip_shelf.setPixelColor(index, color);
    }
}

void reset_effect_states()
{
    comet_pos = 0;
    rainbow_offset = 0;
    gate_stage = 0;
    gate_step = 0;
    gate_fade = 0;
    gate_hold = 0;
    arc_brightness = 25;
    arc_dir = 4;
    rep_phase = 0;
    rep_radius = 0;
    rep_flash = 0;
    rep_fade = 255;
    boot_phase = 0;
    boot_step = 0;
    boot_flash = 0;
}

uint8_t effect_index(ShelfEffect effect)
{
    return static_cast<uint8_t>(effect);
}

uint8_t enabled_effect_count()
{
    uint8_t count = 0;
    for (bool enabled : effect_enabled) {
        count += enabled ? 1 : 0;
    }
    return count;
}

void next_effect()
{
    const uint8_t current = effect_index(current_effect);

    for (uint8_t offset = 1; offset <= SHELF_EFFECT_COUNT; ++offset) {
        const uint8_t candidate = (current + offset) % SHELF_EFFECT_COUNT;
        if (effect_enabled[candidate]) {
            current_effect = static_cast<ShelfEffect>(candidate);
            break;
        }
    }

    last_effect_change = millis();
    reset_effect_states();

    if (led_shelf_active) {
        clear_led_shelf();
    }
}

unsigned long current_effect_time_ms()
{
    if (current_effect == ShelfEffect::BREATH && breath_period_s > 0) {
        return max(EFFECT_TIME_MS, breath_period_s * 1000UL);
    }
    return EFFECT_TIME_MS;
}

void update_bed_led(unsigned long now)
{
    if (!led_bed_active || now - last_bed_frame < BED_FRAME_MS) {
        return;
    }

    last_bed_frame = now;
    const uint32_t color = scaled_color(strip_bed, led_bed_color, breath_level(now));

    for (uint16_t i = 0; i < LED_COUNT_BED; ++i) {
        strip_bed.setPixelColor(i, color);
    }
    strip_bed.show();
}

void handle_pulse_interrupt()
{
    if (!pulse_interrupt_flag) {
        return;
    }

    noInterrupts();
    pulse_interrupt_flag = false;
    interrupts();

    const unsigned long now = millis();
    if (now - last_pulse_handled < PULSE_DEBOUNCE_TIME) {
        return;
    }

    last_pulse_handled = now;
    diag_led.setPixelColor(0, diag_led.Color(255, 255, 255));
    diag_led.show();
    diag_led_off_time = now + 50;
    set_led_shelf_enabled(!led_shelf_active);
}

void update_diag_led()
{
    if (diag_led_off_time > 0 && millis() >= diag_led_off_time) {
        diag_led.clear();
        diag_led.show();
        diag_led_off_time = 0;
    }
}

void smart_delay(unsigned long ms)
{
    const unsigned long start = millis();

    while (millis() - start < ms) {
        const unsigned long now = millis();
        handle_pulse_interrupt();
        update_diag_led();
        update_bed_led(now);

        if (!led_shelf_active) {
            return;
        }
    }
}

uint32_t wheel(byte position)
{
    if (position < 85) {
        return strip_shelf.Color(position * 3, 255 - position * 3, 0);
    }
    if (position < 170) {
        position -= 85;
        return strip_shelf.Color(255 - position * 3, 0, position * 3);
    }

    position -= 170;
    return strip_shelf.Color(0, position * 3, 255 - position * 3);
}

void effect_comet()
{
    constexpr int tail = 5;
    strip_shelf.clear();

    for (int t = 0; t < tail; ++t) {
        const int index = (comet_pos - t + LED_COUNT) % LED_COUNT;
        const uint8_t value = 255 - t * (255 / tail);
        set_pixel(index, t == 0 ? hot_gold() : strip_shelf.Color(value, 0, 0));
    }

    strip_shelf.show();
    smart_delay(50);
    comet_pos = (comet_pos + 1) % LED_COUNT;
}

void effect_rainbow()
{
    for (uint16_t i = 0; i < LED_COUNT; ++i) {
        set_pixel(i, wheel((i * 256 / LED_COUNT + rainbow_offset) & 255));
    }

    strip_shelf.show();
    ++rainbow_offset;
    smart_delay(20);
}

void effect_reactor_gate()
{
    constexpr int tail = 4;
    constexpr int max_step = LED_COUNT / 2;

    if (gate_stage == 0) {
        strip_shelf.clear();
        const int left_head = gate_step;
        const int right_head = LED_COUNT - 1 - gate_step;

        for (int t = 0; t < tail; ++t) {
            const int left = left_head - t;
            const int right = right_head + t;
            const uint32_t color = t == 0 ? hot_gold() :
                                   t == 1 ? iron_red() :
                                   strip_shelf.Color(150 - t * 30, 0, 0);
            set_pixel(left, color);
            set_pixel(right, color);
        }

        strip_shelf.show();
        smart_delay(65);

        if (++gate_step >= max_step) {
            gate_stage = 1;
            gate_fade = 0;
        }
    }
    else if (gate_stage == 1) {
        const int fade = min(gate_fade, 255);
        set_all(strip_shelf.Color(255, 80 + ((175 * fade) / 255), fade));
        set_pixel((LED_COUNT - 1) / 2, arc_white());
        set_pixel(LED_COUNT / 2, arc_white());
        strip_shelf.show();
        smart_delay(30);

        gate_fade += 15;
        if (gate_fade >= 255) {
            gate_stage = 2;
            gate_hold = 0;
        }
    }
    else {
        set_all(strip_shelf.Color(120, 220, 255));
        strip_shelf.show();
        smart_delay(80);

        if (++gate_hold >= 4) {
            gate_stage = 0;
            gate_step = 0;
            gate_fade = 0;
            gate_hold = 0;
        }
    }
}

void effect_arc_reactor()
{
    set_all(strip_shelf.Color(arc_brightness / 5, arc_brightness, arc_brightness));
    set_pixel((LED_COUNT - 1) / 2,
              strip_shelf.Color(arc_brightness / 2, arc_brightness, 255));
    set_pixel(LED_COUNT / 2,
              strip_shelf.Color(arc_brightness / 2, arc_brightness, 255));
    strip_shelf.show();
    smart_delay(35);

    arc_brightness += arc_dir;
    if (arc_brightness >= 210 || arc_brightness <= 25) {
        arc_dir = -arc_dir;
        arc_brightness = constrain(arc_brightness, 25, 210);
    }
}

void effect_repulsor_charge()
{
    const int left_center = (LED_COUNT - 1) / 2;
    const int right_center = LED_COUNT / 2;

    if (rep_phase == 0) {
        set_all(dark_red());
        for (int radius = 0; radius <= rep_radius; ++radius) {
            set_pixel(left_center - radius, gold());
            set_pixel(right_center + radius, gold());
        }
        set_pixel(left_center, arc_white());
        set_pixel(right_center, arc_white());
        strip_shelf.show();
        smart_delay(85);

        if (++rep_radius > LED_COUNT / 2) {
            rep_phase = 1;
            rep_flash = 0;
        }
    }
    else if (rep_phase == 1) {
        set_all(arc_blue());
        strip_shelf.show();
        smart_delay(75);

        if (++rep_flash >= 5) {
            rep_phase = 2;
            rep_fade = 255;
        }
    }
    else {
        set_all(strip_shelf.Color(rep_fade, rep_fade / 5, 0));
        strip_shelf.show();
        smart_delay(35);

        rep_fade -= 18;
        if (rep_fade <= 45) {
            rep_phase = 0;
            rep_radius = 0;
            rep_flash = 0;
            rep_fade = 255;
        }
    }
}

void effect_suit_boot_up()
{
    if (boot_phase == 0) {
        strip_shelf.clear();
        for (int i = 0; i < LED_COUNT; ++i) {
            if (i < boot_step) {
                set_pixel(i, iron_red());
            }
        }
        if (boot_step < LED_COUNT) {
            set_pixel(boot_step, hot_gold());
        }
        strip_shelf.show();
        smart_delay(80);

        if (++boot_step > LED_COUNT) {
            boot_phase = 1;
            boot_step = 0;
        }
    }
    else if (boot_phase == 1) {
        set_all(iron_red());
        set_pixel(boot_step, arc_white());
        set_pixel(boot_step - 1, hot_gold());
        set_pixel(boot_step - 2, gold());
        strip_shelf.show();
        smart_delay(55);

        if (++boot_step >= LED_COUNT + 3) {
            boot_phase = 2;
            boot_flash = 0;
        }
    }
    else {
        set_all(iron_red());
        strip_shelf.show();
        smart_delay(90);

        if (++boot_flash >= 4) {
            boot_phase = 0;
            boot_step = 0;
            boot_flash = 0;
        }
    }
}

void effect_breath()
{
    set_all(scaled_color(strip_shelf, led_bed_color, breath_level(millis())));
    strip_shelf.show();
    smart_delay(BED_FRAME_MS);
}

} // namespace

void leds_init()
{
    diag_led.begin();
    diag_led.clear();
    diag_led.show();

    strip_shelf.begin();
    strip_shelf.setBrightness(led_shelf_brightness);
    clear_led_shelf();

    strip_bed.begin();
    strip_bed.setBrightness(led_bed_brightness);
    clear_led_bed();

    last_effect_change = millis();
    const int interrupt_number = digitalPinToInterrupt(PULSE_PIN);

    if (interrupt_number == NOT_AN_INTERRUPT) {
        Serial.println("PULSE_PIN does not support interrupts.");
    }
    else {
        attachInterrupt(interrupt_number, pulse_isr, RISING);
    }
}

void leds_loop()
{
    const unsigned long now = millis();
    handle_pulse_interrupt();
    update_diag_led();
    update_bed_led(now);

    if (!led_shelf_active) {
        return;
    }

    if (now - last_effect_change >= current_effect_time_ms()) {
        next_effect();
    }

    switch (current_effect) {
        case ShelfEffect::COMET: effect_comet(); break;
        case ShelfEffect::RAINBOW: effect_rainbow(); break;
        case ShelfEffect::REACTOR_GATE: effect_reactor_gate(); break;
        case ShelfEffect::ARC_REACTOR: effect_arc_reactor(); break;
        case ShelfEffect::REPULSOR_CHARGE: effect_repulsor_charge(); break;
        case ShelfEffect::SUIT_BOOT: effect_suit_boot_up(); break;
        case ShelfEffect::BREATH: effect_breath(); break;
        case ShelfEffect::COUNT: break;
    }
}

void set_led_shelf_enabled(bool enabled)
{
    if (led_shelf_active == enabled) {
        return;
    }

    led_shelf_active = enabled;
    if (!enabled) {
        clear_led_shelf();
    }
    else {
        last_effect_change = millis();
        reset_effect_states();
    }
}

void set_led_bed_enabled(bool enabled)
{
    if (led_bed_active == enabled) {
        return;
    }

    led_bed_active = enabled;
    if (!enabled) {
        clear_led_bed();
    }
    else {
        last_bed_frame = 0;
    }
}

bool is_led_shelf_enabled() { return led_shelf_active; }
bool is_led_bed_enabled() { return led_bed_active; }

void set_led_shelf_brightness(uint8_t brightness)
{
    led_shelf_brightness = brightness;
    strip_shelf.setBrightness(brightness);
}

void set_led_bed_brightness(uint8_t brightness)
{
    led_bed_brightness = brightness;
    strip_bed.setBrightness(brightness);
}

uint8_t get_led_shelf_brightness() { return led_shelf_brightness; }
uint8_t get_led_bed_brightness() { return led_bed_brightness; }

void set_led_bed_color(uint32_t rgb) { led_bed_color = rgb & 0xFFFFFF; }
uint32_t get_led_bed_color() { return led_bed_color; }

void set_breath_period_s(uint8_t seconds)
{
    breath_period_s = seconds > 10 ? 10 : seconds;
}

uint8_t get_breath_period_s() { return breath_period_s; }

bool set_shelf_effect_enabled(ShelfEffect effect, bool enabled)
{
    const uint8_t index = effect_index(effect);
    if (index >= SHELF_EFFECT_COUNT) {
        return false;
    }
    if (effect_enabled[index] == enabled) {
        return true;
    }
    if (!enabled && enabled_effect_count() == 1) {
        return false;
    }

    effect_enabled[index] = enabled;
    if (!enabled && current_effect == effect) {
        next_effect();
    }
    return true;
}

bool is_shelf_effect_enabled(ShelfEffect effect)
{
    const uint8_t index = effect_index(effect);
    return index < SHELF_EFFECT_COUNT && effect_enabled[index];
}

ShelfEffect get_current_shelf_effect() { return current_effect; }

const char* get_shelf_effect_name(ShelfEffect effect)
{
    switch (effect) {
        case ShelfEffect::COMET: return "comet";
        case ShelfEffect::RAINBOW: return "rainbow";
        case ShelfEffect::REACTOR_GATE: return "reactor_gate";
        case ShelfEffect::ARC_REACTOR: return "arc_reactor";
        case ShelfEffect::REPULSOR_CHARGE: return "repulsor_charge";
        case ShelfEffect::SUIT_BOOT: return "suit_boot";
        case ShelfEffect::BREATH: return "breath";
        case ShelfEffect::COUNT: return "unknown";
    }
    return "unknown";
}
