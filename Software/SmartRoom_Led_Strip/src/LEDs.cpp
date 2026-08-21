#include "LEDs.h"
#include "Pins.h"

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

bool led1Active = true;
bool ledBedActive = true;

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

void IRAM_ATTR pulseISR()
{
    pulseInterruptFlag = true;
}

uint32_t ironRed()  { return strip1.Color(255, 20, 0); }
uint32_t darkRed()  { return strip1.Color(45, 0, 0); }
uint32_t gold()     { return strip1.Color(255, 160, 0); }
uint32_t hotGold()  { return strip1.Color(255, 220, 40); }
uint32_t arcBlue()  { return strip1.Color(0, 140, 255); }
uint32_t arcWhite() { return strip1.Color(255, 255, 255); }

void clearLed1()
{
    strip1.clear();            // RGB = 0,0,0 dla wszystkich pikseli
    strip1.show();
}

void clearLedBed()
{
    stripBed.clear();          // RGB = 0,0,0 dla wszystkich pikseli
    stripBed.show();
}

void setAll(uint32_t color)
{
    for (uint16_t i = 0; i < LED_COUNT; ++i) {
        strip1.setPixelColor(i, color);
    }
}

void setPixel(int index, uint32_t color)
{
    if (index >= 0 && index < LED_COUNT) {
        strip1.setPixelColor(index, color);
    }
}

void resetEffectStates()
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

void nextMode()
{
    mode = (mode + 1) % MODE_COUNT;
    lastChange = millis();
    resetEffectStates();

    if (led1Active) {
        clearLed1();
    }
}

void updateBedLed()
{
    if (!ledBedActive) {
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

void handlePulseInterrupt()
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

    // Fizyczny impuls przełącza tylko LED1. LED_BED pozostaje niezależny.
    setLed1Enabled(!led1Active);
}

void updateDiagLed()
{
    if (diagLedOffTime > 0 && millis() >= diagLedOffTime) {
        diagLed.clear();
        diagLed.show();
        diagLedOffTime = 0;
    }
}

void smartDelay(unsigned long ms)
{
    const unsigned long start = millis();

    while (millis() - start < ms) {
        handlePulseInterrupt();
        updateDiagLed();
        updateBedLed();

        if (!led1Active) {
            return;
        }
    }
}

uint32_t Wheel(byte wheelPos)
{
    if (wheelPos < 85) {
        return strip1.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
    }
    if (wheelPos < 170) {
        wheelPos -= 85;
        return strip1.Color(255 - wheelPos * 3, 0, wheelPos * 3);
    }

    wheelPos -= 170;
    return strip1.Color(0, wheelPos * 3, 255 - wheelPos * 3);
}

void effectComet()
{
    static int pos = 0;
    constexpr int tail = 5;

    strip1.clear();

    for (int t = 0; t < tail; ++t) {
        const int idx = (pos - t + LED_COUNT) % LED_COUNT;

        if (t == 0) {
            setPixel(idx, hotGold());
        }
        else {
            const uint8_t bright = 255 - t * (255 / tail);
            setPixel(idx, strip1.Color(bright, 0, 0));
        }
    }

    strip1.show();
    smartDelay(50);
    pos = (pos + 1) % LED_COUNT;
}

void effectRainbow()
{
    static uint8_t j = 0;

    for (uint16_t i = 0; i < LED_COUNT; ++i) {
        setPixel(i, Wheel((i * 256 / LED_COUNT + j) & 255));
    }

    strip1.show();
    ++j;
    smartDelay(20);
}

void effectReactorGate()
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
                if (t == 0) setPixel(leftIdx, hotGold());
                else if (t == 1) setPixel(leftIdx, ironRed());
                else setPixel(leftIdx, strip1.Color(150 - t * 30, 0, 0));
            }

            if (rightIdx >= 0 && rightIdx < LED_COUNT) {
                if (t == 0) setPixel(rightIdx, hotGold());
                else if (t == 1) setPixel(rightIdx, ironRed());
                else setPixel(rightIdx, strip1.Color(150 - t * 30, 0, 0));
            }
        }

        strip1.show();
        smartDelay(65);

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

        setAll(strip1.Color(r, g, b));

        const int c1 = (LED_COUNT - 1) / 2;
        const int c2 = LED_COUNT / 2;
        setPixel(c1, arcWhite());
        setPixel(c2, arcWhite());

        strip1.show();
        smartDelay(30);

        gateFade += 15;
        if (gateFade >= 255) {
            gateStage = 2;
            gateHold = 0;
        }
    }
    else {
        setAll(strip1.Color(120, 220, 255));
        strip1.show();
        smartDelay(80);

        ++gateHold;
        if (gateHold >= 4) {
            nextMode();
        }
    }
}

void effectArcReactor()
{
    static int brightness = 25;
    static int dir = 4;

    const uint8_t r = brightness / 5;
    const uint8_t g = brightness;
    const uint8_t b = brightness;
    setAll(strip1.Color(r, g, b));

    const int c1 = (LED_COUNT - 1) / 2;
    const int c2 = LED_COUNT / 2;
    setPixel(c1, strip1.Color(brightness / 2, brightness, 255));
    setPixel(c2, strip1.Color(brightness / 2, brightness, 255));

    strip1.show();
    smartDelay(35);

    brightness += dir;
    if (brightness >= 210 || brightness <= 25) {
        dir = -dir;
        brightness = constrain(brightness, 25, 210);
    }
}

void effectRepulsorCharge()
{
    const int leftCenter = (LED_COUNT - 1) / 2;
    const int rightCenter = LED_COUNT / 2;
    const int maxRadius = LED_COUNT / 2;

    if (repPhase == 0) {
        setAll(darkRed());

        for (int r = 0; r <= repRadius; ++r) {
            setPixel(leftCenter - r, gold());
            setPixel(rightCenter + r, gold());
        }

        setPixel(leftCenter, arcWhite());
        setPixel(rightCenter, arcWhite());

        strip1.show();
        smartDelay(85);

        ++repRadius;
        if (repRadius > maxRadius) {
            repPhase = 1;
            repFlash = 0;
        }
    }
    else if (repPhase == 1) {
        setAll(arcBlue());
        strip1.show();
        smartDelay(75);

        ++repFlash;
        if (repFlash >= 5) {
            repPhase = 2;
            repFade = 255;
        }
    }
    else {
        setAll(strip1.Color(repFade, repFade / 5, 0));
        strip1.show();
        smartDelay(35);

        repFade -= 18;
        if (repFade <= 45) {
            repPhase = 0;
            repRadius = 0;
            repFlash = 0;
            repFade = 255;
        }
    }
}

void effectSuitBootUp()
{
    if (bootPhase == 0) {
        strip1.clear();

        for (int i = 0; i < LED_COUNT; ++i) {
            if (i < bootStep) {
                setPixel(i, ironRed());
            }
        }

        if (bootStep < LED_COUNT) {
            setPixel(bootStep, hotGold());
        }

        strip1.show();
        smartDelay(80);

        ++bootStep;
        if (bootStep > LED_COUNT) {
            bootPhase = 1;
            bootStep = 0;
        }
    }
    else if (bootPhase == 1) {
        setAll(ironRed());

        const int scan = bootStep;
        setPixel(scan, arcWhite());
        setPixel(scan - 1, hotGold());
        setPixel(scan - 2, gold());

        strip1.show();
        smartDelay(55);

        ++bootStep;
        if (bootStep >= LED_COUNT + 3) {
            bootPhase = 2;
            bootFlash = 0;
        }
    }
    else {
        setAll(ironRed());
        strip1.show();
        smartDelay(90);

        ++bootFlash;
        if (bootFlash >= 4) {
            bootPhase = 0;
            bootStep = 0;
            bootFlash = 0;
        }
    }
}

} // namespace

void ledsInit()
{
    diagLed.begin();
    diagLed.clear();
    diagLed.show();

    strip1.begin();
    strip1.setBrightness(90);
    clearLed1();

    stripBed.begin();
    stripBed.setBrightness(150);
    clearLedBed();

    lastChange = millis();

    const int interruptNumber = digitalPinToInterrupt(PULSE_PIN);
    if (interruptNumber == NOT_AN_INTERRUPT) {
        Serial.println("UWAGA: PULSE_PIN nie obsluguje attachInterrupt().");
    }
    else {
        attachInterrupt(interruptNumber, pulseISR, RISING);
        Serial.println("Przerwanie PULSE_PIN aktywne.");
    }
}

void ledsLoop()
{
    handlePulseInterrupt();
    updateDiagLed();
    updateBedLed();

    if (!led1Active) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastChange > MODE_TIME) {
        nextMode();
    }

    switch (mode) {
        case 0: effectComet(); break;
        case 1: effectRainbow(); break;
        case 2: effectReactorGate(); break;
        case 3: effectArcReactor(); break;
        case 4: effectRepulsorCharge(); break;
        case 5: effectSuitBootUp(); break;
    }
}

void setLed1Enabled(bool enabled)
{
    if (led1Active == enabled) {
        return;
    }

    led1Active = enabled;

    if (!led1Active) {
        clearLed1();
    }
    else {
        lastChange = millis();
        resetEffectStates();
    }
}

void setLedBedEnabled(bool enabled)
{
    if (ledBedActive == enabled) {
        return;
    }

    ledBedActive = enabled;

    if (!ledBedActive) {
        clearLedBed();
    }
}

bool isLed1Enabled()
{
    return led1Active;
}

bool isLedBedEnabled()
{
    return ledBedActive;
}
