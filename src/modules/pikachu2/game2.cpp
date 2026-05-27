#include "game2.h"
#include "config2.h"
#include "gps/RTC.h"
#include "graphics/SharedUIDisplay.h"

// Persistence: ESP32 has Preferences; nRF52840 (T114) starts fresh each boot
// until a proper LittleFS-based save is added.
#if defined(ARCH_ESP32)
#include <Preferences.h>
static Preferences prefs2;
#endif

// ─── Globals ──────────────────────────────────────────────────────────────────
PikaStatus2  pikaStatus2;
GameState2   gameState2    = GS2_STARTING;
AnimPlayer2  animPlayer2   = { nullptr, 0, 0, false };
bool         screenOn2     = true;

static uint32_t lastStepMs2         = 0;
static uint32_t lastActivityMs2     = 0;
static uint32_t consecutiveSteps2   = 0;
static bool     isWalking2          = false;
static uint32_t giftPowUntil2       = 0;

static uint32_t cooldownEat2        = 0;
static uint32_t cooldownBath2       = 0;

// ─── Persistence ─────────────────────────────────────────────────────────────
static void load_status2()
{
#if defined(ARCH_ESP32)
    prefs2.begin("pikachu2", true);
    pikaStatus2.steps           = prefs2.getULong("steps",      0);
    pikaStatus2.totalSteps      = prefs2.getULong("totalSteps", 0);
    pikaStatus2.watts           = prefs2.getLong ("watts",      50);
    pikaStatus2.friendshipLevel = prefs2.getLong ("friendship", 0);
    prefs2.end();
#else
    pikaStatus2.steps           = 0;
    pikaStatus2.totalSteps      = 0;
    pikaStatus2.watts           = 50;
    pikaStatus2.friendshipLevel = 0;
#endif
}

static void save_status2()
{
#if defined(ARCH_ESP32)
    prefs2.begin("pikachu2", false);
    prefs2.putULong("steps",      pikaStatus2.steps);
    prefs2.putULong("totalSteps", pikaStatus2.totalSteps);
    prefs2.putLong ("watts",      pikaStatus2.watts);
    prefs2.putLong ("friendship", pikaStatus2.friendshipLevel);
    prefs2.end();
#endif
}

// ─── Time helpers ─────────────────────────────────────────────────────────────
int game2_hour()
{
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (rtc_sec == 0) return 12; // no time sync yet — assume daytime
    uint32_t hms = (rtc_sec % SEC_PER_DAY + SEC_PER_DAY) % SEC_PER_DAY;
    return (int)(hms / SEC_PER_HOUR);
}

bool game2_is_sleep_time()
{
    int h = game2_hour();
    return (h >= SLEEP2_HOUR || h < WAKE2_HOUR);
}

static void enter_idle2();

// ─── Animation player ─────────────────────────────────────────────────────────
void game2_play_anim(const AnimSeq2*seq, bool loop)
{
    animPlayer2.seq         = seq;
    animPlayer2.frameIdx    = 0;
    animPlayer2.lastFrameMs = millis();
    animPlayer2.looping     = loop;
}

const uint8_t* game2_current_frame()
{
    if (!animPlayer2.seq || animPlayer2.seq->count == 0) return PP2_BLANK_FRAME;
    return animPlayer2.seq->frames[animPlayer2.frameIdx];
}

static void advance_anim2()
{
    if (!animPlayer2.seq) return;
    uint32_t now = millis();
    if (now - animPlayer2.lastFrameMs < animPlayer2.seq->delayMs) return;
    animPlayer2.lastFrameMs = now;
    animPlayer2.frameIdx++;
    if (animPlayer2.frameIdx >= animPlayer2.seq->count) {
        if (animPlayer2.looping) {
            animPlayer2.frameIdx = 0;
        } else {
            animPlayer2.frameIdx = animPlayer2.seq->count - 1;
            enter_idle2();
        }
    }
}

// ─── Friendship helpers ───────────────────────────────────────────────────────
// PP2 gift reactions mirror the real device's tiers (Serebii):
//   <150  → Yawn (happy1)
//   <300  → Thanks (happy2)
//   <600  → Happy! (backflip)
//   600+  → Thank You! (heartSmiles)
static void update_friendship2(int32_t delta)
{
    pikaStatus2.friendshipLevel += delta;
    pikaStatus2.friendshipLevel = constrain(pikaStatus2.friendshipLevel, -2000, 6000);

    if (delta <= 0) {
        // negative delta — no celebration
    } else if (delta >= 600) {
        game2_play_anim(&Anims2::thankYou, false);
    } else if (delta >= 300) {
        game2_play_anim(&Anims2::happyPika2, false);
    } else if (delta >= 150) {
        game2_play_anim(&Anims2::thanks, false);
    } else {
        game2_play_anim(&Anims2::yawn, false);
    }
}

// ─── Idle state selector ─────────────────────────────────────────────────────
static void enter_idle2()
{
    isWalking2        = false;
    consecutiveSteps2 = 0;

    if (pikaStatus2.friendshipLevel <= FRIENDSHIP2_RAN_AWAY) {
        gameState2 = GS2_RAN_AWAY;
        game2_play_anim(&Anims2::pikachusBack, true);
        return;
    }
    if (game2_is_sleep_time()) {
        gameState2 = GS2_SLEEPING;
        game2_play_anim(&Anims2::pikachuSleeps, true);
        return;
    }

    gameState2 = GS2_STAND;
    if      (pikaStatus2.friendshipLevel >= FRIENDSHIP2_LOVING)   game2_play_anim(&Anims2::veryFriendly,  true);
    else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_HAPPY)    game2_play_anim(&Anims2::veryHappy,     true);
    else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_FRIENDLY) game2_play_anim(&Anims2::friendlyStand, true);
    else if (pikaStatus2.friendshipLevel <= FRIENDSHIP2_MAD)      game2_play_anim(&Anims2::grumpy,        true);
    else                                                           game2_play_anim(&Anims2::waving,        true);
}

// ─── PP2 Daily schedule ───────────────────────────────────────────────────────
// Real PP2 schedule (Serebii):
//   7–8 am   Mealtime + Brush Teeth
//   8–12     Play (yo-yo, rolling, TV, radio, hula, aerobics…)
//   12–1 pm  Pokémon Food + Brush Teeth
//   1–3 pm   Afternoon play
//   3–4 pm   Snack time (apple / juice / ice cream)
//   4–6 pm   More afternoon play
//   6–7 pm   Mealtime + Brush Teeth
//   7–9 pm   Evening (sparkler, fireworks, bath)
//   9 pm–7 am Sleep
static void check_scheduled_activities2()
{
    if (gameState2 != GS2_STAND) return;

    int h = game2_hour();
    uint32_t now = millis();

    // Meals: 7 am, 12 pm, 6 pm
    if ((h == 7 || h == 12 || h == 18) && now > cooldownEat2) {
        gameState2 = GS2_EATING;
        game2_play_anim(&Anims2::mealtime, false);
        cooldownEat2 = now + 3600000UL;
        update_friendship2(30);
        return;
    }
    // Snack: 3 pm
    if (h == 15 && now > cooldownEat2) {
        gameState2 = GS2_EATING;
        game2_play_anim(&Anims2::pokemonFood, false);
        cooldownEat2 = now + 3600000UL;
        update_friendship2(20);
        return;
    }
    // Evening TV / licking (7 pm)
    if (h == 19) {
        gameState2 = GS2_PLAYING;
        game2_play_anim(&Anims2::pokeBallTV, false);
        return;
    }
    // Bath: 8 pm (before 9 pm sleep)
    if (h == 20 && now > cooldownBath2) {
        gameState2 = GS2_BATH;
        game2_play_anim(&Anims2::bathTime, false);
        cooldownBath2 = now + 3600000UL;
        update_friendship2(40);
        return;
    }
}

// ─── Step / walk logic ────────────────────────────────────────────────────────
static void process_step2()
{
    pikaStatus2.steps++;
    pikaStatus2.totalSteps++;
    consecutiveSteps2++;

    static uint32_t stepsToConvert2 = 0;
    stepsToConvert2++;
    if (stepsToConvert2 >= STEPS2_PER_WATT) {
        pikaStatus2.watts++;
        stepsToConvert2 = 0;
    }

    if (consecutiveSteps2 >= CONSECUTIVE2_TO_WALK && !isWalking2) {
        isWalking2 = true;
        gameState2 = GS2_WALKING;
        game2_play_anim(&Anims2::pikachuWalks, true);
    }

    save_status2();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void game2_on_long_press()
{
    lastActivityMs2 = millis();
    screenOn2 = true;
    if (gameState2 == GS2_SLEEPING) enter_idle2();
}

void game2_do_gift(int32_t cost, int32_t friendship)
{
    lastActivityMs2 = millis();
    if (pikaStatus2.watts < cost) return;
    pikaStatus2.watts -= cost;
    update_friendship2(friendship);
    gameState2    = GS2_GIFT_MENU;
    giftPowUntil2 = millis() + 600;
    save_status2();
}

void game2_on_mesh_message()
{
    uint32_t now = millis();

    // During sleep OR during nighttime hours: earn watts silently — don't
    // wake the screen, don't change animation, don't reset the activity
    // timer. The hour check also catches the window between when the pet
    // is still in GS2_STAND (haven't idled-into-sleep yet) and the next
    // enter_idle2 tick — otherwise a mesh packet at 3 AM would pop the
    // greetings animation and wake the screen.
    if (gameState2 == GS2_SLEEPING || game2_is_sleep_time()) {
        process_step2();
        return;
    }

    lastActivityMs2 = now;
    screenOn2 = true;

    if (gameState2 == GS2_STAND && consecutiveSteps2 == 0)
        game2_play_anim(&Anims2::greetings, false);

    if (isWalking2 && (now - lastStepMs2) > 1000)
        enter_idle2();

    lastStepMs2 = now;
    process_step2();
}

// ─── Frame-focus greeting ─────────────────────────────────────────────────────
void game2_on_frame_focused()
{
    lastActivityMs2 = millis();
    screenOn2 = true;

    // Auto-wake on focus: if Pikachu is asleep, force into a looping stand
    // animation so the user sees Pikachu awake when they navigate to the
    // slide, regardless of clock time. Bypasses enter_idle2's sleep-hours
    // check.
    if (gameState2 == GS2_SLEEPING) {
        gameState2 = GS2_STAND;
        if      (pikaStatus2.friendshipLevel >= FRIENDSHIP2_LOVING)   game2_play_anim(&Anims2::veryFriendly,  true);
        else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_HAPPY)    game2_play_anim(&Anims2::veryHappy,     true);
        else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_FRIENDLY) game2_play_anim(&Anims2::friendlyStand, true);
        else if (pikaStatus2.friendshipLevel <= FRIENDSHIP2_MAD)      game2_play_anim(&Anims2::grumpy,        true);
        else                                                           game2_play_anim(&Anims2::waving,        true);
        return;
    }

    if (gameState2 != GS2_STAND && gameState2 != GS2_WALKING) return;
    // Pick a random greeting from available PP1-era animations (replaced by
    // proper PP2 greetings once animations2.h is generated from the GIFs).
    const AnimSeq2*greetings[] = {
        &Anims2::greetings,
        &Anims2::wave,
    };
    game2_play_anim(greetings[random(2)], false);
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void game2_init()
{
    load_status2();
    game2_play_anim(&Anims2::niceToMeetYou, false);
    gameState2      = GS2_STARTING;
    lastActivityMs2 = millis();
}

// ─── Main update loop ─────────────────────────────────────────────────────────
static uint32_t lastMinuteCheck2 = 0;
static int      lastCheckedHour2 = -1;

void game2_update()
{
    uint32_t now = millis();

    if (screenOn2 && (now - lastActivityMs2) > SCREEN2_TIMEOUT_MS)
        screenOn2 = false;

    if (gameState2 == GS2_STARTING) {
        if (animPlayer2.frameIdx >= animPlayer2.seq->count - 1)
            enter_idle2();
    }

    if (gameState2 == GS2_GIFT_MENU && now >= giftPowUntil2) {
        gameState2 = GS2_HAPPY;
        game2_play_anim(&Anims2::happyPika1, false);
    }

    if (isWalking2 && (now - lastStepMs2) > 1000)
        enter_idle2();

    if (now - lastMinuteCheck2 > 60000) {
        lastMinuteCheck2 = now;
        check_scheduled_activities2();

        int h = game2_hour();
        if (h == 0 && lastCheckedHour2 == 23) {
            pikaStatus2.steps = 0;
            save_status2();
        }
        lastCheckedHour2 = h;

        static uint32_t decayHours2 = 0;
        decayHours2++;
        if (decayHours2 >= 6) {
            update_friendship2(-10);
            decayHours2 = 0;
            save_status2();
        }
    }

    advance_anim2();
}
