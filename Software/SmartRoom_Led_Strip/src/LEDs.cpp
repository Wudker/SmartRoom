#include "LEDs.h"
#include "Pins.h"
#include "MQTT.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>


namespace {

constexpr uint16_t LED_COUNT     = 12;
constexpr uint16_t LED_COUNT_BED = 20;
constexpr uint8_t MODE_COUNT     = 6;
constexpr unsigned long MODE_TIME = 5000UL;

Adafruit_NeoPixel strip1(LED_COUNT, LED_PIN_1, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel stripBed(LED_COUNT_BED, LED_BED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel diagLed(1, DIAG_LED_PIN, NEO_GRB + NEO_KHZ800);

bool led_shelfActive = true;
bool led_bedActive = true;
bool led_controllerActive = true;
uint8_t led_shelf_brightness = 90;
uint8_t led_bed_brightness = 150;

uint8_t mode = 0;
unsigned long lastChange = 0;

volatile bool pulseInterruptFlag = false;
unsigned long lastPulseHandled = 0;
constexpr unsigned long PULSE_DEBOUNCE_TIME = 2000UL;

unsigned long diagLedOffTime = 0;

int gateStage = 0;
int gateStep = 0;
int gateFade = 0;
int gateHold = 0;

int repPhase = 0;
int repRadius = 0;
int repFlash = 0;
int repFade = 255;

int bootPhase = 0;
int bootStep = 0;
int bootFlash = 0;

void IRAM_ATTR pulse_isr()
{
    pulseInterruptFlag = true;
}

uint32_t iron_red()  { return strip1.Color(255, 20, 0); }
uint32_t dark_red()  { return strip1.Color(45, 0, 0); }
uint32_t gold()     { return strip1.Color(255, 160, 0); }
uint32_t hot_gold()  { return strip1.Color(255, 220, 40); }
uint32_t arc_blue()  { return strip1.Color(0, 140, 255); }
uint32_t arc_white() { return strip1.Color(255, 255, 255); }

void clear_led_shelf()
{
    strip1.clear();            // RGB = 0,0,0 dla wszystkich pikseli
    strip1.show();
}

void clear_led_bed()
{
    stripBed.clear();          // RGB = 0,0,0 dla wszystkich pikseli
    stripBed.show();
}

void set_all(uint32_t color)
{
    for (uint16_t i = 0; i < LED_COUNT; ++i) {
        strip1.setPixelColor(i, color);
    }
}

void set_pixel(int index, uint32_t color)
{
    if (index >= 0 && index < LED_COUNT) {
        strip1.setPixelColor(index, color);
    }
}

void reset_effect_states()
{
    gateStage = 0;
    gateStep = 0;
    gateFade = 0;
    gateHold = 0;

    repPhase = 0;
    repRadius = 0;
    repFlash = 0;
    repFade = 255;

    bootPhase = 0;
    bootStep = 0;
    bootFlash = 0;
}

void next_mode()
{
    mode = (mode + 1) % MODE_COUNT;
    lastChange = millis();
    reset_effect_states();

    if (led_shelfActive) {
        clear_led_shelf();
    }
}

void update_bed_led()
{
    if (!led_bedActive) {
        return;
    }

    const float brightnessFactor = (sin(millis() / 3000.0 * PI) + 1.0) / 2.0;
    const uint8_t whiteBrightness = 15 + static_cast<uint8_t>(brightnessFactor * 240.0);
    const uint32_t whiteColor = stripBed.Color(whiteBrightness, whiteBrightness, whiteBrightness);

    for (uint16_t i = 0; i < LED_COUNT_BED; ++i) {
        stripBed.setPixelColor(i, whiteColor);
    }

    stripBed.show();
}

void handle_pulse_interrupt()
{
    if (!pulseInterruptFlag) {
        return;
    }

    noInterrupts();
    pulseInterruptFlag = false;
    interrupts();

    const unsigned long now = millis();

    if (now - lastPulseHandled < PULSE_DEBOUNCE_TIME) {
        return;
    }

    lastPulseHandled = now;

    diagLed.setPixelColor(0, diagLed.Color(255, 255, 255));
    diagLed.show();
    diagLedOffTime = now + 50;

    // Fizyczny impuls przełącza tylko ledshelf. LED_BED pozostaje niezależny.
    set_led_shelf_enabled(!led_shelfActive);
}

void update_diag_led()
{
    if (diagLedOffTime > 0 && millis() >= diagLedOffTime) {
        diagLed.clear();
        diagLed.show();
        diagLedOffTime = 0;
    }
}

void smart_delay(unsigned long ms)
{
    const unsigned long start = millis();

    while (millis() - start < ms) {
        handle_pulse_interrupt();
        update_diag_led();
        update_bed_led();

        if (!led_shelfActive) {
            return;
        }
    }
}

uint32_t wheel(byte wheel_pos)
{
    if (wheel_pos < 85) {
        return strip1.Color(wheel_pos * 3, 255 - wheel_pos * 3, 0);
    }
    if (wheel_pos < 170) {
        wheel_pos -= 85;
        return strip1.Color(255 - wheel_pos * 3, 0, wheel_pos * 3);
    }

    wheel_pos -= 170;
    return strip1.Color(0, wheel_pos * 3, 255 - wheel_pos * 3);
}

void effect_comet()
{
    static int pos = 0;
    constexpr int tail = 5;

    strip1.clear();

    for (int t = 0; t < tail; ++t) {
        const int idx = (pos - t + LED_COUNT) % LED_COUNT;

        if (t == 0) {
            set_pixel(idx, hot_gold());
        }
        else {
            const uint8_t bright = 255 - t * (255 / tail);
            set_pixel(idx, strip1.Color(bright, 0, 0));
        }
    }

    strip1.show();
    smart_delay(50);
    pos = (pos + 1) % LED_COUNT;
}

void effect_rainbow()
{
    static uint8_t j = 0;

    for (uint16_t i = 0; i < LED_COUNT; ++i) {
        set_pixel(i, wheel((i * 256 / LED_COUNT + j) & 255));
    }

    strip1.show();
    ++j;
    smart_delay(20);
}

void effect_reactor_gate()
{
    constexpr int tail = 4;
    constexpr int maxStep = LED_COUNT / 2;

    if (gateStage == 0) {
        strip1.clear();

        const int leftHead = gateStep;
        const int rightHead = LED_COUNT - 1 - gateStep;

        for (int t = 0; t < tail; ++t) {
            const int leftIdx = leftHead - t;
            const int rightIdx = rightHead + t;

            if (leftIdx >= 0 && leftIdx < LED_COUNT) {
                if (t == 0) set_pixel(leftIdx, hot_gold());
                else if (t == 1) set_pixel(leftIdx, iron_red());
                else set_pixel(leftIdx, strip1.Color(150 - t * 30, 0, 0));
            }

            if (rightIdx >= 0 && rightIdx < LED_COUNT) {
                if (t == 0) set_pixel(rightIdx, hot_gold());
                else if (t == 1) set_pixel(rightIdx, iron_red());
                else set_pixel(rightIdx, strip1.Color(150 - t * 30, 0, 0));
            }
        }

        strip1.show();
        smart_delay(65);

        ++gateStep;
        if (gateStep >= maxStep) {
            gateStage = 1;
            gateFade = 0;
        }
    }
    else if (gateStage == 1) {
        const int fade = min(gateFade, 255);
        const uint8_t r = 255;
        const uint8_t g = 80 + ((175 * fade) / 255);
        const uint8_t b = fade;

        set_all(strip1.Color(r, g, b));

        const int c1 = (LED_COUNT - 1) / 2;
        const int c2 = LED_COUNT / 2;
        set_pixel(c1, arc_white());
        set_pixel(c2, arc_white());

        strip1.show();
        smart_delay(30);

        gateFade += 15;
        if (gateFade >= 255) {
            gateStage = 2;
            gateHold = 0;
        }
    }
    else {
        set_all(strip1.Color(120, 220, 255));
        strip1.show();
        smart_delay(80);

        ++gateHold;
        if (gateHold >= 4) {
            next_mode();
        }
    }
}

void effect_arc_reactor()
{
    static int brightness = 25;
    static int dir = 4;

    const uint8_t r = brightness / 5;
    const uint8_t g = brightness;
    const uint8_t b = brightness;
    set_all(strip1.Color(r, g, b));

    const int c1 = (LED_COUNT - 1) / 2;
    const int c2 = LED_COUNT / 2;
    set_pixel(c1, strip1.Color(brightness / 2, brightness, 255));
    set_pixel(c2, strip1.Color(brightness / 2, brightness, 255));

    strip1.show();
    smart_delay(35);

    brightness += dir;
    if (brightness >= 210 || brightness <= 25) {
        dir = -dir;
        brightness = constrain(brightness, 25, 210);
    }
}

void effect_repulsor_charge()
{
    const int leftCenter = (LED_COUNT - 1) / 2;
    const int rightCenter = LED_COUNT / 2;
    const int maxRadius = LED_COUNT / 2;

    if (repPhase == 0) {
        set_all(dark_red());

        for (int r = 0; r <= repRadius; ++r) {
            set_pixel(leftCenter - r, gold());
            set_pixel(rightCenter + r, gold());
        }

        set_pixel(leftCenter, arc_white());
        set_pixel(rightCenter, arc_white());

        strip1.show();
        smart_delay(85);

        ++repRadius;
        if (repRadius > maxRadius) {
            repPhase = 1;
            repFlash = 0;
        }
    }
    else if (repPhase == 1) {
        set_all(arc_blue());
        strip1.show();
        smart_delay(75);

        ++repFlash;
        if (repFlash >= 5) {
            repPhase = 2;
            repFade = 255;
        }
    }
    else {
        set_all(strip1.Color(repFade, repFade / 5, 0));
        strip1.show();
        smart_delay(35);

        repFade -= 18;
        if (repFade <= 45) {
            repPhase = 0;
            repRadius = 0;
            repFlash = 0;
            repFade = 255;
        }
    }
}

void effect_suit_boot_up()
{
    if (bootPhase == 0) {
        strip1.clear();

        for (int i = 0; i < LED_COUNT; ++i) {
            if (i < bootStep) {
                set_pixel(i, iron_red());
            }
        }

        if (bootStep < LED_COUNT) {
            set_pixel(bootStep, hot_gold());
        }

        strip1.show();
        smart_delay(80);

        ++bootStep;
        if (bootStep > LED_COUNT) {
            bootPhase = 1;
            bootStep = 0;
        }
    }
    else if (bootPhase == 1) {
        set_all(iron_red());

        const int scan = bootStep;
        set_pixel(scan, arc_white());
        set_pixel(scan - 1, hot_gold());
        set_pixel(scan - 2, gold());

        strip1.show();
        smart_delay(55);

        ++bootStep;
        if (bootStep >= LED_COUNT + 3) {
            bootPhase = 2;
            bootFlash = 0;
        }
    }
    else {
        set_all(iron_red());
        strip1.show();
        smart_delay(90);

        ++bootFlash;
        if (bootFlash >= 4) {
            bootPhase = 0;
            bootStep = 0;
            bootFlash = 0;
        }
    }
}

} // namespace

void leds_init()
{
    diagLed.begin();
    diagLed.clear();
    diagLed.show();

    strip1.begin();
    strip1.setBrightness(led_shelf_brightness);
    clear_led_shelf();

    stripBed.begin();
    stripBed.setBrightness(led_bed_brightness);
    clear_led_bed();

    lastChange = millis();

    const int interruptNumber = digitalPinToInterrupt(PULSE_PIN);
    if (interruptNumber == NOT_AN_INTERRUPT) {
        Serial.println("UWAGA: PULSE_PIN nie obsluguje attachInterrupt().");
    }
    else {
        attachInterrupt(interruptNumber, pulse_isr, RISING);
        Serial.println("Przerwanie PULSE_PIN aktywne.");
    }
}

void leds_loop()
{
    handle_pulse_interrupt();
    update_diag_led();
    update_bed_led();

    if (!led_shelfActive) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastChange > MODE_TIME) {
        next_mode();
    }

    switch (mode) {
        case 0: effect_comet(); break;
        case 1: effect_rainbow(); break;
        case 2: effect_reactor_gate(); break;
        case 3: effect_arc_reactor(); break;
        case 4: effect_repulsor_charge(); break;
        case 5: effect_suit_boot_up(); break;
    }
}

void set_led_shelf_enabled(bool enabled)
{
    if (led_shelfActive == enabled) {
        return;
    }

    led_shelfActive = enabled;

    if (!led_shelfActive) {
        clear_led_shelf();
    }
    else {
        lastChange = millis();
        reset_effect_states();
    }
}

void set_led_bed_enabled(bool enabled)
{
    if (led_bedActive == enabled) {
        return;
    }

    led_bedActive = enabled;

    if (!led_bedActive) {
        clear_led_bed();
    }
}

void set_led_bed_brightness(uint8_t brightness)
{
    led_bed_brightness = brightness;
    stripBed.setBrightness(led_bed_brightness);
}

void set_led_shelf_brightness(uint8_t brightness)
{
    led_shelf_brightness = brightness;
    strip1.setBrightness(led_shelf_brightness);
}

uint8_t get_led_shelf_brightness()
{
    return led_shelf_brightness;
}

uint8_t get_led_bed_brightness()
{
    return led_bed_brightness;
}

bool is_led_shelf_enabled()
{
    return led_shelfActive;
}

bool is_led_bed_enabled()
{
    return led_bedActive;
}

bool is_led_controller_enabled()
{
    return led_controllerActive;
}
