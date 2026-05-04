#include "configuration.h"

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU

#include "PocketPikachuModule.h"
#include "pikachu/game.h"
#include "pikachu/config.h"
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
    game_update();

    // If menu is open but our drawFrame hasn't been called for 2 s, the user
    // navigated away. Auto-close so carousel navigation isn't stuck.
    if (menuActive_ && millis() - lastDrawMs_ > 2000) {
        menuActive_ = false;
        game_on_long_press(); // enter_idle() when in menu state
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
    if (event->inputEvent == INPUT_BROKER_SELECT) {
        // Long press: toggle menu open/close
        menuActive_ = !menuActive_;
        game_on_long_press();   // enters clock menu from game, or enter_idle() from menu
        if (gameState != GS_CLOCK_MENU && gameState != GS_STATUS_MENU)
            menuActive_ = false; // ensure flag matches actual state

    } else if (event->inputEvent == INPUT_BROKER_USER_PRESS && menuActive_) {
        // Short press while menu open: cycle clock → status → exit
        game_on_short_press();
        if (gameState != GS_CLOCK_MENU && gameState != GS_STATUS_MENU)
            menuActive_ = false; // cycled all the way back to game
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
    lastDrawMs_ = millis();
    display->clear();
    if (!screenOn) return;

    switch (gameState) {
        case GS_CLOCK_MENU:   drawClockScreen(display, x, y);  break;
        case GS_STATUS_MENU:  drawStatusScreen(display, x, y); break;
        default:              drawGameArea(display, x, y);
                              drawSidebar(display, x, y);       break;
    }
}

#endif // HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU
