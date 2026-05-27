#include "game.h"
#include "config.h"
#include "FSCommon.h"     // cross-platform FSCom (LittleFS/SD/NoFS abstraction)
#include <time.h>
#include <string.h>

// ─── Globals ──────────────────────────────────────────────────────────────────
PikaStatus  pikaStatus;
GameState   gameState    = GS_STARTING;
AnimPlayer  animPlayer   = { nullptr, 0, 0, false };
bool        screenOn     = true;

// Pikachu save lives at /pikachu/status.dat as a single binary record.
// Cross-platform via FSCom — works on ESP32 (LittleFS) and nRF52 (LittleFS
// over QSPI). Replaces the old ESP32-only Preferences NVS path.
static constexpr const char *PIKA_SAVE_PATH = "/pikachu/status.dat";
static constexpr uint32_t    PIKA_SAVE_MAGIC = 0x504B4341u;  // 'PKCA'

#pragma pack(push, 1)
struct PikaSaveBlob {
    uint32_t magic;
    PikaStatus status;
};
#pragma pack(pop)

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

// ─── Persistence (FSCom — cross-platform) ────────────────────────────────────
static void pika_set_defaults()
{
    memset(&pikaStatus, 0, sizeof(pikaStatus));
    pikaStatus.watts = 50;     // matches old NVS default
}

static void load_status() {
    pika_set_defaults();
#ifdef FSCom
    auto f = FSCom.open(PIKA_SAVE_PATH, FILE_O_READ);
    if (!f) return;
    PikaSaveBlob blob{};
    size_t n = f.read((uint8_t *)&blob, sizeof(blob));
    f.close();
    if (n == sizeof(blob) && blob.magic == PIKA_SAVE_MAGIC) {
        pikaStatus = blob.status;
    }
#endif
}

static void save_status() {
#ifdef FSCom
    FSCom.mkdir("/pikachu");
    if (FSCom.exists(PIKA_SAVE_PATH)) FSCom.remove(PIKA_SAVE_PATH);
    auto f = FSCom.open(PIKA_SAVE_PATH, FILE_O_WRITE);
    if (!f) return;
    PikaSaveBlob blob{ PIKA_SAVE_MAGIC, pikaStatus };
    f.write((const uint8_t *)&blob, sizeof(blob));
    f.flush();
    f.close();
#endif
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
// Shared core — does ONE step's bookkeeping without flushing to flash.
// Callers (process_step + game_add_steps) save once at the end of their
// batch to avoid hammering LittleFS on 200-step WPS bursts.
static void process_step_core() {
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
        return;
    }

    // Trigger walking animation after enough consecutive messages
    if (consecutiveSteps >= CONSECUTIVE_TO_WALK && !isWalking) {
        isWalking = true;
        gameState = GS_WALKING;
        game_play_anim(&Anims::walkCycle1, true);
    }
}

static void process_step() {
    process_step_core();
    save_status();
}

// ─── Public API ───────────────────────────────────────────────────────────────

// Any received mesh packet (broadcast or encrypted DM) counts as a step.
void game_add_steps(uint32_t count) {
    if (count == 0) return;
    uint32_t now = millis();
    // Quiet hours: still count steps toward watts/friendship in the
    // background, but DON'T turn the screen on or reset lastActivityMs
    // — otherwise every BLE/WiFi detection wakes Pikachu at 3 AM.
    bool quiet = game_is_sleep_time() || gameState == GS_SLEEPING;
    if (!quiet) {
        lastActivityMs = now;
        screenOn = true;
        lastStepMs = now;
    }
    // Skip the "look around" / walk-streak resets — those are for human-cadence
    // mesh chat. WiFi-vuln batches are bursty; treat them as background activity.
    for (uint32_t i = 0; i < count; ++i) {
        process_step_core();
        if (gameState == GS_GAME_COMPLETE) break;
    }
    save_status();
}

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

// ─── Gift menu data ──────────────────────────────────────────────────────────
// Watt amounts mirror Pocket Pikachu 1/2's canonical gift presets. Clock and
// Status are also reachable from this menu so a single long-press surfaces
// every screen.
const GiftOption GIFT_OPTIONS[] = {
    { "10W",     10 },
    { "50W",     50 },
    { "100W",   100 },
    { "200W",   200 },
    { "500W",   500 },
    { "All",     -1 },
    { "Clock",    0 },
    { "Status",   0 },
    { "Back",     0 },
};
const uint8_t  GIFT_OPTION_COUNT = sizeof(GIFT_OPTIONS) / sizeof(GIFT_OPTIONS[0]);
uint8_t        giftMenuCursor    = 0;
uint32_t       giftPowUntil      = 0;

// Each watt of gift = +20 friendship (matches the original Pocket Pikachu's
// 10W → +200 ratio).
static void give_gift(int32_t wattAmount)
{
    if (wattAmount < 0) wattAmount = pikaStatus.watts;        // "All"
    if (wattAmount <= 0 || pikaStatus.watts < wattAmount) {
        // Not enough watts — don't spend, but still flash POW briefly so the
        // user gets feedback. No friendship boost in that case.
        giftPowUntil = millis() + 400;
        return;
    }
    pikaStatus.watts -= wattAmount;
    update_friendship(wattAmount * 20);
    giftPowUntil = millis() + 600;
    save_status();
}

// Short press: cycle inside whichever menu is open.
void game_on_short_press() {
    screenOn = true;
    lastActivityMs = millis();

    // POW flash is briefly modal — ignore presses until it clears.
    if (giftPowUntil != 0 && millis() < giftPowUntil) return;

    switch (gameState) {
        case GS_GIFT_MENU:
            giftMenuCursor = (uint8_t)((giftMenuCursor + 1) % GIFT_OPTION_COUNT);
            break;
        case GS_CLOCK_MENU:
            gameState = GS_STATUS_MENU;
            break;
        case GS_STATUS_MENU:
            // Hop back into the gift menu so the user can try another option.
            giftMenuCursor = 0;
            gameState = GS_GIFT_MENU;
            break;
        default:
            // Outside a menu, short press should fall through to Meshtastic
            // (carousel advance). PocketPikachuModule gates this case via
            // menuActive_, so we shouldn't actually be reached here — but be
            // safe and do nothing rather than open a menu surprise.
            break;
    }
}

// Long press: open the gift menu from the game, select inside the gift menu,
// or exit clock/status back to the game.
void game_on_long_press() {
    screenOn = true;
    lastActivityMs = millis();

    if (giftPowUntil != 0 && millis() < giftPowUntil) return;

    switch (gameState) {
        case GS_GIFT_MENU: {
            const GiftOption &opt = GIFT_OPTIONS[giftMenuCursor];
            if (opt.watts != 0) {
                // Watt amount or "All" — spend + boost friendship + POW flash.
                give_gift(opt.watts);
            } else if (strcmp(opt.label, "Clock") == 0) {
                gameState = GS_CLOCK_MENU;
            } else if (strcmp(opt.label, "Status") == 0) {
                gameState = GS_STATUS_MENU;
            } else {  // "Back"
                enter_idle();
            }
            break;
        }
        case GS_CLOCK_MENU:
        case GS_STATUS_MENU:
            enter_idle();
            break;
        default:
            // Open the gift menu from the game.
            giftMenuCursor = 0;
            gameState = GS_GIFT_MENU;
            break;
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

    // Gift POW flash expired. give_gift() already queued the happy
    // animation via update_friendship(); leave it playing for ~2.5 s before
    // returning to idle so the user actually sees Pikachu react.
    static uint32_t happyEndAt = 0;
    if (gameState == GS_GIFT_MENU && giftPowUntil != 0 && now >= giftPowUntil) {
        giftPowUntil = 0;
        gameState    = GS_HAPPY;
        happyEndAt   = now + 2500;
    }
    if (gameState == GS_HAPPY && happyEndAt != 0 && now >= happyEndAt) {
        happyEndAt = 0;
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
