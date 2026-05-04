#pragma once
#include <Arduino.h>
#include <time.h>
#include "animations.h"

// ─── Game state ───────────────────────────────────────────────────────────────
enum GameState : uint8_t {
    GS_STARTING,
    GS_STAND,          // Idle — variant chosen by friendship level
    GS_SLEEPING,
    GS_EATING,
    GS_WALKING,
    GS_BATH,
    GS_LICKING,
    GS_WATCH_TV,
    GS_HAPPY,
    GS_HEART_SMILES,
    GS_BACKFLIP,
    GS_CLOCK_MENU,
    GS_STATUS_MENU,
    GS_GIFT_MENU,
    GS_RAN_AWAY,       // Pikachu left — friendship too low
    GS_GAME_COMPLETE,  // Reached step goal
};

// ─── Persistent Pikachu status ───────────────────────────────────────────────
struct PikaStatus {
    uint32_t steps;             // Session steps (resets each "day")
    uint32_t totalSteps;        // All-time step total
    int32_t  watts;             // Currency
    int32_t  friendshipLevel;   // -1500 … 3000+
    bool     todayReach150;     // Unlocked tier 1 animations today
    bool     todayReach300;     // Unlocked tier 2 animations today
    bool     reachEnd;          // Completed the game
};

// ─── Current animation player ────────────────────────────────────────────────
struct AnimPlayer {
    const AnimSeq* seq;         // Active animation sequence
    uint8_t        frameIdx;    // Current frame within the sequence
    uint32_t       lastFrameMs; // millis() timestamp of last advance
    bool           looping;     // Repeat the sequence?
};

// ─── Publicly accessible state ───────────────────────────────────────────────
extern PikaStatus  pikaStatus;
extern GameState   gameState;
extern AnimPlayer  animPlayer;
extern bool        screenOn;

// ─── API ─────────────────────────────────────────────────────────────────────
void game_init();
void game_update();             // Call every loop()
void game_on_short_press();     // PRG short press — advance to next screen
void game_on_long_press();      // PRG long press  — back to game / confirm
void game_on_mesh_message();    // Any received mesh packet (broadcast or DM) — counts as a step

// Start an animation (replaces current)
void game_play_anim(const AnimSeq* seq, bool loop = false);
// Convenience: get the frame pointer for the current animation frame
const uint8_t* game_current_frame();

// Current local time helpers
int  game_hour();
int  game_minute();
bool game_is_sleep_time();
