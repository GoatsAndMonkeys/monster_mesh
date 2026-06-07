#pragma once
// ── BattleWindow ─────────────────────────────────────────────────────────────
// SDL2 graphical battle-screen renderer.  Lives alongside the existing ncurses
// terminal — ncurses still owns input, menus, and every non-battle screen.
// When the user enters BATTLE / BATTLE_END / PVP_BATTLE / PVP_BATTLE_END the
// SDL window pops open and draws the full Gen 1/2 GBC diagonal layout with
// real 56x56 / 48x48 Crystal sprites and per-species palettes.  When the
// user leaves those screens, the window closes.
//
// Logical surface is 640x480 (RetroFlag GPI Case 2W native).  The window is
// resizable; rendering happens at logical 640x480 and SDL scales it to fit
// via SDL_RenderSetLogicalSize.
//
// All input is still routed through ncurses.  The SDL pump only services
// SDL_QUIT (close button) and lets the OS keep the window from going
// "unresponsive".

#include <SDL.h>
#include <stdint.h>
#include <string>
#include <vector>

class BattleWindow {
public:
    // Snapshot of everything the renderer needs for one frame.  TerminalUI
    // builds this each tick and hands it in via setState().
    struct PokeView {
        uint8_t  species  = 0;   // gen 1 dex (1..151)
        uint8_t  level    = 0;
        uint16_t hp       = 0;
        uint16_t maxHp    = 0;
        char     nickname[12] = {};
    };

    struct MoveView {
        char    name[16] = "---";
        uint8_t pp       = 0;
        uint8_t maxPp    = 0;
        bool    slotUsed = false;     // true if mons.moves[i] != 0
    };

    enum class Mode : uint8_t { FIGHT, SWITCH, MESSAGES };

    enum class EndResult : uint8_t { ONGOING, WIN, LOSE, DRAW };

    struct State {
        PokeView foe;
        PokeView you;
        MoveView moves[4];
        int      selectedMove = 0;       // 0..3
        Mode     mode         = Mode::FIGHT;
        std::vector<std::string> log;    // newest at end
        EndResult endResult   = EndResult::ONGOING;
        // PvP/Switch list (optional — only used when mode == SWITCH).
        // Each entry: nickname, level, hp/maxHp, fainted.
        struct SwitchSlot {
            char     nickname[12] = {};
            uint8_t  level        = 0;
            uint16_t hp           = 0;
            uint16_t maxHp        = 0;
            bool     active       = false;
            bool     fainted      = false;
        };
        SwitchSlot switchSlots[6] = {};
        int        switchCount    = 0;
        int        selectedSwitch = 0;
        // Optional header (gym round, PvP enemy name).  Empty == no header.
        char header[64] = {};
        // Local player identity — shown in the YOU status box in place of
        // the "YOU" tag, so the player sees their own short name + trainer
        // name (e.g. "GPI / ASH").  Empty strings fall back to "YOU".
        char localShort[8]   = {};
        char localTrainer[12] = {};
        // Pentest Pikachu ROM: when true there's no move panel (the fight is
        // automatic) — the message log grows to fill the bottom of the screen,
        // and the vulnerability text is pushed into `log`.
        bool pentest          = false;
        // When non-empty, drawn as the foe box's corner tag in place of "FOE"
        // (used to show the WiFi network name; foe.nickname keeps the species).
        char foeTag[24]       = {};
        // Player EXP progress toward the next level, 0..1000 (per-mille).  Drives
        // the EXP bar in the YOU box.
        int  expPermille      = 0;
        // Pentest status overlay: when true the window draws a full-screen
        // status readout (gyms / current-area mons / Pokedex) from statusLines
        // instead of the battle.
        bool showStatus       = false;
        std::vector<std::string> statusLines;
    };

    BattleWindow();
    ~BattleWindow();

    // Create window + renderer + caches.  Returns false silently if SDL is
    // unavailable or the window can't be created — TerminalUI keeps going
    // with ncurses-only in that case.
    bool open();

    // Tear down window + renderer + textures.  Idempotent.
    void close();

    bool isOpen() const { return window_ != nullptr; }

    // Swap in a new state and request a repaint on the next render() call.
    void setState(const State &s);

    // Paint one frame at the current state, present.
    void render();

    // Pump SDL events.  Closes the window on SDL_QUIT.  Ignores keyboard —
    // ncurses still owns input.  Cheap; safe to call once per main-loop tick.
    void pumpEvents();

private:
    SDL_Window   *window_   = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    State         state_;
    bool          initOk_   = false;

    // Layout helpers
    void drawBackground();
    void drawFoePlatform();
    void drawYouPlatform();
    void drawFoeSprite();
    void drawYouSprite();
    void drawFoeBox();
    void drawYouBox();
    void drawMessageLog();
    void drawBottomPanel();
    void drawEndOverlay();

    // Rounded-corner box with optional title chip.  Inner area starts at
    // (x+border, y+border) so callers can fill / write text inside.
    void drawBox(int x, int y, int w, int h, SDL_Color fill, SDL_Color stroke);
    void drawHpBar(int x, int y, int w, int h, uint16_t hp, uint16_t maxHp);
    void drawExpBar(int x, int y, int w, int h);  // cosmetic placeholder
    void drawShadowEllipse(int cx, int cy, int rx, int ry, SDL_Color c);
};
