#pragma once
#include <LovyanGFX.hpp>
#include "DungeonGame.h"

// ── DungeonOverlay ────────────────────────────────────────────────────────────
// Full-screen text overlay for the dungeon game.
// Shows: title bar, party HP, last N log lines, current prompt.
// Renders only when DungeonGame::consumeDirty() returns true.

static constexpr uint16_t DUNGEON_DISP_W = 320;
static constexpr uint16_t DUNGEON_DISP_H = 240;

class DungeonOverlay {
public:
    explicit DungeonOverlay(DungeonGame &game) : game_(game) {}

    bool isActive()    const { return active_; }
    void open()              { active_ = true;  forceRedraw_ = true; }
    void close()             { active_ = false; }
    void toggle()            { if (active_) close(); else open(); }
    void forceRedraw()       { forceRedraw_ = true; }

    void render(lgfx::LGFX_Device *gfx) {
        if (!active_ || !gfx) return;
        if (!game_.consumeDirty() && !forceRedraw_) return;
        forceRedraw_ = false;

        gfx->fillScreen(TFT_BLACK);

        // ── Title bar ────────────────────────────────────────────────────────
        const uint16_t TITLE_BG = 0x1082;
        gfx->fillRect(0, 0, DUNGEON_DISP_W, 14, TITLE_BG);
        gfx->setTextColor(TFT_CYAN, TITLE_BG);
        gfx->setTextSize(1);

        char title[40];
        if (game_.isHost()) {
            snprintf(title, sizeof(title), "D&MonstersMesh [HOST] F%u",
                     game_.floorDepth());
        } else {
            snprintf(title, sizeof(title), "D&MonstersMesh F%u",
                     game_.floorDepth());
        }
        gfx->setCursor(4, 3);
        gfx->print(title);

        const char *phaseStr = "LOBBY";
        switch (game_.phase()) {
            case RunPhase::Exploring:     phaseStr = "EXPLORE"; break;
            case RunPhase::InCombat:      phaseStr = "COMBAT";  break;
            case RunPhase::Trivia:        phaseStr = "TRIVIA";  break;
            case RunPhase::WordleRecovery:phaseStr = "WORDLE";  break;
            case RunPhase::HackMinigame:  phaseStr = "HACK";    break;
            case RunPhase::FloorComplete: phaseStr = "CLEAR!";  break;
            case RunPhase::RunOver:       phaseStr = "GAME OVR";break;
            default: break;
        }
        gfx->setTextColor(TFT_YELLOW, TITLE_BG);
        // Right-align phase string
        int16_t phaseW = strlen(phaseStr) * 6;
        gfx->setCursor(DUNGEON_DISP_W - 4 - phaseW, 3);
        gfx->print(phaseStr);

        // ── Party HP bar ─────────────────────────────────────────────────────
        xSemaphoreTake(game_.mutex(), portMAX_DELAY);
        uint8_t ps = game_.partySize();
        uint16_t barY = 18;
        gfx->setTextSize(1);

        for (uint8_t i = 0; i < ps && i < 5; i++) {
            const DungeonTrainer &t = game_.run().party[i];
            uint8_t slot = t.activeSlot;
            uint16_t hp  = t.slotHp[slot];
            bool alive   = !t.slotFainted[slot];

            uint16_t col = alive ? TFT_GREEN : 0x632C;
            gfx->setTextColor(col, TFT_BLACK);

            char hpLine[32];
            snprintf(hpLine, sizeof(hpLine), "P%u HP:%3u%s",
                     i + 1, hp, alive ? "" : " [FAINTED]");
            gfx->setCursor(4, barY + i * 10);
            gfx->print(hpLine);
        }
        xSemaphoreGive(game_.mutex());

        // ── Separator ────────────────────────────────────────────────────────
        uint16_t sepY = barY + ps * 10 + 2;
        gfx->drawFastHLine(0, sepY, DUNGEON_DISP_W, 0x2945);

        // ── Log lines ────────────────────────────────────────────────────────
        uint16_t logY = sepY + 4;
        uint8_t maxLines = (DUNGEON_DISP_H - logY - 16) / 10;
        if (maxLines > DLOG_LINES) maxLines = DLOG_LINES;

        gfx->setTextSize(1);

        xSemaphoreTake(game_.mutex(), portMAX_DELAY);
        const DungeonLog &log = game_.log();
        uint8_t logCount = log.count;
        uint8_t startIdx = (logCount > maxLines) ? logCount - maxLines : 0;
        for (uint8_t i = 0; i < maxLines && (startIdx + i) < logCount; i++) {
            const char *line = log.get(startIdx + i);
            uint16_t col = TFT_WHITE;
            if (line[0] == '[' && line[1] == 'D' && line[2] == ']') col = 0xAD55;
            else if (line[0] == '[' && line[1] == 'P' && line[2] == 'C') col = TFT_GREEN;
            else if (strncmp(line, "===", 3) == 0) col = TFT_YELLOW;
            else if (strncmp(line, "A wild", 6) == 0) col = TFT_RED;
            gfx->setTextColor(col, TFT_BLACK);
            gfx->setCursor(4, logY + i * 10);
            gfx->print(line);
        }
        xSemaphoreGive(game_.mutex());

        // ── Prompt bar ───────────────────────────────────────────────────────
        gfx->fillRect(0, DUNGEON_DISP_H - 14, DUNGEON_DISP_W, 14, 0x18E3);
        gfx->setTextColor(0xAD55, 0x18E3);
        gfx->setTextSize(1);
        const char *hint = game_.isHost()
            ? "host> attack/switch/item/rest/party/start"
            : "guest> attack/switch/item/rest/party";
        gfx->setCursor(4, DUNGEON_DISP_H - 11);
        gfx->print(hint);
    }

private:
    DungeonGame &game_;
    bool         active_      = false;
    bool         forceRedraw_ = false;
};
