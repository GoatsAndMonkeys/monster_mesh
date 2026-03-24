#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Lobby.h"

// ── Lobby overlay ───────────────────────────────────────────────────────────
// Full-screen UI drawn on top of the game when the lobby is open.
// Driven by 'P' key toggle. Captures W/S/K/L keys for navigation.

class LobbyOverlay {
public:
    LobbyOverlay(TFT_eSPI &tft, Lobby &lobby)
        : tft_(tft), lobby_(lobby) {}

    // Returns true if lobby is visible and consuming input
    bool isActive() const { return lobby_.isOpen(); }

    void render() {
        if (!lobby_.isOpen()) return;

        // ── Background ─────────────────────────────────────────────────────
        tft_.fillScreen(TFT_BLACK);
        tft_.setTextColor(TFT_WHITE, TFT_BLACK);
        tft_.setTextSize(2);
        tft_.setTextDatum(TC_DATUM);
        tft_.drawString("POKEMESH LOBBY", 160, 4);

        // ── Stats bar ──────────────────────────────────────────────────────
        const auto &st = lobby_.stats();
        tft_.setTextSize(1);
        tft_.setTextDatum(TL_DATUM);
        char statBuf[48];
        snprintf(statBuf, sizeof(statBuf), "ELO: %u  W:%u L:%u D:%u",
                 st.elo, st.wins, st.losses, st.draws);
        tft_.drawString(statBuf, 4, 24);

        // ── State-dependent content ────────────────────────────────────────
        auto state = lobby_.state();

        if (state == Lobby::State::CHALLENGING) {
            tft_.setTextSize(2);
            tft_.setTextDatum(MC_DATUM);
            tft_.drawString("Challenging...", 160, 120);
            tft_.setTextSize(1);
            tft_.drawString("(L to cancel)", 160, 150);
            return;
        }

        if (state == Lobby::State::INCOMING) {
            tft_.setTextSize(2);
            tft_.setTextDatum(MC_DATUM);
            tft_.drawString("CHALLENGE!", 160, 90);
            tft_.setTextSize(1);

            tft_.drawString("K = Accept   L = Reject", 160, 130);
            return;
        }

        if (state == Lobby::State::PAIRED) {
            tft_.setTextSize(2);
            tft_.setTextDatum(MC_DATUM);
            tft_.drawString("Paired!", 160, 110);
            tft_.setTextSize(1);
            tft_.drawString("Enter Cable Club in-game", 160, 140);
            return;
        }

        // ── BROWSING: show peer list ───────────────────────────────────────
        uint8_t count = lobby_.peerCount();
        if (count == 0) {
            tft_.setTextSize(1);
            tft_.setTextDatum(MC_DATUM);
            tft_.drawString("No nearby trainers found", 160, 100);
            tft_.drawString("Beaconing every 2 min...", 160, 115);
            tft_.drawString("P = close lobby", 160, 140);
            return;
        }

        tft_.setTextDatum(TL_DATUM);
        tft_.setTextSize(1);

        // Header
        tft_.setTextColor(0xAD55, TFT_BLACK);  // grey
        tft_.drawString("TRAINER    ROM  LEAD       LV  ELO", 4, 40);
        tft_.setTextColor(TFT_WHITE, TFT_BLACK);

        uint8_t cursor = lobby_.cursor();
        int16_t y = 52;
        for (uint8_t i = 0; i < count && i < 7; i++) {  // max 7 visible rows
            const auto &p = lobby_.peer(i);

            // ROM version tag
            const char *romTag = "???";
            switch (p.romVersion) {
                case RomVersion::RED:     romTag = "RED"; break;
                case RomVersion::BLUE:    romTag = "BLU"; break;
                case RomVersion::YELLOW:  romTag = "YEL"; break;
                case RomVersion::GOLD:    romTag = "GLD"; break;
                case RomVersion::SILVER:  romTag = "SLV"; break;
                case RomVersion::CRYSTAL: romTag = "CRY"; break;
                default: break;
            }

            // Highlight selected row
            uint16_t bg = TFT_BLACK;
            if (i == cursor) {
                bg = 0x1082;  // dark blue
                tft_.fillRect(0, y - 1, 320, 12, bg);
                tft_.setTextColor(TFT_YELLOW, bg);
            } else {
                tft_.setTextColor(TFT_WHITE, TFT_BLACK);
            }

            char row[54];
            snprintf(row, sizeof(row), "%-10s %3s  %-10s %2u  %4u",
                     p.name, romTag, p.leadName, p.leadLevel, p.elo);
            tft_.drawString(row, 4, y);

            // Show level range on second half-line if extended beacon
            if (p.beaconVersion >= 1 && i == cursor) {
                char detail[40];
                snprintf(detail, sizeof(detail), "Lv%u-%u  %umon  %u games",
                         p.partyMinLevel, p.partyMaxLevel,
                         p.partyCount, p.totalBattles);
                tft_.setTextSize(1);
                tft_.setTextColor(0xAD55, bg);
                tft_.drawString(detail, 4, y + 11);
                tft_.setTextColor(TFT_YELLOW, bg);
            }

            y += (i == cursor && p.beaconVersion >= 1) ? 24 : 14;
        }

        // Footer
        tft_.setTextColor(0xAD55, TFT_BLACK);
        tft_.setTextDatum(BC_DATUM);
        tft_.drawString("W/S=scroll  K=challenge  T=tourney  P=close", 160, 236);
    }

    // Handle key input while lobby is open
    // Returns true if key was consumed
    bool handleKey(uint8_t ascii) {
        if (!lobby_.isOpen()) return false;

        switch (ascii) {
            case 'w': case 'W': lobby_.navigateUp();      return true;
            case 's': case 'S': lobby_.navigateDown();    return true;
            case 'k': case 'K': lobby_.selectPeer();      return true;
            case 'l': case 'L': {
                auto st = lobby_.state();
                if (st == Lobby::State::INCOMING) {
                    lobby_.rejectIncoming();
                } else if (st == Lobby::State::CHALLENGING) {
                    lobby_.close();
                    lobby_.open();  // back to browsing
                }
                return true;
            }
            default: return false;
        }
    }

private:
    TFT_eSPI &tft_;
    Lobby    &lobby_;
};
