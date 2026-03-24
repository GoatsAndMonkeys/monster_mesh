#pragma once
#include <Arduino.h>
#include <SD.h>
#include <LittleFS.h>
#include "ISerialLink.h"

// Forward declaration — full peanut_gb.h only included in MonsterMeshEmulator.cpp
struct gb_s;

// ── Display / viewport geometry ───────────────────────────────────────────────
#define GB_SCREEN_W          160
#define GB_SCREEN_H          144
#define PM_DISP_W            320
#define PM_DISP_H            240
#define GB_SCALE             2
#define VIEWPORT_MIN         0
#define VIEWPORT_MAX         48
#define VIEWPORT_CENTER      24
#define VIEWPORT_SCROLL_STEP 4

// DMG palette (RGB565)
static const uint16_t DMG_PALETTE[4] = {
    0xFFFF, 0xAD55, 0x52AA, 0x0000,
};

// ── MonsterMesh Emulator ────────────────────────────────────────────────────────
// Adapted from EmulatorApp. No longer owns TFT_eSPI directly — rendering goes
// through a framebuffer that MonsterMeshModule blits to screen when in emulator mode.
// Runs on a dedicated FreeRTOS task (Core 1).

class MonsterMeshEmulator {
public:
    MonsterMeshEmulator() {}

    bool begin(const char *romPath = "/pokemon.gb");
    void runFrame();

    uint8_t readWRAM(uint16_t gbAddr) const;
    void    writeWRAM(uint16_t gbAddr, uint8_t value);
    void    readWRAMRange(uint16_t gbAddr, uint8_t *buf, size_t len) const;

    bool isRunning() const { return running_; }
    void save() { if (running_) writeSaveFile(romPath_); }

    void setSerialLink(ISerialLink *link) { serialLink_ = link; }

    // ── Joypad input ────────────────────────────────────────────────────────
    void setJoypad(uint8_t activeHighBits) { joypadState_ = activeHighBits; }

    // ── Viewport ────────────────────────────────────────────────────────────
    int16_t viewportY() const { return viewportY_; }
    void scrollViewport(int8_t delta) {
        viewportY_ = constrain(viewportY_ + delta * VIEWPORT_SCROLL_STEP,
                               VIEWPORT_MIN, VIEWPORT_MAX);
    }
    void centerViewport() { viewportY_ = VIEWPORT_CENTER; }

    // ── Framebuffer access (for MonsterMeshModule to blit to display) ──────────
    // lineBuf_ is written by lcdDrawLine. The module reads it to push to TFT.
    // We expose a callback mechanism: set a draw callback and we'll call it
    // for each scanline during gb_run_frame().
    typedef void (*ScanlineCallback)(uint8_t line, const uint16_t *pixels320,
                                     int16_t screenY0, int16_t screenY1,
                                     void *ctx);
    void setScanlineCallback(ScanlineCallback cb, void *ctx) {
        scanlineCb_ = cb;
        scanlineCtx_ = ctx;
    }

    // Peanut-GB callbacks are file-scope functions in MonsterMeshEmulator.cpp.
    // They access the emulator through gb->direct.priv.
    // Fields below are public so the callbacks can access them.
public:
    struct gb_s *gb_          = nullptr;
    uint8_t     *romData_     = nullptr;
    size_t       romSize_     = 0;
    uint8_t      cartRam_[0x8000];
    bool         running_     = false;
    ISerialLink *serialLink_  = nullptr;
    char         romPath_[64] = {};
    int16_t      viewportY_   = VIEWPORT_CENTER;
    uint16_t     lineBuf_[PM_DISP_W];
    volatile uint8_t joypadState_ = 0;

    ScanlineCallback scanlineCb_  = nullptr;
    void            *scanlineCtx_ = nullptr;

    static void romPathToSavePath(const char *romPath, char *out, size_t outLen);
    bool loadROM(const char *path);
    void loadSaveFile(const char *romPath);
    void writeSaveFile(const char *romPath);
};
