#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "EmulatorApp.h"
#include "PokemonData.h"
#include "BattleShim.h"

// ── WRAM debug overlay ────────────────────────────────────────────────────────
// Toggle with Tab key (0x09) via InputMap::consumeDebugToggle().
// Rendered on Core 1 after each gb_run_frame().
//
// Displays:
//   • Battle state byte and type
//   • Link state + player number
//   • RNG seed bytes
//   • Party count and species IDs
//   • First party mon (level + HP)
//   • BattleShim LoRa connection state

class DebugOverlay {
public:
    DebugOverlay(TFT_eSPI &tft, EmulatorApp &emu, BattleShim *shim = nullptr)
        : tft_(tft), emu_(emu), shim_(shim) {}

    bool isActive() const { return active_; }
    void toggle()         { active_ = !active_; }
    void setShim(BattleShim *s) { shim_ = s; }

    // Call every frame from emuTask (Core 1). No-op when inactive.
    void render() {
        if (!active_) return;

        static constexpr int16_t PX = 2,  PY = 2;
        static constexpr int16_t PW = 212, PH = 176;
        tft_.fillRect(PX, PY, PW, PH, 0x0821);  // very dark blue background
        tft_.setTextColor(TFT_GREEN, 0x0821);
        tft_.setTextSize(1);
        tft_.setTextDatum(TL_DATUM);

        int16_t cx = PX + 4;
        int16_t y  = PY + 4;
        char    buf[52];

// ── local row helpers (macros, undef'd at end of function) ───────────────
#define ROW(s)    do { tft_.drawString((s), cx, y); y += 10; } while (0)
#define ROWF(...) do { snprintf(buf, sizeof(buf), __VA_ARGS__); ROW(buf); } while (0)

        ROW("--- WRAM DEBUG ---");

        // Battle state
        uint8_t inBattle = emu_.readWRAM(Gen1::wIsInBattle);
        uint8_t btlType  = emu_.readWRAM(Gen1::wBattleType);
        ROWF("Battle:%d  Type:%d", inBattle, btlType);

        // Link state
        uint8_t linkSt = emu_.readWRAM(Gen1::wLinkState);
        uint8_t plNum  = emu_.readWRAM(Gen1::wLinkPlayerNumber);
        ROWF("Link:0x%02X  Player:%d", linkSt, plNum);

        // RNG
        uint8_t rAdd = emu_.readWRAM(Gen1::hRandomAdd);
        uint8_t rSub = emu_.readWRAM(Gen1::hRandomSub);
        ROWF("RNG:  %02X / %02X", rAdd, rSub);

        // Party
        uint8_t count = emu_.readWRAM(Gen1::wPartyCount);
        ROWF("Party: %d mon(s)", count);

        {
            // Species IDs
            char sp[32];
            char *p = sp;
            p += snprintf(sp, sizeof(sp), "Sp:");
            for (uint8_t i = 0; i < count && i < 6; i++) {
                uint8_t s = emu_.readWRAM(Gen1::wPartySpecies + i);
                p += snprintf(p, sp + sizeof(sp) - p, " %02X", s);
            }
            ROW(sp);
        }

        // First mon detail
        if (count > 0) {
            uint8_t  lv  = emu_.readWRAM(Gen1::wPartyMons + 0x21);
            uint16_t hp  = ((uint16_t)emu_.readWRAM(Gen1::wPartyMons + 0x01) << 8)
                         |  emu_.readWRAM(Gen1::wPartyMons + 0x02);
            uint16_t mhp = ((uint16_t)emu_.readWRAM(Gen1::wPartyMons + 0x22) << 8)
                         |  emu_.readWRAM(Gen1::wPartyMons + 0x23);
            ROWF("Mon0  Lv%d  HP %d/%d", lv, hp, mhp);
        }

        // BattleShim state
        if (shim_) {
            static const char *const kStateNames[] =
                { "IDLE", "ADVERT", "CONN", "BATTLE", "DONE" };
            auto si = (uint8_t)shim_->state();
            ROWF("LoRa: %s %s  sid=%04X",
                 si < 5 ? kStateNames[si] : "?",
                 shim_->isMaster() ? "MSTR" : "SLAV",
                 shim_->sessionId());
        } else {
            ROW("LoRa: no shim");
        }

        ROW("[Tab] to close");

#undef ROWF
#undef ROW
    }

private:
    TFT_eSPI    &tft_;
    EmulatorApp &emu_;
    BattleShim  *shim_;
    bool         active_ = false;
};
