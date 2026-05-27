#include "configuration.h"

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU

#include "PocketPikachuModule.h"
#include "game.h"
#include "config.h"
#include <time.h>

PocketPikachuModule *pikachuModule;

// ─── Lifecycle ────────────────────────────────────────────────────────────────

PocketPikachuModule::PocketPikachuModule()
    : MeshModule("PocketPikachu"),
      concurrency::OSThread("PocketPikachu")
{
    game_init();
    pikachuModule = this;

    if (inputBroker)
        inputObserver_.observe(inputBroker);
}

// OSThread: advance game logic ~20 fps; auto-exit menu if frame not drawn.
int32_t PocketPikachuModule::runOnce()
{
    // (Boot-focus now owned by PentestModule — user wants the Warwalker
    // frame to be the first screen at startup.)
    game_update();

    // If a Pikachu menu is open but drawFrame hasn't been called for 2 s,
    // the user has navigated away — auto-close so carousel navigation isn't
    // stuck on the menu state when they come back.
    bool inMenu = (gameState == GS_GIFT_MENU ||
                   gameState == GS_CLOCK_MENU ||
                   gameState == GS_STATUS_MENU);
    if (inMenu && lastDrawMs_ != 0 && millis() - lastDrawMs_ > 2000) {
        // game_on_long_press exits any of those menu states back to idle.
        game_on_long_press();
    }

    return 50;
}

// ─── Mesh ─────────────────────────────────────────────────────────────────────

ProcessMessage PocketPikachuModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    game_on_mesh_message();
    return ProcessMessage::CONTINUE;
}

// ─── Button input ─────────────────────────────────────────────────────────────

int PocketPikachuModule::handleInputEvent(const InputEvent *event)
{
    // Only act on input when Pikachu's frame is currently being drawn — i.e.
    // the user is on the Pikachu carousel page. Otherwise pass through so the
    // active frame (Messages/Nodes/etc.) gets the event.
    //
    // 3 s window: when the carousel parks on Pikachu it stops re-rendering
    // between presses, so lastDrawMs_ can be a few seconds stale even though
    // we're the active frame. 250 ms was too tight — long-press to open the
    // menu would silently no-op.
    if (lastDrawMs_ == 0 || (millis() - lastDrawMs_) > 3000) {
        return 0;
    }

    auto inMenu = []() {
        return gameState == GS_GIFT_MENU ||
               gameState == GS_CLOCK_MENU ||
               gameState == GS_STATUS_MENU;
    };

    if (event->inputEvent == INPUT_BROKER_SELECT) {
        game_on_long_press();
        return 1;
    } else if (event->inputEvent == INPUT_BROKER_USER_PRESS && inMenu()) {
        // Only consume short press while a menu is actually open. Otherwise
        // pass through so the Meshtastic carousel can advance.
        game_on_short_press();
        return 1;
    }
    return 0;
}



// ─── Draw helpers ─────────────────────────────────────────────────────────────

static inline void drawScaledPixel(OLEDDisplay *d, int16_t x, int16_t y)
{
    for (int dy = 0; dy < GAME_SCALE; dy++)
        for (int dx = 0; dx < GAME_SCALE; dx++)
            d->setPixel(x + dx, y + dy);
}

void PocketPikachuModule::drawGameArea(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    // Invert ONLY the Pikachu game area: white field, black line art. The rest
    // of the Meshtastic carousel (sidebar, clock/status menus, other module
    // frames) keeps the standard white-on-black scheme.
    display->setColor(WHITE);
    display->fillRect(ox + GAME_X, oy + GAME_Y, GAME_W, GAME_H);
    display->setColor(BLACK);

    const uint8_t *frame = game_current_frame();
    for (int row = 0; row < PIKA_ROWS; row++) {
        for (int col = 0; col < PIKA_COLS; col++) {
            uint16_t idx = (uint16_t)row * PIKA_COLS + col;
            if (frame_pixel(frame, idx))
                drawScaledPixel(display,
                    ox + GAME_X + col * GAME_SCALE,
                    oy + GAME_Y + row * GAME_SCALE);
        }
    }

    // Restore default color so the sidebar (drawn next) renders normally.
    display->setColor(WHITE);
}

void PocketPikachuModule::drawSidebar(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    display->drawLine(ox + SIDEBAR_X - 1, oy, ox + SIDEBAR_X - 1, oy + SCREEN_H - 1);
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    int h12 = t.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    char buf[16];
    snprintf(buf, sizeof(buf), "%2d:%02d", h12, t.tm_min);
    display->drawString(ox + SIDEBAR_X + 1, oy,      buf);
    display->drawString(ox + SIDEBAR_X + 1, oy + 10, t.tm_hour >= 12 ? "PM" : "AM");

    int hearts = 0;
    if      (pikaStatus.friendshipLevel >= FRIENDSHIP_LOVES) hearts = 3;
    else if (pikaStatus.friendshipLevel >= FRIENDSHIP_LIKES) hearts = 2;
    else if (pikaStatus.friendshipLevel >  0)                hearts = 1;
    for (int i = 0; i < 3; i++) {
        int hx = ox + SIDEBAR_X + 1 + i * 10;
        int hy = oy + 22;
        if (i < hearts) {
            display->fillRect(hx + 1, hy,     2, 1);
            display->fillRect(hx + 3, hy,     2, 1);
            display->fillRect(hx,     hy + 1, 6, 1);
            display->fillRect(hx + 1, hy + 2, 4, 1);
            display->fillRect(hx + 2, hy + 3, 2, 1);
        } else {
            display->setPixel(hx + 1, hy); display->setPixel(hx + 2, hy);
            display->setPixel(hx + 3, hy); display->setPixel(hx + 4, hy);
            display->setPixel(hx,     hy + 1); display->setPixel(hx + 5, hy + 1);
            display->setPixel(hx,     hy + 2); display->setPixel(hx + 5, hy + 2);
            display->setPixel(hx + 1, hy + 3); display->setPixel(hx + 4, hy + 3);
            display->setPixel(hx + 2, hy + 4); display->setPixel(hx + 3, hy + 4);
        }
    }

    snprintf(buf, sizeof(buf), "W%5ld",  (long)pikaStatus.watts);
    display->drawString(ox + SIDEBAR_X + 1, oy + 30, buf);
    snprintf(buf, sizeof(buf), "S%6lu",  (unsigned long)pikaStatus.steps);
    display->drawString(ox + SIDEBAR_X + 1, oy + 42, buf);
}

void PocketPikachuModule::drawClockScreen(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    int h12 = t.tm_hour % 12;
    if (h12 == 0) h12 = 12;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", h12, t.tm_min);
    display->setFont(ArialMT_Plain_24);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(ox + 64, oy + 18, buf);   // centred on full width
    display->setFont(ArialMT_Plain_10);
    display->drawString(ox + 64, oy + 46, t.tm_hour >= 12 ? "PM" : "AM");
}

void PocketPikachuModule::drawGiftMenu(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    // POW! flash takes the whole screen for ~600 ms after a gift is selected.
    if (giftPowUntil != 0 && millis() < giftPowUntil) {
        display->setFont(ArialMT_Plain_24);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(ox + SCREEN_W / 2, oy + 18, "POW!");
        display->setFont(ArialMT_Plain_10);
        char buf[20];
        snprintf(buf, sizeof(buf), "%ldW left", (long)pikaStatus.watts);
        display->drawString(ox + SCREEN_W / 2, oy + 46, buf);
        return;
    }

    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    char hdr[20];
    snprintf(hdr, sizeof(hdr), "Gift Pikachu  %ldW", (long)pikaStatus.watts);
    display->drawString(ox + 2, oy, hdr);

    // Render up to 5 visible rows centred on the cursor so it's always in view.
    constexpr uint8_t VISIBLE = 5;
    uint8_t first;
    if (GIFT_OPTION_COUNT <= VISIBLE) {
        first = 0;
    } else if (giftMenuCursor < VISIBLE / 2) {
        first = 0;
    } else if (giftMenuCursor + (VISIBLE - VISIBLE / 2) >= GIFT_OPTION_COUNT) {
        first = (uint8_t)(GIFT_OPTION_COUNT - VISIBLE);
    } else {
        first = (uint8_t)(giftMenuCursor - VISIBLE / 2);
    }
    uint8_t last = (uint8_t)((first + VISIBLE < GIFT_OPTION_COUNT)
                                  ? first + VISIBLE : GIFT_OPTION_COUNT);

    for (uint8_t i = first; i < last; i++) {
        int row = i - first;
        int yy  = oy + 12 + row * 10;
        if (i == giftMenuCursor) {
            display->fillRect(ox + 0, yy, SCREEN_W, 10);
            display->setColor(BLACK);
            display->drawString(ox + 6, yy, GIFT_OPTIONS[i].label);
            display->setColor(WHITE);
        } else {
            display->drawString(ox + 6, yy, GIFT_OPTIONS[i].label);
        }
    }
}

void PocketPikachuModule::drawStatusScreen(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    char buf[24];

    snprintf(buf, sizeof(buf), "Steps: %lu", (unsigned long)pikaStatus.totalSteps);
    display->drawString(ox + 4, oy + 2,  buf);

    snprintf(buf, sizeof(buf), "Watts: %ld", (long)pikaStatus.watts);
    display->drawString(ox + 4, oy + 14, buf);

    const char *mood;
    if      (pikaStatus.friendshipLevel >= FRIENDSHIP_LOVES) mood = "Loves you!";
    else if (pikaStatus.friendshipLevel >= FRIENDSHIP_LIKES) mood = "Likes you";
    else if (pikaStatus.friendshipLevel >= 0)                mood = "Neutral";
    else if (pikaStatus.friendshipLevel >  FRIENDSHIP_MAD)   mood = "Unhappy";
    else                                                      mood = "Mad!";
    snprintf(buf, sizeof(buf), "Mood: %s", mood);
    display->drawString(ox + 4, oy + 26, buf);

    snprintf(buf, sizeof(buf), "Level: %s",
             pikaStatus.todayReach300 ? "3" : pikaStatus.todayReach150 ? "2" : "1");
    display->drawString(ox + 4, oy + 38, buf);
}

// ─── Carousel frame entry point ───────────────────────────────────────────────

void PocketPikachuModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                                    int16_t x, int16_t y)
{
    uint32_t now = millis();
    // Auto-wake on focus: first visit (lastDrawMs_ == 0) AND any return
    // after a >2 s gap. If Pikachu is asleep, wake to a looping idle stand.
    bool focusEvent = (lastDrawMs_ == 0 || (now - lastDrawMs_) > 2000);
    if (focusEvent && gameState == GS_SLEEPING) {
        gameState = GS_STAND;
        if      (pikaStatus.friendshipLevel >= FRIENDSHIP_LOVES) game_play_anim(&Anims::standLove,      true);
        else if (pikaStatus.friendshipLevel >= FRIENDSHIP_LIKES) game_play_anim(&Anims::standLike,      true);
        else if (pikaStatus.friendshipLevel <= FRIENDSHIP_MAD)   game_play_anim(&Anims::standMad,       true);
        else                                                     game_play_anim(&Anims::standBasicIdle, true);
        screenOn = true;
    }
    lastDrawMs_ = now;
    display->clear();
    if (!screenOn) return;

    switch (gameState) {
        case GS_GIFT_MENU:    drawGiftMenu(display, x, y);     break;
        case GS_CLOCK_MENU:   drawClockScreen(display, x, y);  break;
        case GS_STATUS_MENU:  drawStatusScreen(display, x, y); break;
        default:              drawGameArea(display, x, y);
                              drawSidebar(display, x, y);       break;
    }
}

#endif // HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU
