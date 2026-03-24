#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "BattleShim.h"

// ── Status bar overlay ────────────────────────────────────────────────────────
// Always-on (no toggle). Renders a single line at the bottom of the screen
// only when BattleShim is in a transient state (ADVERTISING / CONNECTED).
// No-op for all other states, so zero cost during normal gameplay.

class Lobby;  // forward declare

class StatusOverlay {
public:
    StatusOverlay(TFT_eSPI &tft, BattleShim *shim)
        : tft_(tft), shim_(shim) {}

    void setLobby(Lobby *lobby) { lobby_ = lobby; }

    void render() {
        if (!shim_) return;
        // Don't draw over the lobby screen
        if (lobby_ && lobbyIsOpen()) return;

        auto st = shim_->state();
        bool shouldDraw = (st == BattleShim::State::ADVERTISING ||
                           st == BattleShim::State::CONNECTED);

        if (!shouldDraw) {
            // If we were previously showing the bar, clear it once
            if (wasShowing_) {
                tft_.fillRect(0, BAR_Y, 320, BAR_H, TFT_BLACK);
                wasShowing_ = false;
            }
            return;
        }
        wasShowing_ = true;

        // ── Draw bar background ──────────────────────────────────────────
        tft_.fillRect(0, BAR_Y, 320, BAR_H, 0x18E3);  // dark charcoal
        tft_.setTextColor(TFT_WHITE, 0x18E3);
        tft_.setTextSize(1);
        tft_.setTextDatum(MC_DATUM);

        char buf[40];
        if (st == BattleShim::State::ADVERTISING) {
            // Pulsing dots: cycle  .  ..  ...  every ~20 frames (~333 ms)
            uint8_t dots = (frame_ / 20) % 4;
            const char *trail = dots == 0 ? "" : dots == 1 ? "." : dots == 2 ? ".." : "...";
            snprintf(buf, sizeof(buf), "Searching for opponent%s", trail);
        } else {
            snprintf(buf, sizeof(buf), "Opponent found!");
        }

        tft_.drawString(buf, 160, BAR_Y + BAR_H / 2);
        frame_++;
    }

private:
    static constexpr int16_t BAR_Y = 226;  // bottom of 240px screen
    static constexpr int16_t BAR_H = 14;

    TFT_eSPI    &tft_;
    BattleShim  *shim_;
    Lobby       *lobby_ = nullptr;
    bool         wasShowing_ = false;
    uint16_t     frame_      = 0;

    // Avoid including Lobby.h — just check state via pointer
    bool lobbyIsOpen() const;
};
