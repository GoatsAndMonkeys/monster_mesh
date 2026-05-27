#pragma once

// ── Heltec WiFi LoRa 32 V3 OLED pins ─────────────────────────────────────────
#define OLED_SDA    17
#define OLED_SCL    18
#define OLED_RST    21

// ── Button pins ───────────────────────────────────────────────────────────────
#define BTN_PRG         0    // PRG button — short press = next, long press = select
#define LONG_PRESS_MS   600  // Threshold separating short from long press

// ── Display geometry ──────────────────────────────────────────────────────────
#define SCREEN_W    128
#define SCREEN_H    64

// Pocket Pikachu logical grid
#define PIKA_COLS   36
#define PIKA_ROWS   30
#define PIKA_PIXELS (PIKA_COLS * PIKA_ROWS)   // 1080
#define FRAME_BYTES (PIKA_PIXELS / 8)          // 135 bytes per frame

// Game area: 2× scaled, centred vertically  ((64-60)/2 = 2px top padding)
#define GAME_SCALE  2
#define GAME_X      0
#define GAME_Y      2
#define GAME_W      (PIKA_COLS * GAME_SCALE)   // 72
#define GAME_H      (PIKA_ROWS * GAME_SCALE)   // 60

// Sidebar to the right of the game area
#define SIDEBAR_X   (GAME_W + 2)               // 74 — 2px gap + divider
#define SIDEBAR_W   (SCREEN_W - SIDEBAR_X)     // 54

// ── WiFi / NTP ────────────────────────────────────────────────────────────────
// Meshtastic owns WiFi + NTP in this build — these are kept for reference only
// and not actually used. The standalone Pocket Pikachu firmware (separate PIO
// project) does its own WiFi/NTP via secrets.h.
// #include "secrets.h"        // standalone-only — not present in firmware tree
#define NTP_SERVER  "pool.ntp.org"
#define GMT_OFFSET  -18000     // UTC-5 (Eastern); adjust as needed
#define DST_OFFSET  3600

// ── Game constants ────────────────────────────────────────────────────────────
#define FRIENDSHIP_RAN_AWAY   -1500
#define FRIENDSHIP_MAD         -500
#define FRIENDSHIP_LIKES       1500
#define FRIENDSHIP_LOVES       3000

#define SLEEP_HOUR             20
#define WAKE_HOUR               8

#define STEPS_PER_WATT         20
#define CONSECUTIVE_TO_WALK    20   // Shakes needed to trigger walk animation

// Step unlock thresholds (medium difficulty)
#define STEPS_UNLOCK1          15000
#define STEPS_UNLOCK2          30000
#define STEPS_GOAL            100000

#define SCREEN_TIMEOUT_MS      60000  // Auto-off after 60 s of inactivity

// ── Animation timing ─────────────────────────────────────────────────────────
#define ANIM_FRAME_MS          500    // Default ms between frames
#define SLEEP_FRAME_MS        1200    // Slower breathing during sleep
