#include "game.h"
#include "config.h"
#include <Preferences.h>
#include <time.h>

// ─── Globals ──────────────────────────────────────────────────────────────────
PikaStatus  pikaStatus;
GameState   gameState    = GS_STARTING;
AnimPlayer  animPlayer   = { nullptr, 0, 0, false };
bool        screenOn     = true;

static Preferences prefs;

// Step / walk tracking
static uint32_t lastStepMs         = 0;
static uint32_t lastActivityMs     = 0;
static uint32_t consecutiveSteps   = 0;
static bool     isWalking          = false;


// Time-based activity cookies (millis timestamps when they expire)
static uint32_t cooldownEat        = 0;
static uint32_t cooldownBath       = 0;
static uint32_t cooldownSleep      = 0;
static uint32_t cooldownWalk       = 0;

// ─── Persistence ─────────────────────────────────────────────────────────────
static void load_status() {
    prefs.begin("pikachu", true);
    pikaStatus.steps           = prefs.getULong("steps",      0);
    pikaStatus.totalSteps      = prefs.getULong("totalSteps", 0);
    pikaStatus.watts           = prefs.getLong ("watts",      50);
    pikaStatus.friendshipLevel = prefs.getLong ("friendship", 0);
    pikaStatus.todayReach150   = prefs.getBool ("reach150",   false);
    pikaStatus.todayReach300   = prefs.getBool ("reach300",   false);
    pikaStatus.reachEnd        = prefs.getBool ("reachEnd",   false);
    prefs.end();
}

static void save_status() {
    prefs.begin("pikachu", false);
    prefs.putULong("steps",      pikaStatus.steps);
    prefs.putULong("totalSteps", pikaStatus.totalSteps);
    prefs.putLong ("watts",      pikaStatus.watts);
    prefs.putLong ("friendship", pikaStatus.friendshipLevel);
    prefs.putBool ("reach150",   pikaStatus.todayReach150);
    prefs.putBool ("reach300",   pikaStatus.todayReach300);
    prefs.putBool ("reachEnd",   pikaStatus.reachEnd);
    prefs.end();
}

// ─── Time helpers ─────────────────────────────────────────────────────────────
int game_hour() {
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    return t.tm_hour;
}

int game_minute() {
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    return t.tm_min;
}

bool game_is_sleep_time() {
    int h = game_hour();
    return (h >= SLEEP_HOUR || h < WAKE_HOUR);
}

// ─── Animation player ─────────────────────────────────────────────────────────
void game_play_anim(const AnimSeq* seq, bool loop) {
    animPlayer.seq        = seq;
    animPlayer.frameIdx   = 0;
    animPlayer.lastFrameMs = millis();
    animPlayer.looping    = loop;
}

const uint8_t* game_current_frame() {
    if (!animPlayer.seq || animPlayer.seq->count == 0) return BLANK_FRAME;
    return animPlayer.seq->frames[animPlayer.frameIdx];
}

static void advance_anim() {
    if (!animPlayer.seq) return;
    uint32_t now = millis();
    if (now - animPlayer.lastFrameMs < animPlayer.seq->delayMs) return;
    animPlayer.lastFrameMs = now;
    animPlayer.frameIdx++;
    if (animPlayer.frameIdx >= animPlayer.seq->count) {
        if (animPlayer.looping) {
            animPlayer.frameIdx = 0;
        } else {
            animPlayer.frameIdx = animPlayer.seq->count - 1;
            // Animation finished — return to idle
            game_update();
        }
    }
}

// ─── Friendship helpers ───────────────────────────────────────────────────────
static void update_friendship(int32_t delta) {
    pikaStatus.friendshipLevel += delta;
    pikaStatus.friendshipLevel = constrain(pikaStatus.friendshipLevel, -2000, 4000);

    // Reaction animation based on how positive the change is
    if (delta <= 0) {
        // Small negative: tongue-out look (not implemented yet, just stand mad)
    } else if (delta >= 500) {
        game_play_anim(&Anims::heartSmiles, false);
    } else if (delta >= 200) {
        game_play_anim(&Anims::happy2, false);
    } else {
        game_play_anim(&Anims::happy1, false);
    }
}

// ─── Idle state selector ─────────────────────────────────────────────────────
static void enter_idle() {
    isWalking = false;
    consecutiveSteps = 0;

    if (pikaStatus.friendshipLevel <= FRIENDSHIP_RAN_AWAY) {
        gameState = GS_RAN_AWAY;
        game_play_anim(&Anims::ranAway, true);
        return;
    }
    if (pikaStatus.reachEnd) {
        gameState = GS_GAME_COMPLETE;
        game_play_anim(&Anims::finalScreen, true);
        return;
    }
    if (game_is_sleep_time()) {
        gameState = GS_SLEEPING;
        // Pick a random sleep pose
        const AnimSeq* poses[] = { &Anims::sleepFront, &Anims::sleepSide, &Anims::sleepBack };
        game_play_anim(poses[random(3)], true);
        return;
    }

    gameState = GS_STAND;
    if      (pikaStatus.friendshipLevel >= FRIENDSHIP_LOVES) game_play_anim(&Anims::standLove,  true);
    else if (pikaStatus.friendshipLevel >= FRIENDSHIP_LIKES) game_play_anim(&Anims::standLike,  true);
    else if (pikaStatus.friendshipLevel <= FRIENDSHIP_MAD)   game_play_anim(&Anims::standMad,   true);
    else                                                      game_play_anim(&Anims::standBasicIdle, true);
}

// ─── Time-based activities (called once per minute) ───────────────────────────
static void check_scheduled_activities() {
    if (gameState != GS_STAND) return;  // Don't interrupt ongoing animation

    int h = game_hour();
    uint32_t now = millis();

    // Eating — hours 10, 12, 18
    if ((h == 10 || h == 12 || h == 18) && now > cooldownEat) {
        gameState = GS_EATING;
        bool chopsticks = pikaStatus.todayReach300 && (random(2) == 0);
        game_play_anim(chopsticks ? &Anims::eatingNomnom : &Anims::eatingToast, false);
        cooldownEat = now + 3600000UL;
        update_friendship(50);
        return;
    }
    // Licking — hour 15
    if (h == 15 && now > cooldownBath) {
        gameState = GS_LICKING;
        game_play_anim(&Anims::licking, false);
        cooldownBath = now + 3600000UL;
        return;
    }
    // Bath — hour 19
    if (h == 19 && now > cooldownBath) {
        gameState = GS_BATH;
        game_play_anim(&Anims::bath, false);
        cooldownBath = now + 3600000UL;
        update_friendship(30);
        return;
    }
    // TV — hours 18, 19
    if (h == 18 || h == 19) {
        gameState = GS_WATCH_TV;
        game_play_anim(&Anims::watchTV, false);
        return;
    }
}

// ─── Step / walk logic ────────────────────────────────────────────────────────
static void process_step() {
    if (pikaStatus.steps >= 99999) return;

    pikaStatus.steps++;
    pikaStatus.totalSteps++;
    consecutiveSteps++;

    // Convert steps to watts
    static uint32_t stepsToConvert = 0;
    stepsToConvert++;
    if (stepsToConvert >= STEPS_PER_WATT) {
        pikaStatus.watts++;
        stepsToConvert = 0;
    }

    // Unlock tiers
    if (!pikaStatus.todayReach150 && pikaStatus.totalSteps >= STEPS_UNLOCK1) {
        pikaStatus.todayReach150 = true;
        update_friendship(200);
    }
    if (!pikaStatus.todayReach300 && pikaStatus.totalSteps >= STEPS_UNLOCK2) {
        pikaStatus.todayReach300 = true;
        update_friendship(300);
    }
    if (!pikaStatus.reachEnd && pikaStatus.totalSteps >= STEPS_GOAL) {
        pikaStatus.reachEnd = true;
        gameState = GS_GAME_COMPLETE;
        game_play_anim(&Anims::finalScreen, true);
        save_status();
        return;
    }

    // Trigger walking animation after enough consecutive messages
    if (consecutiveSteps >= CONSECUTIVE_TO_WALK && !isWalking) {
        isWalking = true;
        gameState = GS_WALKING;
        game_play_anim(&Anims::walkCycle1, true);
    }

    save_status();
}

// ─── Public API ───────────────────────────────────────────────────────────────

// Any received mesh packet (broadcast or encrypted DM) counts as a step.
void game_on_mesh_message() {
    uint32_t now = millis();
    lastActivityMs = now;
    screenOn = true;

    // Receiving messages while Pikachu sleeps slowly wakes and annoys her
    if (gameState == GS_SLEEPING) {
        consecutiveSteps++;
        if (consecutiveSteps >= 15) {
            update_friendship(-200);
            enter_idle();
        }
        return;
    }

    // First message while standing still: look around before walking
    if (gameState == GS_STAND && consecutiveSteps == 0) {
        game_play_anim(&Anims::standBasicLook, false);
    }

    // Gap of >1 s between messages resets the walk streak
    if (isWalking && (now - lastStepMs) > 1000) {
        enter_idle();
    }

    lastStepMs = now;
    process_step();
}

// Short press: advance one step in the screen carousel.
void game_on_short_press() {
    screenOn = true;
    lastActivityMs = millis();

    switch (gameState) {
        case GS_CLOCK_MENU:
            gameState = GS_STATUS_MENU;
            break;
        case GS_STATUS_MENU:
            enter_idle();
            break;
        default:  // any game / animation state
            gameState = GS_CLOCK_MENU;
            break;
    }
}

// Long press: enter menu from game, or return to game from any menu.
void game_on_long_press() {
    screenOn = true;
    lastActivityMs = millis();

    if (gameState == GS_CLOCK_MENU || gameState == GS_STATUS_MENU || gameState == GS_GIFT_MENU) {
        enter_idle();
    } else {
        // Open the menu at the clock screen
        gameState = GS_CLOCK_MENU;
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void game_init() {
    load_status();

    // Meshtastic handles WiFi, NTP, and button pin setup — nothing to do here.

    game_play_anim(&Anims::pokeball, false);
    gameState = GS_STARTING;
    lastActivityMs = millis();
}

// ─── Main update loop ─────────────────────────────────────────────────────────
static uint32_t lastMinuteCheck = 0;
static int      lastCheckedHour = -1;

void game_update() {
    uint32_t now = millis();

    // Screen timeout
    if (screenOn && (now - lastActivityMs) > SCREEN_TIMEOUT_MS) {
        screenOn = false;
    }

    // Transition out of STARTING once pokeball animation ends
    if (gameState == GS_STARTING) {
        if (animPlayer.frameIdx >= animPlayer.seq->count - 1) {
            enter_idle();
        }
    }

    // Walking: stop if no mesh message for 1 second
    if (isWalking && (now - lastStepMs) > 1000) {
        enter_idle();
    }

    // Minute-by-minute scheduled activities
    if (now - lastMinuteCheck > 60000) {
        lastMinuteCheck = now;
        check_scheduled_activities();

        // Daily reset at midnight
        int h = game_hour();
        if (h == 0 && lastCheckedHour == 23) {
            pikaStatus.steps         = 0;
            pikaStatus.todayReach150 = false;
            pikaStatus.todayReach300 = false;
            save_status();
        }
        lastCheckedHour = h;

        // Friendship slowly decays unless player is active
        static uint32_t decayHours = 0;
        decayHours++;
        if (decayHours >= 6) {
            update_friendship(-10);
            decayHours = 0;
            save_status();
        }
    }

    // Button events come from Meshtastic's ButtonThread via onShortPress()/onLongPress().

    // Advance current animation frame
    advance_anim();
}
