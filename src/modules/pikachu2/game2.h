#pragma once
#include <Arduino.h>
#include <time.h>
#include "modules/pikachu2/animations2.h"

// ─── Game state ───────────────────────────────────────────────────────────────
enum GameState2 : uint8_t {
    GS2_STARTING,
    GS2_STAND,       // Idle — variant chosen by friendship level
    GS2_SLEEPING,
    GS2_EATING,      // Meal / snack
    GS2_PLAYING,     // Scheduled play activity
    GS2_BATH,
    GS2_WALKING,
    GS2_HAPPY,       // Reaction animation after gift confirm
    GS2_GIFT_MENU,   // POW! flash (600ms) after confirming a gift
    GS2_RAN_AWAY,
};

// ─── Persistent Pikachu 2 status ─────────────────────────────────────────────
struct PikaStatus2 {
    uint32_t steps;
    uint32_t totalSteps;
    int32_t  watts;
    int32_t  friendshipLevel;
};

// ─── Animation player ─────────────────────────────────────────────────────────
struct AnimPlayer2 {
    const AnimSeq2* seq;
    uint8_t         frameIdx;
    uint32_t        lastFrameMs;
    bool            looping;
};

// ─── Publicly accessible state ───────────────────────────────────────────────
extern PikaStatus2  pikaStatus2;
extern GameState2   gameState2;
extern AnimPlayer2  animPlayer2;
extern bool         screenOn2;

// ─── API ─────────────────────────────────────────────────────────────────────
void game2_init();
void game2_update();
void game2_on_long_press();                          // wakes screen if off
void game2_do_gift(int32_t cost, int32_t friendship); // overlay callback
void game2_on_mesh_message();

void game2_play_anim(const AnimSeq2* seq, bool loop = false);
const uint8_t* game2_current_frame();
void game2_on_frame_focused();  // play a greeting when screen navigates to this frame

int  game2_hour();
bool game2_is_sleep_time();
