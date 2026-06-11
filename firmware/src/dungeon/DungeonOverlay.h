#pragma once
#include "../Display.h"
#include "DungeonGame.h"

// ── DungeonOverlay ────────────────────────────────────────────────────────────
// Full-screen text overlay for the dungeon game.
// Shows: title bar, party HP, last N log lines, current prompt.
// Renders only when DungeonGame::consumeDirty() returns true.

class DungeonOverlay {
public:
    DungeonOverlay(TFT_eSPI &tft, DungeonGame &game)
        : tft_(tft), game_(game) {}

    bool isActive() const { return active_; }

    void open() {
        active_ = true;
        forceRedraw_ = true;
    }

    void close() {
        active_ = false;
    }

    void toggle() {
        if (active_) close(); else open();
    }

    void render() {
        if (!active_) return;
        if (!game_.consumeDirty() && !forceRedraw_) return;
        forceRedraw_ = false;

        tft_.fillScreen(TFT_BLACK);

        // ── Title bar ────────────────────────────────────────────────────────
        const uint16_t TITLE_BG = 0x1082;  // very dark blue
        tft_.fillRect(0, 0, DISP_W, 14, TITLE_BG);
        tft_.setTextColor(TFT_CYAN, TITLE_BG);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextSize(1);

        char title[40];
        if (game_.isHost()) {
            snprintf(title, sizeof(title), "Dungeons & MonstersMesh [HOST] F%u",
                     game_.floorDepth());
        } else {
            snprintf(title, sizeof(title), "Dungeons & MonstersMesh F%u",
                     game_.floorDepth());
        }
        tft_.drawString(title, 4, 7);

        const char *phaseStr = "LOBBY";
        switch (game_.phase()) {
            case RunPhase::Exploring:    phaseStr = "EXPLORE"; break;
            case RunPhase::InCombat:     phaseStr = "COMBAT";  break;
            case RunPhase::Trivia:       phaseStr = "TRIVIA";  break;
            case RunPhase::WordleRecovery: phaseStr = "WORDLE"; break;
            case RunPhase::HackMinigame: phaseStr = "HACK";    break;
            case RunPhase::FloorComplete:phaseStr = "CLEAR!";  break;
            case RunPhase::RunOver:      phaseStr = "GAME OVR";break;
            default: break;
        }
        tft_.setTextDatum(MR_DATUM);
        tft_.setTextColor(TFT_YELLOW, TITLE_BG);
        tft_.drawString(phaseStr, DISP_W - 4, 7);

        // ── Party HP bar ─────────────────────────────────────────────────────
        xSemaphoreTake(game_.mutex(), portMAX_DELAY);
        uint8_t ps = game_.partySize();
        uint16_t barY = 18;
        tft_.setTextSize(1);
        tft_.setTextDatum(ML_DATUM);

        for (uint8_t i = 0; i < ps && i < 5; i++) {
            const DungeonTrainer &t = game_.run().party[i];
            uint8_t slot = t.activeSlot;
            uint16_t hp  = t.slotHp[slot];
            bool alive   = !t.slotFainted[slot];

            uint16_t col = alive ? TFT_GREEN : 0x632C;  // green or dim red
            tft_.setTextColor(col, TFT_BLACK);

            char hpLine[32];
            snprintf(hpLine, sizeof(hpLine), "P%u HP:%3u%s",
                     i + 1, hp, alive ? "" : " [FAINTED]");

            // Mark my own slot with >
            if (t.nodeId == (uint32_t)0) {  // nodeId 0 = self placeholder for now
                tft_.setTextColor(TFT_WHITE, TFT_BLACK);
            }
            tft_.drawString(hpLine, 4, barY + i * 10);
        }
        xSemaphoreGive(game_.mutex());

        // ── Separator ────────────────────────────────────────────────────────
        uint16_t sepY = barY + ps * 10 + 2;
        tft_.drawFastHLine(0, sepY, DISP_W, 0x2945);  // dark grey

        // ── Log lines ────────────────────────────────────────────────────────
        uint16_t logY = sepY + 4;
        uint8_t maxLines = (DISP_H - logY - 16) / 10;  // leave room for prompt
        if (maxLines > DLOG_LINES) maxLines = DLOG_LINES;

        tft_.setTextSize(1);
        tft_.setTextDatum(ML_DATUM);

        xSemaphoreTake(game_.mutex(), portMAX_DELAY);
        const DungeonLog &log = game_.log();
        uint8_t logCount = log.count;
        uint8_t startIdx = (logCount > maxLines) ? logCount - maxLines : 0;
        for (uint8_t i = 0; i < maxLines && (startIdx + i) < logCount; i++) {
            const char *line = log.get(startIdx + i);
            uint16_t col = TFT_WHITE;
            // Color coding based on prefix
            if (line[0] == '[' && line[1] == 'D' && line[2] == ']') col = 0xAD55;  // grey
            else if (line[0] == '[' && line[1] == 'P' && line[2] == 'C') col = TFT_GREEN;
            else if (strncmp(line, "===", 3) == 0) col = TFT_YELLOW;
            else if (strncmp(line, "A wild", 6) == 0) col = TFT_RED;
            tft_.setTextColor(col, TFT_BLACK);
            tft_.drawString(line, 4, logY + i * 10);
        }
        xSemaphoreGive(game_.mutex());

        // ── Prompt bar at bottom ──────────────────────────────────────────────
        tft_.fillRect(0, DISP_H - 14, DISP_W, 14, 0x18E3);
        tft_.setTextColor(0xAD55, 0x18E3);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextSize(1);

        const char *hint = game_.isHost()
            ? "host> attack/switch/item/rest/party/start"
            : "guest> attack/switch/item/rest/party";
        tft_.drawString(hint, 4, DISP_H - 7);
    }

    void forceRedraw() { forceRedraw_ = true; }

private:
    TFT_eSPI    &tft_;
    DungeonGame &game_;
    bool         active_      = false;
    bool         forceRedraw_ = false;
};
