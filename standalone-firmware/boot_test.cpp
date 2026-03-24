// Ultra-minimal T-Deck test — hardcoded pins, no macros
#include <Arduino.h>

// T-Deck backlight is GPIO 42
#define BACKLIGHT_PIN 42

void setup() {
    // Just turn on the backlight — if the screen glows, firmware is running
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);
}

void loop() {
    // Blink backlight so it's obvious
    digitalWrite(BACKLIGHT_PIN, HIGH);
    delay(500);
    digitalWrite(BACKLIGHT_PIN, LOW);
    delay(500);
}
