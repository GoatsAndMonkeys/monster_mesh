#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include "peanut_gb.h"
#include "InputMap.h"
#include "ISerialLink.h"
#include "PokemonData.h"
#include "pins.h"

// ── Display / viewport geometry ───────────────────────────────────────────────
// GB output:     160 × 144 px
// T-Deck screen: 320 × 240 px  (landscape)
//
// 2× pixel scaling → rendered size: 320 × 288 px
// The screen is 240 px tall, so 48 px overflow (288 − 240 = 48).
//
// viewportY_ is the pixel offset of GB line 0 from the top of the screen:
//   viewportY_ =  0  → top-aligned    (GB lines  0..119 visible, 120..143 hidden)
//   viewportY_ = 24  → centred        (GB lines  6..125 visible) ← default
//   viewportY_ = 48  → bottom-aligned (GB lines 24..143 visible, 0..23  hidden)
//
// Trackball UP   → viewportY_ decreases (peek at top of frame)
// Trackball DOWN → viewportY_ increases (peek at bottom of frame)
// Trackball PRESS → re-center (viewportY_ = VIEWPORT_CENTER)

#define GB_SCREEN_W          160
#define GB_SCREEN_H          144
#define DISP_W               320
#define DISP_H               240
#define GB_SCALE             2
#define VIEWPORT_MIN         0
#define VIEWPORT_MAX         48      // (GB_SCREEN_H * GB_SCALE) − DISP_H
#define VIEWPORT_CENTER      24      // VIEWPORT_MAX / 2
#define VIEWPORT_SCROLL_STEP 4       // screen pixels per trackball tick (= 2 GB lines)

// ── Classic DMG palette (RGB565, shade 0=lightest, 3=darkest) ────────────────
static const uint16_t DMG_PALETTE[4] = {
    0xFFFF,  // 0 → white
    0xAD55,  // 1 → light grey
    0x52AA,  // 2 → dark grey
    0x0000,  // 3 → black
};

// ── Gen 1 WRAM addresses (Pokémon Red/Blue) ───────────────────────────────────
// Used by BattleShim (Phase 5) and the debug overlay (Phase 2).
namespace PokeRed {
    static constexpr uint16_t wPartyCount       = 0xD163;
    static constexpr uint16_t wPartySpecies      = 0xD164; // 6 bytes (+ terminator)
    static constexpr uint16_t wPartyMons         = 0xD16B; // 6 × 44 bytes
    static constexpr uint16_t wEnemyPartyCount   = 0xD89C;
    static constexpr uint16_t wEnemyMons         = 0xD8A4; // 6 × 44 bytes
    static constexpr uint16_t wIsInBattle        = 0xD057;
    static constexpr uint16_t wBattleType        = 0xD05A; // 0=wild, 1=trainer, 2=link
    static constexpr uint16_t hRandomAdd         = 0xFFD3; // HRAM — RNG state
    static constexpr uint16_t hRandomSub         = 0xFFD4;
    static constexpr uint16_t PARTY_MON_SIZE     = 44;
    static constexpr uint16_t PARTY_MAX          = 6;
}

class EmulatorApp {
public:
    EmulatorApp(TFT_eSPI &tft, InputMap &input)
        : tft_(tft), input_(input) {}

    // Load ROM from SD card at romPath, initialise emulator.
    // Returns false on any error (no ROM, bad ROM, PSRAM alloc failure).
    bool begin(const char *romPath = "/pokemon.gb");

    // Advance one emulator frame (~60 Hz). Called from emuTask on Core 1.
    void runFrame();

    // ── WRAM / HRAM access (used by BattleShim + debug overlay) ─────────────
    // Pass the full Game Boy address (e.g. 0xD163 for wPartyCount).
    // WRAM: 0xC000–0xDFFF  →  gb_->wram[addr − 0xC000]
    // HRAM/IO: 0xFF00–0xFFFE →  gb_->hram_io[addr − 0xFF00]
    uint8_t readWRAM(uint16_t gbAddr) const;
    void    writeWRAM(uint16_t gbAddr, uint8_t value);
    void    readWRAMRange(uint16_t gbAddr, uint8_t *buf, size_t len) const;
    void    writeWRAMRange(uint16_t gbAddr, const uint8_t *buf, size_t len);

    bool isRunning() const { return running_; }

    // ROM version detection (reads ROM header title at 0x0134–0x0143)
    RomVersion romVersion() const;

    // Raw ROM data pointer (for ROM header inspection)
    const uint8_t *romData() const { return romData_; }

    // Persist cart RAM to LittleFS (.sav file derived from ROM path).
    // Safe to call from Core 1 (emuTask). No-op if emulator is not running.
    void save() { if (running_) writeSaveFile(romPath_); }

    // ── Serial link (Phase 5: BattleShim) ────────────────────────────────────
    // Call before begin() or at any time to swap in/out the serial relay.
    // Pass nullptr to revert to the "no-connection" stub.
    void setSerialLink(ISerialLink *link) { serialLink_ = link; }

    // ── Peanut-GB static callbacks (public for C linkage) ───────────────────
    static uint8_t romRead(struct gb_s *gb, const uint_fast32_t addr);
    static uint8_t cartRamRead(struct gb_s *gb, const uint_fast32_t addr);
    static void    cartRamWrite(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val);
    // lcd_draw_line registered via gb_init_lcd()
    static void    lcdDrawLine(struct gb_s *gb, const uint8_t *pixels,
                               const uint_fast8_t line);
    // Serial TX: game sends a byte over the link cable → we buffer it
    static void    serialTx(struct gb_s *gb, const uint8_t tx);
    // Serial RX: game wants to receive a byte → Phase 1: no connection
    static enum gb_serial_rx_ret_e serialRx(struct gb_s *gb, uint8_t *rx);

private:
    TFT_eSPI  &tft_;
    InputMap  &input_;

    // gb_s is ~80 KB — allocated in PSRAM, accessed via pointer
    struct gb_s *gb_          = nullptr;
    uint8_t     *romData_     = nullptr;
    size_t       romSize_     = 0;
    // 32 KB cart save RAM (MBC battery-backed saves — Pokémon save file)
    uint8_t      cartRam_[0x8000];
    bool         running_     = false;
    ISerialLink *serialLink_  = nullptr;  // optional serial relay (BattleShim)
    char         romPath_[64] = {};       // stored at begin() for later save()

    // Viewport scroll position (screen pixels). 0..VIEWPORT_MAX, default=24.
    int16_t  viewportY_ = VIEWPORT_CENTER;

    // Per-scanline render buffer: 320 RGB565 pixels (2× stretched GB line)
    uint16_t lineBuf_[DISP_W];

    // Derive save file path from ROM path (/pokemon.gb → /pokemon.sav)
    static void romPathToSavePath(const char *romPath, char *out, size_t outLen);
    bool loadROM(const char *path);
    void loadSaveFile(const char *romPath);
    void writeSaveFile(const char *romPath);
};
