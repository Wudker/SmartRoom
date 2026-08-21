#include <Arduino.h>
#include <WiFi.h>
#include "MQTT.h"
#include "Pins.h"
#include <Adafruit_NeoPixel.h>

#define LED_PIN_1   5
#define LED_PIN_2   20 //not in use
#define LED_BED     6
#define DIAG_LED_PIN 2

    // Konfiguracja liczby diod dla poszczególnych sekcji
#define LED_COUNT       12  // Liczba diod w pasku 1 oraz pasku 2
#define LED_COUNT_BED   20  // Liczba diod w pasku BED (ustaw własną wartość)

#define PULSE_PIN   8

// Inicjalizacja trzech osobnych pasków LED
Adafruit_NeoPixel strip1(LED_COUNT, LED_PIN_1, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LED_COUNT, LED_PIN_2, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel stripBed(LED_COUNT_BED, LED_BED, NEO_GRB + NEO_KHZ800); // Często paski mają układ GRB zamiast RGB
Adafruit_NeoPixel diagLed(1, DIAG_LED_PIN, NEO_GRB + NEO_KHZ800);

#define MODE_COUNT 6
#define MODE_TIME  5000UL

uint8_t mode = 0;
unsigned long lastChange = 0;

// -------- Obsługa aktywacji LED --------
bool ledsActive = true;

// Flaga ustawiana w przerwaniu
volatile bool pulseInterruptFlag = false;

// Debounce obsługiwany poza przerwaniem
unsigned long lastPulseHandled = 0;
const unsigned long pulseDebounceTime = 2000;

// Diagnostyczna dioda bez delay()
unsigned long diagLedOffTime = 0;

// -------- Zmienne efektu "wrota" --------
int gateStage = 0;
int gateStep = 0;
int gateFade = 0;
int gateHold = 0;

// -------- Zmienne efektu Repulsor Charge --------
int repPhase = 0;
int repRadius = 0;
int repFlash = 0;
int repFade = 255;

// -------- Zmienne efektu Suit Boot-Up --------
int bootPhase = 0;
int bootStep = 0;
int bootFlash = 0;

// -------- Przerwanie od sygnału włączenia --------
void IRAM_ATTR pulseISR() {
  pulseInterruptFlag = true;
}

// -------- Kolory pomocnicze (dla strip1, bo są identyczne) --------
uint32_t ironRed() { return strip1.Color(255, 20, 0); }
uint32_t darkRed() { return strip1.Color(45, 0, 0); }
uint32_t gold() { return strip1.Color(255, 160, 0); }
uint32_t hotGold() { return strip1.Color(255, 220, 40); }
uint32_t arcBlue() { return strip1.Color(0, 140, 255); }
uint32_t arcWhite() { return strip1.Color(255, 255, 255); }

// Funkcja czyszcząca bufory obu głównych pasków
void clearMainStrips() {
  strip1.clear();
  strip2.clear();
}

// Funkcja wysyłająca dane do obu głównych pasków na raz
void showMainStrips() {
  strip1.show();
  strip2.show();
}

// Ustawianie koloru na obu paskach jednocześnie
void setAll(uint32_t color) {
  for (int i = 0; i < LED_COUNT; i++) {
    strip1.setPixelColor(i, color);
    strip2.setPixelColor(i, color);
  }
}

// Ustawianie konkretnego piksela na obu paskach
void setPixelDuplicate(int index, uint32_t color) {
  if (index >= 0 && index < LED_COUNT) {
    strip1.setPixelColor(index, color);
    strip2.setPixelColor(index, color);
  }
}

void resetEffectStates() {
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

void nextMode() {
  mode = (mode + 1) % MODE_COUNT;
  lastChange = millis();
  resetEffectStates();

  if (ledsActive) {
    clearMainStrips();
    showMainStrips();
  }
}

// Obsługa paska BED - płynne, wolne oddychanie światłem białym
void updateBedLed() {
  // ZMODYFIKOWANE: Jeśli ledy są wyłączone, nic nie rób (wygaszenie nastąpiło w handlePulseInterrupt)
  if (!ledsActive) {
    return;
  }

  // Obliczanie jasności przy użyciu funkcji sinus na podstawie czasu systemowego (millis)
  // Liczba 3000 definiuje prędkość oddychania (wyższa wartość = wolniej)
  float brightnessFactor = (sin(millis() / 3000.0 * PI) + 1.0) / 2.0; 
  
  // Zakres jasności od minimalnego żarzenia (15) do pełnej mocy (255)
  uint8_t whiteBrightness = 15 + (brightnessFactor * 240); 
  
  uint32_t whiteColor = stripBed.Color(whiteBrightness, whiteBrightness, whiteBrightness);

  for (int i = 0; i < LED_COUNT_BED; i++) {
    stripBed.setPixelColor(i, whiteColor);
  }
  stripBed.show();
}

// -------- Obsługa flagi z przerwania --------
void handlePulseInterrupt() {
  if (!pulseInterruptFlag) {
    return;
  }

  noInterrupts();
  pulseInterruptFlag = false;
  interrupts();

  unsigned long now = millis();

  if (now - lastPulseHandled < pulseDebounceTime) {
    return;
  }

  lastPulseHandled = now;

  diagLed.setPixelColor(0, diagLed.Color(255, 255, 255));
  diagLed.show();
  diagLedOffTime = now + 50;

  ledsActive = !ledsActive;

  if (!ledsActive) {
    // Całkowite czyszczenie i gaszenie wszystkich pasków
    clearMainStrips();
    showMainStrips();
    
    stripBed.clear();
    stripBed.show();
  } else {
    lastChange = millis();
    resetEffectStates();
  }
}

void updateDiagLed() {
  if (diagLedOffTime > 0 && millis() >= diagLedOffTime) {
    diagLed.clear();
    diagLed.show();
    diagLedOffTime = 0;
  }
}

// -------- Delay z obsługą przerwania oraz paska pod łóżkiem --------
void smartDelay(unsigned long ms) {
  unsigned long start = millis();

  while (millis() - start < ms) {
    handlePulseInterrupt();
    updateDiagLed();
    updateBedLed(); // Aktualizujemy jasność łóżka podczas trwania przerw/pętli

    if (!ledsActive) {
      return;
    }
  }
}

// -------- Koło barw --------
uint32_t Wheel(byte WheelPos) {
  if (WheelPos < 85) {
    return strip1.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip1.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip1.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

// -------- Efekt 1: Złoto-Czerwona Kometa --------
void effectComet() {
  static int pos = 0;
  const int tail = 5;

  clearMainStrips();

  for (int t = 0; t < tail; t++) {
    int idx = (pos - t + LED_COUNT) % LED_COUNT;

    if (t == 0) {
      setPixelDuplicate(idx, hotGold());
    } else {
      uint8_t bright = 255 - t * (255 / tail);
      setPixelDuplicate(idx, strip1.Color(bright, 0, 0));
    }
  }

  showMainStrips();
  smartDelay(50);

  pos = (pos + 1) % LED_COUNT;
}

// -------- Efekt 2: Tęcza --------
void effectRainbow() {
  static uint8_t j = 0;

  for (int i = 0; i < LED_COUNT; i++) {
    setPixelDuplicate(i, Wheel((i * 256 / LED_COUNT + j) & 255));
  }

  showMainStrips();
  j++;
  smartDelay(20);
}

// -------- Efekt 3: Wrota Reaktora --------
void effectReactorGate() {
  const int tail = 4;
  const int maxStep = LED_COUNT / 2;

  if (gateStage == 0) {
    clearMainStrips();

    int leftHead = gateStep;
    int rightHead = LED_COUNT - 1 - gateStep;

    for (int t = 0; t < tail; t++) {
      int leftIdx = leftHead - t;
      int rightIdx = rightHead + t;

      if (leftIdx >= 0 && leftIdx < LED_COUNT) {
        if (t == 0) setPixelDuplicate(leftIdx, hotGold());
        else if (t == 1) setPixelDuplicate(leftIdx, ironRed());
        else {
          uint8_t r = 150 - t * 30;
          setPixelDuplicate(leftIdx, strip1.Color(r, 0, 0));
        }
      }

      if (rightIdx >= 0 && rightIdx < LED_COUNT) {
        if (t == 0) setPixelDuplicate(rightIdx, hotGold());
        else if (t == 1) setPixelDuplicate(rightIdx, ironRed());
        else {
          uint8_t r = 150 - t * 30;
          setPixelDuplicate(rightIdx, strip1.Color(r, 0, 0));
        }
      }
    }

    showMainStrips();
    smartDelay(65);

    gateStep++;

    if (gateStep >= maxStep) {
      gateStage = 1;
      gateFade = 0;
    }
  }

  else if (gateStage == 1) {
    int fade = gateFade;
    if (fade > 255) fade = 255;

    for (int i = 0; i < LED_COUNT; i++) {
      uint8_t r = 255;
      uint8_t g = 80 + ((175 * fade) / 255);
      uint8_t b = fade;

      setAll(strip1.Color(r, g, b));
    }

    int c1 = (LED_COUNT - 1) / 2;
    int c2 = LED_COUNT / 2;

    setPixelDuplicate(c1, arcWhite());
    setPixelDuplicate(c2, arcWhite());

    showMainStrips();
    smartDelay(30);

    gateFade += 15;

    if (gateFade >= 255) {
      gateStage = 2;
      gateHold = 0;
    }
  }

  else if (gateStage == 2) {
    setAll(strip1.Color(120, 220, 255));
    showMainStrips();
    smartDelay(80);

    gateHold++;

    if (gateHold >= 4) {
      nextMode();
    }
  }
}

// -------- Efekt 4: Arc Reactor Pulse --------
void effectArcReactor() {
  static int brightness = 25;
  static int dir = 4;

  for (int i = 0; i < LED_COUNT; i++) {
    uint8_t r = brightness / 5;
    uint8_t g = brightness;
    uint8_t b = brightness;

    setAll(strip1.Color(r, g, b));
  }

  int c1 = (LED_COUNT - 1) / 2;
  int c2 = LED_COUNT / 2;

  setPixelDuplicate(c1, strip1.Color(brightness / 2, brightness, 255));
  setPixelDuplicate(c2, strip1.Color(brightness / 2, brightness, 255));

  showMainStrips();
  smartDelay(35);

  brightness += dir;

  if (brightness >= 210 || brightness <= 25) {
    dir = -dir;
    if (brightness > 210) brightness = 210;
    if (brightness < 25) brightness = 25;
  }
}

// -------- Efekt 5: Repulsor Charge --------
void effectRepulsorCharge() {
  int leftCenter = (LED_COUNT - 1) / 2;
  int rightCenter = LED_COUNT / 2;
  int maxRadius = LED_COUNT / 2;

  if (repPhase == 0) {
    setAll(darkRed());

    for (int r = 0; r <= repRadius; r++) {
      int l = leftCenter - r;
      int rr = rightCenter + r;

      setPixelDuplicate(l, gold());
      setPixelDuplicate(rr, gold());
    }

    setPixelDuplicate(leftCenter, arcWhite());
    setPixelDuplicate(rightCenter, arcWhite());

    showMainStrips();
    smartDelay(85);

    repRadius++;

    if (repRadius > maxRadius) {
      repPhase = 1;
      repFlash = 0;
    }
  }

  else if (repPhase == 1) {
    setAll(arcBlue());
    showMainStrips();
    smartDelay(75);

    repFlash++;

    if (repFlash >= 5) {
      repPhase = 2;
      repFade = 255;
    }
  }

  else if (repPhase == 2) {
    uint8_t r = repFade;
    uint8_t g = repFade / 5;
    uint8_t b = 0;

    setAll(strip1.Color(r, g, b));
    showMainStrips();
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

// -------- Efekt 6: Mark Suit Boot-Up --------
void effectSuitBootUp() {
  if (bootPhase == 0) {
    clearMainStrips();

    for (int i = 0; i < LED_COUNT; i++) {
      if (i < bootStep) {
        setPixelDuplicate(i, ironRed());
      }
    }

    if (bootStep < LED_COUNT) {
      setPixelDuplicate(bootStep, hotGold());
    }

    showMainStrips();
    smartDelay(80);

    bootStep++;

    if (bootStep > LED_COUNT) {
      bootPhase = 1;
      bootStep = 0;
    }
  }

  else if (bootPhase == 1) {
    setAll(ironRed());

    int scan = bootStep;

    setPixelDuplicate(scan, arcWhite());
    setPixelDuplicate(scan - 1, hotGold());
    setPixelDuplicate(scan - 2, gold());

    showMainStrips();
    smartDelay(55);

    bootStep++;

    if (bootStep >= LED_COUNT + 3) {
      bootPhase = 2;
      bootFlash = 0;
    }
  }

  else if (bootPhase == 2) {
    setAll(ironRed());
    showMainStrips();
    smartDelay(90);

    bootFlash++;

    if (bootFlash >= 4) {
      bootPhase = 0;
      bootStep = 0;
      bootFlash = 0;
    }
  }
}


void setup()
{
    pins_init();
    wifi_init();
    mqttBegin();

  pinMode(PULSE_PIN, INPUT);

  diagLed.begin();
  diagLed.clear();
  diagLed.show();

  // Inicjalizacja wszystkich trzech pasków
  strip1.begin();
  strip1.setBrightness(90);
  
  strip2.begin();
  strip2.setBrightness(90);

  stripBed.begin();
  stripBed.setBrightness(150); // Wyższa jasność domyślna dla łóżka (możesz zmniejszyć)

  clearMainStrips();
  showMainStrips();
  
  stripBed.clear();
  stripBed.show();

  lastChange = millis();

  int interruptNumber = digitalPinToInterrupt(PULSE_PIN);

  if (interruptNumber == NOT_AN_INTERRUPT) {
    Serial.println("UWAGA: Ten pin nie obsluguje attachInterrupt(). Zmien PULSE_PIN np. na 2 albo 3.");
  } else {
    attachInterrupt(interruptNumber, pulseISR, RISING);
    Serial.println("Przerwanie PULSE_PIN aktywne.");
  }
}

void loop()
{
    mqttLoop();
  handlePulseInterrupt();
  updateDiagLed();
  updateBedLed(); // Ciągłe odświeżanie paska pod łóżkiem w pętli głównej

  if (!ledsActive) {
    return;
  }

  unsigned long now = millis();

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