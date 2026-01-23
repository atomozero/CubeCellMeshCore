/**
 * Led.cpp - LED signaling for CubeCellMeshCore
 *
 * Supports both NeoPixel (HTCC-AB01) and GPIO13 LED (HTCC-AB02A)
 */

#include "Led.h"

//=============================================================================
// LED Functions
//=============================================================================

void initLed() {
#ifdef MC_SIGNAL_NEOPIXEL
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, HIGH);  // Start with Vext off
    pixels.begin();
    pixels.clear();
    pixels.show();
#endif
#ifdef MC_SIGNAL_GPIO13
    pinMode(GPIO13, OUTPUT);
    digitalWrite(GPIO13, LOW);
#endif
}

void ledVextOn() {
#ifdef MC_SIGNAL_NEOPIXEL
    digitalWrite(Vext, LOW);
    delay(1);
#endif
}

void ledRxOn() {
#ifdef MC_SIGNAL_NEOPIXEL
    ledVextOn();
    pixels.setPixelColor(0, pixels.Color(0, 16, 0));  // Green
    pixels.show();
#endif
#ifdef MC_SIGNAL_GPIO13
    digitalWrite(GPIO13, HIGH);
#endif
}

void ledTxOn() {
#ifdef MC_SIGNAL_NEOPIXEL
    ledVextOn();
    pixels.setPixelColor(0, pixels.Color(16, 0, 16));  // Viola (red + blue)
    pixels.show();
#endif
#ifdef MC_SIGNAL_GPIO13
    digitalWrite(GPIO13, HIGH);
#endif
}

void ledRedSolid() {
#ifdef MC_SIGNAL_NEOPIXEL
    ledVextOn();
    pixels.setPixelColor(0, pixels.Color(16, 0, 0));  // Red
    pixels.show();
#endif
#ifdef MC_SIGNAL_GPIO13
    digitalWrite(GPIO13, HIGH);
#endif
}

void ledGreenBlink() {
#ifdef MC_SIGNAL_NEOPIXEL
    ledVextOn();
    pixels.setPixelColor(0, pixels.Color(0, 32, 0));  // Green bright
    pixels.show();
    delay(50);
    pixels.clear();
    pixels.show();
#endif
#ifdef MC_SIGNAL_GPIO13
    digitalWrite(GPIO13, HIGH);
    delay(50);
    digitalWrite(GPIO13, LOW);
#endif
}

void ledBlueDoubleBlink() {
#ifdef MC_SIGNAL_NEOPIXEL
    ledVextOn();
    // First blink
    pixels.setPixelColor(0, pixels.Color(0, 0, 32));  // Blue
    pixels.show();
    delay(100);
    pixels.clear();
    pixels.show();
    delay(100);
    // Second blink
    pixels.setPixelColor(0, pixels.Color(0, 0, 32));  // Blue
    pixels.show();
    delay(100);
    pixels.clear();
    pixels.show();
#endif
#ifdef MC_SIGNAL_GPIO13
    digitalWrite(GPIO13, HIGH);
    delay(100);
    digitalWrite(GPIO13, LOW);
    delay(100);
    digitalWrite(GPIO13, HIGH);
    delay(100);
    digitalWrite(GPIO13, LOW);
#endif
}

void ledOff() {
#ifdef MC_SIGNAL_NEOPIXEL
    pixels.clear();
    pixels.show();
    digitalWrite(Vext, HIGH);  // Turn off Vext to save power
#endif
#ifdef MC_SIGNAL_GPIO13
    digitalWrite(GPIO13, LOW);
#endif
}
