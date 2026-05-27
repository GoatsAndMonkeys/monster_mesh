// SPDX-License-Identifier: MIT
//
// GauntletScreen — opt-in OLED carousel frame showing live gym status.
//
// Pocket Pikachu owns the display permanently per the project's CLAUDE.md
// note. This screen frame is therefore NOT auto-registered. It exists as a
// drop-in for nodes that DO want a gym status frame (e.g. a kiosk-style
// public node without the Pikachu animation).
//
// To register, drop this in Screen.cpp's frame list:
//   #include "modules/monstermesh/gauntlet/GauntletScreen.h"
//   { GauntletScreen::drawFrame, FOCUS_GYM }
//
// Layout (128×64 monochrome SSD1306):
//   ┌────────────── PALLET GYM ───────────────┐
//   │ ─────────────────────────────────────── │  divider
//   │ Ldr: ABCD                                │
//   │ Rank: 4 trainers                         │
//   │ Battles: 1247                            │
//   │ MQTT: online                             │
//   │                              Boulder Badge │
//   └──────────────────────────────────────────┘

#pragma once
#include <stdint.h>

class OLEDDisplay;
class OLEDDisplayUiState;

class GauntletScreen {
  public:
    static void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                           int16_t x, int16_t y);
};
