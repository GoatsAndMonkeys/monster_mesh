#include "EmulatorApp.h"
#include <LittleFS.h>
#include <SPI.h>

// ── begin() ──────────────────────────────────────────────────────────────────

bool EmulatorApp::begin(const char *romPath) {
    // Store path for later save() calls
    strncpy(romPath_, romPath, sizeof(romPath_) - 1);
    romPath_[sizeof(romPath_) - 1] = '\0';

    // 1. Load ROM from SD into PSRAM
    if (!loadROM(romPath)) return false;

    // 2. Allocate gb_s in PSRAM (~80 KB)
    gb_ = static_cast<struct gb_s *>(ps_malloc(sizeof(struct gb_s)));
    if (!gb_) {
        Serial.println("[EMU] PSRAM alloc failed for gb_s");
        return false;
    }
    memset(gb_, 0, sizeof(struct gb_s));

    // 3. Load cart save RAM (Pokémon save file)
    memset(cartRam_, 0, sizeof(cartRam_));
    loadSaveFile(romPath);

    // 4. gb_init — passes priv=this so callbacks can reach the object
    enum gb_init_error_e err = gb_init(
        gb_,
        EmulatorApp::romRead,
        EmulatorApp::cartRamRead,
        EmulatorApp::cartRamWrite,
        nullptr,    // error callback — set to null for now
        this);      // priv pointer

    if (err != GB_INIT_NO_ERROR) {
        Serial.printf("[EMU] gb_init error: %d\n", (int)err);
        free(gb_); gb_ = nullptr;
        return false;
    }

    // 5. Register LCD draw callback
    gb_init_lcd(gb_, EmulatorApp::lcdDrawLine);

    // 6. Register serial callbacks (Phase 1: no-connection stubs)
    gb_init_serial(gb_, EmulatorApp::serialTx, EmulatorApp::serialRx);

    // 7. Display: fill black, enable backlight
    tft_.fillScreen(TFT_BLACK);
    ledcSetup(LEDC_CH_BACKLIGHT, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, LEDC_CH_BACKLIGHT);
    ledcWrite(LEDC_CH_BACKLIGHT, 200);  // ~78% brightness

    running_ = true;
    Serial.printf("[EMU] started — ROM: %s (%u bytes)\n", romPath, (unsigned)romSize_);
    return true;
}

// ── runFrame() ───────────────────────────────────────────────────────────────

void EmulatorApp::runFrame() {
    if (!running_) return;

    // Push current button state to emulator.
    // Peanut-GB joypad is active-low: bit clear = pressed, bit set = released.
    // InputMap::state is active-high: bit set = pressed. Invert to convert.
    // Bit order matches: a, b, select, start, right, left, up, down (bits 0–7).
    gb_->direct.joypad = ~(input_.state);

    gb_run_frame(gb_);

    // Apply viewport scroll accumulated by trackball ISRs this frame
    if (input_.consumeReCenter()) {
        viewportY_ = VIEWPORT_CENTER;
    } else {
        int8_t delta = input_.consumeViewportDelta();
        if (delta != 0) {
            viewportY_ = (int16_t)constrain(
                (int)viewportY_ + (int)(delta * VIEWPORT_SCROLL_STEP),
                VIEWPORT_MIN, VIEWPORT_MAX);
        }
    }
}

// ── WRAM / HRAM access ────────────────────────────────────────────────────────

uint8_t EmulatorApp::readWRAM(uint16_t gbAddr) const {
    if (!gb_) return 0xFF;
    if (gbAddr >= 0xC000 && gbAddr <= 0xDFFF)
        return gb_->wram[gbAddr - 0xC000];
    if (gbAddr >= 0xFF00 && gbAddr <= 0xFFFE)
        return gb_->hram_io[gbAddr - 0xFF00];
    Serial.printf("[EMU] readWRAM bad addr 0x%04X\n", gbAddr);
    return 0xFF;
}

void EmulatorApp::writeWRAM(uint16_t gbAddr, uint8_t value) {
    if (!gb_) return;
    if (gbAddr >= 0xC000 && gbAddr <= 0xDFFF) {
        gb_->wram[gbAddr - 0xC000] = value;
        return;
    }
    if (gbAddr >= 0xFF00 && gbAddr <= 0xFFFE) {
        gb_->hram_io[gbAddr - 0xFF00] = value;
        return;
    }
    Serial.printf("[EMU] writeWRAM bad addr 0x%04X\n", gbAddr);
}

void EmulatorApp::readWRAMRange(uint16_t gbAddr, uint8_t *buf, size_t len) const {
    for (size_t i = 0; i < len; i++) buf[i] = readWRAM(gbAddr + i);
}

void EmulatorApp::writeWRAMRange(uint16_t gbAddr, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) writeWRAM(gbAddr + i, buf[i]);
}

// ── Peanut-GB callbacks ───────────────────────────────────────────────────────

uint8_t EmulatorApp::romRead(struct gb_s *gb, const uint_fast32_t addr) {
    EmulatorApp *self = static_cast<EmulatorApp *>(gb->direct.priv);
    if (addr >= self->romSize_) return 0xFF;
    return self->romData_[addr];
}

uint8_t EmulatorApp::cartRamRead(struct gb_s *gb, const uint_fast32_t addr) {
    EmulatorApp *self = static_cast<EmulatorApp *>(gb->direct.priv);
    if (addr >= sizeof(self->cartRam_)) return 0xFF;
    return self->cartRam_[addr];
}

void EmulatorApp::cartRamWrite(struct gb_s *gb, const uint_fast32_t addr,
                                const uint8_t val) {
    EmulatorApp *self = static_cast<EmulatorApp *>(gb->direct.priv);
    if (addr < sizeof(self->cartRam_))
        self->cartRam_[addr] = val;
}

// lcdDrawLine — called by Peanut-GB once per GB scanline (line 0–143).
// pixels[x] = palette index 0–3 for each of 160 pixels in the scanline.
//
// Viewport mapping:
//   screen_y = (line * GB_SCALE) − viewportY_
//   Two screen rows are pushed per GB line (2× vertical scale).
//   Rows outside [0, DISP_H) are skipped silently.
void EmulatorApp::lcdDrawLine(struct gb_s *gb, const uint8_t *pixels,
                               const uint_fast8_t line) {
    EmulatorApp *self = static_cast<EmulatorApp *>(gb->direct.priv);

    int16_t y0 = (int16_t)(line * GB_SCALE) - self->viewportY_;
    int16_t y1 = y0 + 1;

    if (y1 < 0 || y0 >= DISP_H) return;  // both rows off-screen

    // Stretch 160 px → 320 px (2× horizontal)
    uint16_t *buf = self->lineBuf_;
    for (int x = 0; x < GB_SCREEN_W; x++) {
        uint16_t c  = DMG_PALETTE[pixels[x] & 0x03];
        buf[x * 2]     = c;
        buf[x * 2 + 1] = c;
    }

    if (y0 >= 0 && y0 < DISP_H) self->tft_.pushImage(0, y0, DISP_W, 1, buf);
    if (y1 >= 0 && y1 < DISP_H) self->tft_.pushImage(0, y1, DISP_W, 1, buf);
}

// serialTx — game sends a byte over the link cable.
// If a serial link (BattleShim) is installed, forward to it.
// Otherwise silently drop.
void EmulatorApp::serialTx(struct gb_s *gb, const uint8_t tx) {
    EmulatorApp *self = static_cast<EmulatorApp *>(gb->direct.priv);
    if (self->serialLink_) {
        self->serialLink_->onSerialTx(tx);
    }
}

// serialRx — game wants to receive a byte from the link cable.
// If a serial link is installed and has a byte ready, deliver it.
// Otherwise return NO_CONNECTION (game busy-waits and retries each frame —
// this is the correct idle behaviour and imposes no latency penalty).
enum gb_serial_rx_ret_e EmulatorApp::serialRx(struct gb_s *gb, uint8_t *rx) {
    EmulatorApp *self = static_cast<EmulatorApp *>(gb->direct.priv);
    if (self->serialLink_) {
        uint8_t b;
        if (self->serialLink_->onSerialRx(b)) {
            *rx = b;
            return GB_SERIAL_RX_SUCCESS;
        }
    }
    *rx = 0xFF;  // idle line value
    return GB_SERIAL_RX_NO_CONNECTION;
}

// ── ROM loading ───────────────────────────────────────────────────────────────

bool EmulatorApp::loadROM(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[EMU] ROM not found: %s\n", path);
        return false;
    }
    romSize_ = f.size();
    if (romSize_ == 0 || romSize_ > 1024 * 1024) {
        Serial.printf("[EMU] ROM bad size: %u\n", (unsigned)romSize_);
        f.close();
        return false;
    }
    romData_ = static_cast<uint8_t *>(ps_malloc(romSize_));
    if (!romData_) {
        Serial.println("[EMU] PSRAM alloc failed for ROM");
        f.close();
        return false;
    }
    size_t n = f.read(romData_, romSize_);
    f.close();
    if (n != romSize_) {
        Serial.printf("[EMU] ROM read short: %u / %u\n", (unsigned)n, (unsigned)romSize_);
        return false;
    }
    Serial.printf("[EMU] ROM loaded: %u bytes\n", (unsigned)romSize_);
    return true;
}

void EmulatorApp::romPathToSavePath(const char *romPath, char *out, size_t outLen) {
    strncpy(out, romPath, outLen - 1);
    out[outLen - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) strcpy(dot, ".sav");
    else strncat(out, ".sav", outLen - strlen(out) - 1);
}

void EmulatorApp::loadSaveFile(const char *romPath) {
    char savPath[64];
    romPathToSavePath(romPath, savPath, sizeof(savPath));
    File f = LittleFS.open(savPath, "r");
    if (!f) return;
    size_t n = f.read(cartRam_, sizeof(cartRam_));
    f.close();
    Serial.printf("[EMU] save loaded: %s (%u bytes)\n", savPath, (unsigned)n);
}

void EmulatorApp::writeSaveFile(const char *romPath) {
    char savPath[64];
    romPathToSavePath(romPath, savPath, sizeof(savPath));
    File f = LittleFS.open(savPath, "w");
    if (!f) { Serial.println("[EMU] save write failed"); return; }
    f.write(cartRam_, sizeof(cartRam_));
    f.close();
    Serial.printf("[EMU] save written: %s\n", savPath);
}

// ── ROM version detection ───────────────────────────────────────────────────
// ROM header title at 0x0134–0x0143 (16 bytes). Compare against known titles.
RomVersion EmulatorApp::romVersion() const {
    if (!romData_ || romSize_ < 0x0144) return RomVersion::UNKNOWN;

    // Read 15 bytes of the ROM title (some ROMs only use 11)
    char title[16];
    memcpy(title, &romData_[0x0134], 15);
    title[15] = '\0';

    if (strncmp(title, "POKEMON RED",  11) == 0) return RomVersion::RED;
    if (strncmp(title, "POKEMON BLUE", 12) == 0) return RomVersion::BLUE;
    if (strncmp(title, "POKEMON YELL", 12) == 0) return RomVersion::YELLOW;
    if (strncmp(title, "POKEMON GOLD", 12) == 0) return RomVersion::GOLD;
    if (strncmp(title, "POKEMON SILV", 12) == 0) return RomVersion::SILVER;
    if (strncmp(title, "POKEMON CRYS", 12) == 0) return RomVersion::CRYSTAL;

    return RomVersion::UNKNOWN;
}
