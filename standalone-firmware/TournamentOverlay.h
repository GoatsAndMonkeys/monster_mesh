#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "TournamentCoordinator.h"
#include "TournamentClient.h"
#include "Lobby.h"

// ── TournamentOverlay ────────────────────────────────────────────────────────
// Full-screen UI for tournament discovery, creation, registration, and bracket
// viewing.  Toggled by 'T' key.

class TournamentOverlay {
public:
    TournamentOverlay(TFT_eSPI &tft, TournamentCoordinator &coord,
                      TournamentClient &client, Lobby &lobby)
        : tft_(tft), coord_(coord), client_(client), lobby_(lobby) {}

    bool isActive() const { return visible_; }
    void toggle() { visible_ = !visible_; cursor_ = 0; }
    void open()   { visible_ = true;  cursor_ = 0; }
    void close()  { visible_ = false; }

    // ── Key input (returns true if consumed) ─────────────────────────────────
    bool handleKey(uint8_t ascii) {
        if (!visible_) return false;

        switch (ascii) {
            case 'w': case 'W': cursorUp();    return true;
            case 's': case 'S': cursorDown();  return true;
            case 'k': case 'K': select();      return true;
            case 'l': case 'L': back();        return true;
            default: return false;
        }
    }

    // ── Render ───────────────────────────────────────────────────────────────
    void render() {
        if (!visible_) return;

        tft_.fillScreen(TFT_BLACK);
        tft_.setTextColor(TFT_WHITE, TFT_BLACK);
        tft_.setTextSize(2);
        tft_.setTextDatum(TC_DATUM);
        tft_.drawString("TOURNAMENT", 160, 4);
        tft_.setTextSize(1);

        // Check if we're coordinating
        if (coord_.phase() != TournamentCoordinator::Phase::IDLE) {
            renderCoordinator();
            return;
        }

        // Check client state
        auto cs = client_.state();
        switch (cs) {
            case TournamentClient::ClientState::IDLE:
            case TournamentClient::ClientState::DISCOVERED:
                renderDiscovery();
                break;
            case TournamentClient::ClientState::REGISTERED:
            case TournamentClient::ClientState::WAITING:
                renderWaiting();
                break;
            case TournamentClient::ClientState::MATCHED:
                renderMatched();
                break;
            case TournamentClient::ClientState::REPORTING:
                renderReporting();
                break;
        }

        // Footer
        tft_.setTextColor(0xAD55, TFT_BLACK);
        tft_.setTextDatum(BC_DATUM);
        tft_.drawString("W/S=scroll  K=select  L=back  T=close", 160, 236);
    }

private:
    TFT_eSPI               &tft_;
    TournamentCoordinator  &coord_;
    TournamentClient       &client_;
    Lobby                  &lobby_;
    bool                    visible_ = false;
    uint8_t                 cursor_  = 0;

    // ── Sub-screens ──────────────────────────────────────────────────────────

    void renderDiscovery() {
        tft_.setTextDatum(TL_DATUM);
        int16_t y = 28;

        // "Create Tournament" option always at top
        if (cursor_ == 0) {
            tft_.fillRect(0, y - 1, 320, 12, 0x1082);
            tft_.setTextColor(TFT_YELLOW, 0x1082);
        } else {
            tft_.setTextColor(TFT_GREEN, TFT_BLACK);
        }
        tft_.drawString("+ Create New Tournament", 4, y);
        y += 16;

        // Discovered tournaments
        uint8_t count = client_.discoveredCount();
        if (count == 0) {
            tft_.setTextColor(0xAD55, TFT_BLACK);
            tft_.drawString("Scanning for nearby tournaments...", 4, y);
        } else {
            tft_.setTextColor(0xAD55, TFT_BLACK);
            tft_.drawString("NAME             TYPE   PLAYERS", 4, y);
            y += 12;

            for (uint8_t i = 0; i < count && i < 4; i++) {
                const auto &t = client_.discovered(i);
                if (cursor_ == i + 1) {
                    tft_.fillRect(0, y - 1, 320, 12, 0x1082);
                    tft_.setTextColor(TFT_YELLOW, 0x1082);
                } else {
                    tft_.setTextColor(TFT_WHITE, TFT_BLACK);
                }

                const char *typeStr = (t.type == 1) ? "DBLEL" : "SINGL";
                char row[48];
                snprintf(row, sizeof(row), "%-16s %5s  %u/%u",
                         t.name, typeStr, t.currentPlayers, t.maxPlayers);
                tft_.drawString(row, 4, y);
                y += 14;
            }
        }
    }

    void renderCoordinator() {
        tft_.setTextDatum(TL_DATUM);
        int16_t y = 28;

        char info[48];
        snprintf(info, sizeof(info), "Hosting: %s", coord_.name());
        tft_.setTextColor(TFT_GREEN, TFT_BLACK);
        tft_.drawString(info, 4, y); y += 14;

        snprintf(info, sizeof(info), "Players: %u  Round: %u  Phase: %u",
                 coord_.playerCount(), coord_.currentRound(),
                 (unsigned)coord_.phase());
        tft_.setTextColor(TFT_WHITE, TFT_BLACK);
        tft_.drawString(info, 4, y); y += 16;

        // Show bracket
        renderBracketView(y);

        // Start button if still in registration
        if (coord_.phase() == TournamentCoordinator::Phase::REGISTRATION) {
            tft_.setTextColor(TFT_GREEN, TFT_BLACK);
            tft_.setTextDatum(BC_DATUM);
            tft_.drawString("K = Start Tournament", 160, 220);
        }
    }

    void renderBracketView(int16_t startY) {
        tft_.setTextColor(0xAD55, TFT_BLACK);
        tft_.drawString("BRACKET:", 4, startY);
        int16_t y = startY + 12;

        uint8_t count = client_.bracketMatchCount();
        if (count == 0 && coord_.phase() != TournamentCoordinator::Phase::IDLE) {
            count = coord_.matchCount();
        }

        for (uint8_t i = 0; i < count && y < 210; i++) {
            char matchStr[48];
            if (coord_.phase() != TournamentCoordinator::Phase::IDLE) {
                const auto &m = coord_.match(i);
                const char *s1 = (m.seed1 < coord_.playerCount()) ?
                    coord_.player(m.seed1).name : "BYE";
                const char *s2 = (m.seed2 < coord_.playerCount()) ?
                    coord_.player(m.seed2).name : "BYE";
                const char *res = (m.result == 0) ? "..." :
                                  (m.result == 1) ? "<W" :
                                  (m.result == 2) ? "W>" : "T/O";
                snprintf(matchStr, sizeof(matchStr), "R%u #%u: %-8s vs %-8s %s",
                         m.round, m.matchId, s1, s2, res);
            } else {
                const auto &e = client_.bracketEntry(i);
                snprintf(matchStr, sizeof(matchStr), "#%u: S%u vs S%u [%u]",
                         i, e.seed1, e.seed2, e.result);
            }

            tft_.setTextColor(TFT_WHITE, TFT_BLACK);
            tft_.drawString(matchStr, 4, y);
            y += 12;
        }
    }

    void renderWaiting() {
        tft_.setTextSize(2);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(TFT_WHITE, TFT_BLACK);
        tft_.drawString("Registered!", 160, 80);
        tft_.setTextSize(1);
        tft_.drawString("Waiting for match assignment...", 160, 110);

        char seedInfo[32];
        snprintf(seedInfo, sizeof(seedInfo), "Your seed: #%u", client_.mySeed());
        tft_.drawString(seedInfo, 160, 130);
    }

    void renderMatched() {
        tft_.setTextSize(2);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft_.drawString("MATCH TIME!", 160, 80);
        tft_.setTextSize(1);
        tft_.setTextColor(TFT_WHITE, TFT_BLACK);

        char opp[40];
        snprintf(opp, sizeof(opp), "Opponent: 0x%08X", (unsigned)client_.opponentChipId());
        tft_.drawString(opp, 160, 110);
        tft_.drawString("Open Lobby (P) to challenge them!", 160, 130);
    }

    void renderReporting() {
        tft_.setTextSize(1);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(TFT_WHITE, TFT_BLACK);
        tft_.drawString("Reporting result to coordinator...", 160, 120);
    }

    // ── Navigation ───────────────────────────────────────────────────────────

    void cursorUp() {
        if (cursor_ > 0) cursor_--;
    }

    void cursorDown() {
        uint8_t maxIdx = client_.discoveredCount(); // +1 for "Create" option
        if (cursor_ < maxIdx) cursor_++;
    }

    void select() {
        auto cs = client_.state();

        // Coordinator: start tournament
        if (coord_.phase() == TournamentCoordinator::Phase::REGISTRATION) {
            coord_.startTournament();
            return;
        }

        // Discovery: cursor 0 = create, 1+ = join
        if (cs == TournamentClient::ClientState::IDLE ||
            cs == TournamentClient::ClientState::DISCOVERED) {
            if (cursor_ == 0) {
                // Create new tournament
                coord_.create(TournamentCoordinator::TournamentType::DOUBLE_ELIM,
                             16, "POKEMESH CUP");
                return;
            }
            // Join selected tournament
            const auto &st = lobby_.stats();
            char name[11];
            // Use first peer name or default
            strncpy(name, "TRAINER", 10);
            client_.registerFor(cursor_ - 1, name, st.elo);
        }
    }

    void back() {
        auto cs = client_.state();
        if (cs == TournamentClient::ClientState::DISCOVERED) {
            close();
        } else {
            close();
        }
    }
};
