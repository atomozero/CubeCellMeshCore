/**
 * Led.h - LED signaling for CubeCellMeshCore
 *
 * Supports both NeoPixel (HTCC-AB01) and GPIO13 LED (HTCC-AB02A)
 */

#pragma once

#include <Arduino.h>
#include "globals.h"

//=============================================================================
// LED Functions
//=============================================================================

void initLed();
void ledVextOn();
void ledRxOn();
void ledTxOn();
void ledRedSolid();
void ledGreenBlink();
void ledBlueDoubleBlink();
void ledOff();
