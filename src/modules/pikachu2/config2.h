#pragma once

// ── T114 display (240×135 TFT, 4× pixel scale) ───────────────────────────────
#define SCREEN2_W    240
#define SCREEN2_H    135

// Pocket Pikachu 2 logical grid — same 36×30 sprites as PP1, rendered larger
#define PIKA2_COLS   36
#define PIKA2_ROWS   30
#define PIKA2_PIXELS (PIKA2_COLS * PIKA2_ROWS)   // 1080
#define FRAME2_BYTES ((PIKA2_PIXELS * 2 + 7) / 8) // 270 bytes per frame (2bpp)

// Title bar at top of screen
#define TITLE2_H     18

// Game area: 4× scaled, starts below title bar
#define GAME2_SCALE  4
#define GAME2_X      0
#define GAME2_Y      TITLE2_H
#define GAME2_W      (PIKA2_COLS * GAME2_SCALE)  // 144
#define GAME2_H      (PIKA2_ROWS * GAME2_SCALE)  // 120

// Sidebar
#define SIDEBAR2_X   (GAME2_W + 2)               // 146
#define SIDEBAR2_W   (SCREEN2_W - SIDEBAR2_X)    // 94

// ── PP2 game constants ────────────────────────────────────────────────────────
// Real PP2 sleeps 9 pm – 7 am
#define SLEEP2_HOUR   21
#define WAKE2_HOUR     6

// Friendship thresholds (PP2 is more generous — steps + gifts both count)
#define FRIENDSHIP2_RAN_AWAY   -1500
#define FRIENDSHIP2_MAD         -500
#define FRIENDSHIP2_FRIENDLY    1500
#define FRIENDSHIP2_HAPPY       3000
#define FRIENDSHIP2_LOVING      5000

// PP2 keeps the same 10 messages = 1 watt from our config.h
#define STEPS2_PER_WATT   10

// Walk-streak threshold before walk animation fires
#define CONSECUTIVE2_TO_WALK   20

#define SCREEN2_TIMEOUT_MS   60000

// ── Animation timing ─────────────────────────────────────────────────────────
#define ANIM2_FRAME_MS     500
#define SLEEP2_FRAME_MS   1200

// ── Fallback 4-color palette ─────────────────────────────────────────────────
// Game Boy DMG green, RGB565. Per-animation palettes (in animations2.h's
// AnimSeq2::palette) take precedence — this is just the fallback when the
// active sequence is missing or when we render the blank frame.
//   #9bbc0f  paper  → 0x9DE1
//   #8bac0f  light  → 0x8D61
//   #306230  dark   → 0x3306
//   #0f380f  ink    → 0x09C1
// RGB565 conversion: ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)
static const uint16_t PIKACHU2_PALETTE[4] = {
    0x9DE1,  // lightest (paper)
    0x8D61,  // light shade
    0x3306,  // dark shade
    0x09C1,  // darkest (ink)
};
