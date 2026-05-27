#include "configuration.h"

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU2

#include "PocketPikachu2Module.h"
#include "game2.h"
#include "config2.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "gps/RTC.h"
#include "Tft4ColorBlit.h"
// Phase 2c: pet sprite renders in true 4-color via Tft4ColorBlit, which
// drives SPI1 + CS/DC directly (mirroring the bundled meshtastic-st7789 lib's
// setAddrWindow + RAMWR). The sidebar/title still go through OLEDDisplay's
// 1-bit shim — we flush that first inside drawFrame, then blit the sprite on
// top of the cleared game-area region.

PocketPikachu2Module *pikachuModule2;

// ─── Lifecycle ────────────────────────────────────────────────────────────────

PocketPikachu2Module::PocketPikachu2Module()
    : MeshModule("PocketPikachu2"),
      concurrency::OSThread("PocketPikachu2")
{
    game2_init();
    pikachuModule2 = this;
    requestFocus();

    if (inputBroker)
        inputObserver_.observe(inputBroker);
}

int32_t PocketPikachu2Module::runOnce()
{
    game2_update();
    if (lastDrawMs_ != 0 && (millis() - lastDrawMs_) > 2000)
        isFrameActive_ = false;
    if (menuIsOpen_ && screen && !screen->isOverlayBannerShowing())
        menuIsOpen_ = false;
    if (pendingGiftMenu_) {
        pendingGiftMenu_ = false;
        showGiftMenu();
    }
    return 50;
}

// ─── Mesh ─────────────────────────────────────────────────────────────────────

ProcessMessage PocketPikachu2Module::handleReceived(const meshtastic_MeshPacket &mp)
{
    game2_on_mesh_message();
    return ProcessMessage::CONTINUE;
}

// ─── Button input ─────────────────────────────────────────────────────────────

int PocketPikachu2Module::handleInputEvent(const InputEvent *event)
{
    if (event->inputEvent == INPUT_BROKER_SELECT) {
        if (menuIsOpen_) {
            menuIsOpen_ = false;
        } else if (isFrameActive_) {
            if (!screenOn2)
                game2_on_long_press();
            else
                pendingGiftMenu_ = true;
        }
    }
    return 0;
}

void PocketPikachu2Module::showGiftMenu()
{
    if (!screen) return;

    // PP2 gift tiers mirror the real device's reaction bands (Serebii):
    //   10W   → Yawn        (1–99W  band)
    //   100W  → Thanks      (100–199W band)
    //   300W  → Happy!      (300–399W band)
    //   600W  → Thank You!  (500+W  band)
    static const char *kOptions[] = {
        "Tiny     10W",
        "Small   100W",
        "Medium  300W",
        "Large   600W",
        "Back",
    };
    static const int32_t kCosts[]      = {10,  100,  300,  600};
    static const int32_t kFriendship[] = {50,  150,  450,  900};

    static char title[32];
    snprintf(title, sizeof(title), "Give Watts (%ldW)", (long)pikaStatus2.watts);

    menuIsOpen_ = true;
    graphics::BannerOverlayOptions banner;
    banner.message         = title;
    banner.optionsArrayPtr = kOptions;
    banner.optionsCount    = 5;
    banner.notificationType = graphics::notificationTypeEnum::selection_picker;
    banner.bannerCallback   = [](int selected) {
        if (selected >= 0 && selected < 4)
            game2_do_gift(kCosts[selected], kFriendship[selected]);
    };
    screen->showOverlayBanner(banner);
}

// ─── Draw helpers ─────────────────────────────────────────────────────────────

void PocketPikachu2Module::drawGameArea(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    // Pet sprite uses direct 4-color RGB565 blit on the T114 ST7789 (see
    // Tft4ColorBlit.cpp). On other display backends (e.g. monochrome OLEDs in
    // a hypothetical future port) the blit compiles out and we fall back to a
    // monochrome silhouette through the OLEDDisplay buffer.
    const uint8_t *frame = game2_current_frame();

#if defined(USE_ST7789) && !defined(ARCH_ESP32)
    (void)display;
    // Per-animation palette if present, otherwise the DMG-green fallback.
    const uint16_t *palette = PIKACHU2_PALETTE;
    if (animPlayer2.seq) palette = animPlayer2.seq->palette;
    pikachu2::blit2bpp_rgb565((int16_t)(ox + GAME2_X),
                              (int16_t)(oy + GAME2_Y),
                              PIKA2_COLS, PIKA2_ROWS,
                              frame, palette, GAME2_SCALE);
#else
    for (int row = 0; row < PIKA2_ROWS; row++) {
        for (int col = 0; col < PIKA2_COLS; col++) {
            uint16_t idx = (uint16_t)row * PIKA2_COLS + col;
            uint8_t pi = frame_pixel2_idx(frame, idx);
            if (pi == 0) continue; // background → leave as cleared
            display->fillRect(ox + GAME2_X + col * GAME2_SCALE,
                              oy + GAME2_Y + row * GAME2_SCALE,
                              GAME2_SCALE, GAME2_SCALE);
        }
    }
#endif
}

void PocketPikachu2Module::drawSidebar(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    // Divider line — starts below the title bar
    display->drawLine(ox + SIDEBAR2_X - 1, oy + TITLE2_H, ox + SIDEBAR2_X - 1, oy + SCREEN2_H - 1);

    display->setFont(ArialMT_Plain_16);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const int16_t sx = ox + SIDEBAR2_X + 2;
    // Sidebar content base offset sits below the title bar.
    // 122px available (135 - 13); 7 items repacked to fit.
    const int16_t sy = oy + TITLE2_H;

    // Clock — Meshtastic's TZ-adjusted local time, rendered 12h + AM/PM
    // (matches the V3 sidebar). Falls back to "--:--" if RTC isn't synced.
    char buf[24];
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (rtc_sec > 0) {
        uint32_t hms = (rtc_sec % SEC_PER_DAY + SEC_PER_DAY) % SEC_PER_DAY;
        int hour24 = (int)(hms / SEC_PER_HOUR);
        int minute = (int)((hms % SEC_PER_HOUR) / SEC_PER_MIN);
        int h12 = hour24 % 12;
        if (h12 == 0) h12 = 12;
        snprintf(buf, sizeof(buf), "%d:%02d %s",
                 h12, minute, hour24 >= 12 ? "PM" : "AM");
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    display->drawString(sx, sy + 0, buf);

    // Hearts (3 max; 12×8 px each, 14 px pitch)
    int hearts = 0;
    if      (pikaStatus2.friendshipLevel >= FRIENDSHIP2_LOVING)   hearts = 3;
    else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_HAPPY)    hearts = 3;
    else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_FRIENDLY) hearts = 2;
    else if (pikaStatus2.friendshipLevel > 0)                     hearts = 1;
    for (int i = 0; i < 3; i++) {
        int hx = sx + i * 14;
        int hy = sy + 17;
        if (i < hearts) {
            display->fillRect(hx + 1, hy,     4, 1);
            display->fillRect(hx + 7, hy,     4, 1);
            display->fillRect(hx,     hy + 1, 12, 1);
            display->fillRect(hx + 1, hy + 2, 10, 1);
            display->fillRect(hx + 2, hy + 3, 8, 1);
            display->fillRect(hx + 3, hy + 4, 6, 1);
            display->fillRect(hx + 4, hy + 5, 4, 1);
            display->fillRect(hx + 5, hy + 6, 2, 1);
        } else {
            display->setPixel(hx + 1, hy); display->setPixel(hx + 2, hy);
            display->setPixel(hx + 3, hy); display->setPixel(hx + 4, hy);
            display->setPixel(hx + 7, hy); display->setPixel(hx + 8, hy);
            display->setPixel(hx + 9, hy); display->setPixel(hx + 10, hy);
            display->setPixel(hx,     hy + 1); display->setPixel(hx + 5,  hy + 1);
            display->setPixel(hx + 6, hy + 1); display->setPixel(hx + 11, hy + 1);
            display->setPixel(hx,     hy + 2); display->setPixel(hx + 11, hy + 2);
            display->setPixel(hx + 1, hy + 3); display->setPixel(hx + 10, hy + 3);
            display->setPixel(hx + 2, hy + 4); display->setPixel(hx + 9,  hy + 4);
            display->setPixel(hx + 3, hy + 5); display->setPixel(hx + 8,  hy + 5);
            display->setPixel(hx + 4, hy + 6); display->setPixel(hx + 7,  hy + 6);
            display->setPixel(hx + 5, hy + 7); display->setPixel(hx + 6,  hy + 7);
        }
    }

    snprintf(buf, sizeof(buf), "W:%ld",  (long)pikaStatus2.watts);
    display->drawString(sx, sy + 30, buf);

    snprintf(buf, sizeof(buf), "S:%lu",  (unsigned long)pikaStatus2.steps);
    display->drawString(sx, sy + 46, buf);

    snprintf(buf, sizeof(buf), "T:%lu",  (unsigned long)pikaStatus2.totalSteps);
    display->drawString(sx, sy + 62, buf);

    // Friendship level label
    const char *flabel;
    if      (pikaStatus2.friendshipLevel >= FRIENDSHIP2_LOVING)   flabel = "Loving";
    else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_HAPPY)    flabel = "Happy";
    else if (pikaStatus2.friendshipLevel >= FRIENDSHIP2_FRIENDLY) flabel = "Friendly";
    else if (pikaStatus2.friendshipLevel <= FRIENDSHIP2_MAD)      flabel = "Grumpy";
    else                                                           flabel = "Neutral";
    display->drawString(sx, sy + 78, flabel);

    // Sleep indicator
    if (game2_is_sleep_time())
        display->drawString(sx, sy + 94, "zzz");
}

void PocketPikachu2Module::drawPowScreen(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    display->setColor(WHITE);
    display->fillRect(ox + GAME2_X, oy + GAME2_Y, GAME2_W, GAME2_H);
    display->setColor(BLACK);
    display->setFont(ArialMT_Plain_24);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(ox + GAME2_X + GAME2_W / 2, oy + GAME2_Y + (GAME2_H - 28) / 2, "POW!");
    display->setColor(WHITE);
}

// ─── Carousel frame entry point ───────────────────────────────────────────────

void PocketPikachu2Module::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                                     int16_t x, int16_t y)
{
    uint32_t now = millis();
    // Treat the first drawFrame call AND any return after a >2 s gap as a
    // "focus" event. Without the first-call branch, a freshly-booted device
    // that's currently in sleep hours leaves Pikachu asleep and the user
    // would have to long-press to wake.
    if (lastDrawMs_ == 0 || (now - lastDrawMs_) > 2000)
        game2_on_frame_focused();
    isFrameActive_ = true;
    lastDrawMs_ = now;

    display->clear();
    if (!screenOn2) return;

    // Title bar — full width, above game area and sidebar
    display->setFont(ArialMT_Plain_16);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(x + SCREEN2_W / 2, y + 1, "Pocket Pikachu Color");
    display->drawLine(x, y + TITLE2_H - 1, x + SCREEN2_W - 1, y + TITLE2_H - 1);

    if (menuIsOpen_ || pendingGiftMenu_) {
        // Don't blit directly to TFT while the banner overlay is visible —
        // the direct 2bpp write would overwrite the banner each animation tick.
        drawSidebar(display, x, y);
    } else if (gameState2 == GS2_GIFT_MENU) {
        drawPowScreen(display, x, y);
        drawSidebar(display, x, y);
    } else {
#if defined(USE_ST7789) && !defined(ARCH_ESP32)
        // Color path on T114: paint the sidebar/title into the OLEDDisplay
        // back-buffer and FLUSH IT FIRST. After flush, buffer == buffer_back
        // for the game-area region, so the carousel's next display->display()
        // call (after we return from drawFrame) is a no-op there and won't
        // clobber the RGB565 pixels our blit writes below.
        drawSidebar(display, x, y);
        display->display();
        drawGameArea(display, x, y);
#else
        drawGameArea(display, x, y);
        drawSidebar(display, x, y);
#endif
    }
}

#endif // HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU2
