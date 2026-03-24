#include "MonsterMeshEmulator.h"

// peanut_gb.h is a single-header library with all function bodies inline.
// It MUST only be included in this one translation unit.
#define ENABLE_LCD 1
#define ENABLE_SOUND 0
#include "peanut_gb.h"

// Forward declarations for Peanut-GB callbacks (defined below)
static uint8_t pm_romRead(struct gb_s *gb, const uint_fast32_t addr);
static uint8_t pm_cartRamRead(struct gb_s *gb, const uint_fast32_t addr);
static void    pm_cartRamWrite(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val);
static void    pm_lcdDrawLine(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line);
static void    pm_serialTx(struct gb_s *gb, const uint8_t tx);
static enum gb_serial_rx_ret_e pm_serialRx(struct gb_s *gb, uint8_t *rx);

// peanut_gb error callback — must not return (called on invalid opcode / bad read)
static void pm_gbError(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr)
{
    Serial.printf("[EMU] FATAL gb_error: err=%d addr=0x%04X\n", (int)err, addr);
    while (true) vTaskDelay(portMAX_DELAY);
}

// ── begin() ──────────────────────────────────────────────────────────────────

bool MonsterMeshEmulator::begin(const char *romPath) {
    strncpy(romPath_, romPath, sizeof(romPath_) - 1);
    romPath_[sizeof(romPath_) - 1] = '\0';

    if (!loadROM(romPath)) return false;

    gb_ = static_cast<struct gb_s *>(ps_malloc(sizeof(struct gb_s)));
    if (!gb_) {
        Serial.println("[EMU] PSRAM alloc failed for gb_s");
        return false;
    }
    memset(gb_, 0, sizeof(struct gb_s));

    memset(cartRam_, 0, sizeof(cartRam_));
    loadSaveFile(romPath);

    enum gb_init_error_e err = gb_init(
        gb_,
        pm_romRead,
        pm_cartRamRead,
        pm_cartRamWrite,
        pm_gbError,
        this);

    if (err != GB_INIT_NO_ERROR) {
        Serial.printf("[EMU] gb_init error: %d\n", (int)err);
        free(gb_); gb_ = nullptr;
        return false;
    }

    gb_init_lcd(gb_, pm_lcdDrawLine);
    gb_init_serial(gb_, pm_serialTx, pm_serialRx);

    running_ = true;
    Serial.printf("[EMU] started — ROM: %s (%u bytes)\n", romPath, (unsigned)romSize_);
    return true;
}

// ── runFrame() ───────────────────────────────────────────────────────────────

void MonsterMeshEmulator::runFrame() {
    if (!running_) return;
    gb_->direct.joypad = ~joypadState_;
    gb_run_frame(gb_);
}

// ── WRAM access ──────────────────────────────────────────────────────────────

uint8_t MonsterMeshEmulator::readWRAM(uint16_t gbAddr) const {
    if (!gb_) return 0xFF;
    if (gbAddr >= 0xC000 && gbAddr <= 0xDFFF)
        return gb_->wram[gbAddr - 0xC000];
    if (gbAddr >= 0xFF00 && gbAddr <= 0xFFFE)
        return gb_->hram_io[gbAddr - 0xFF00];
    return 0xFF;
}

void MonsterMeshEmulator::writeWRAM(uint16_t gbAddr, uint8_t value) {
    if (!gb_) return;
    if (gbAddr >= 0xC000 && gbAddr <= 0xDFFF) {
        gb_->wram[gbAddr - 0xC000] = value;
        return;
    }
    if (gbAddr >= 0xFF00 && gbAddr <= 0xFFFE) {
        gb_->hram_io[gbAddr - 0xFF00] = value;
        return;
    }
}

void MonsterMeshEmulator::readWRAMRange(uint16_t gbAddr, uint8_t *buf, size_t len) const {
    for (size_t i = 0; i < len; i++) buf[i] = readWRAM(gbAddr + i);
}

// ── Peanut-GB callbacks (file-scope, not class members) ─────────────────────
// These access MonsterMeshEmulator fields through gb->direct.priv.
// They are only referenced in begin() within this translation unit.

static uint8_t pm_romRead(struct gb_s *gb, const uint_fast32_t addr) {
    MonsterMeshEmulator *self = static_cast<MonsterMeshEmulator *>(gb->direct.priv);
    if (addr >= self->romSize_) return 0xFF;
    return self->romData_[addr];
}

static uint8_t pm_cartRamRead(struct gb_s *gb, const uint_fast32_t addr) {
    MonsterMeshEmulator *self = static_cast<MonsterMeshEmulator *>(gb->direct.priv);
    if (addr >= sizeof(self->cartRam_)) return 0xFF;
    return self->cartRam_[addr];
}

static void pm_cartRamWrite(struct gb_s *gb, const uint_fast32_t addr,
                             const uint8_t val) {
    MonsterMeshEmulator *self = static_cast<MonsterMeshEmulator *>(gb->direct.priv);
    if (addr < sizeof(self->cartRam_))
        self->cartRam_[addr] = val;
}

static void pm_lcdDrawLine(struct gb_s *gb, const uint8_t *pixels,
                            const uint_fast8_t line) {
    MonsterMeshEmulator *self = static_cast<MonsterMeshEmulator *>(gb->direct.priv);

    int16_t y0 = (int16_t)(line * GB_SCALE) - self->viewportY_;
    int16_t y1 = y0 + 1;

    if (y1 < 0 || y0 >= PM_DISP_H) return;

    uint16_t *buf = self->lineBuf_;
    for (int x = 0; x < GB_SCREEN_W; x++) {
        uint16_t c = DMG_PALETTE[pixels[x] & 0x03];
        buf[x * 2]     = c;
        buf[x * 2 + 1] = c;
    }

    if (self->scanlineCb_) {
        self->scanlineCb_(line, buf, y0, y1, self->scanlineCtx_);
    }
}

static void pm_serialTx(struct gb_s *gb, const uint8_t tx) {
    MonsterMeshEmulator *self = static_cast<MonsterMeshEmulator *>(gb->direct.priv);
    if (self->serialLink_) {
        self->serialLink_->onSerialTx(tx);
    }
}

static enum gb_serial_rx_ret_e pm_serialRx(struct gb_s *gb, uint8_t *rx) {
    MonsterMeshEmulator *self = static_cast<MonsterMeshEmulator *>(gb->direct.priv);
    if (self->serialLink_) {
        uint8_t b;
        if (self->serialLink_->onSerialRx(b)) {
            *rx = b;
            return GB_SERIAL_RX_SUCCESS;
        }
    }
    *rx = 0xFF;
    return GB_SERIAL_RX_NO_CONNECTION;
}

// ── ROM loading ──────────────────────────────────────────────────────────────

bool MonsterMeshEmulator::loadROM(const char *path) {
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

void MonsterMeshEmulator::romPathToSavePath(const char *romPath, char *out, size_t outLen) {
    strncpy(out, romPath, outLen - 1);
    out[outLen - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) strcpy(dot, ".sav");
    else strncat(out, ".sav", outLen - strlen(out) - 1);
}

void MonsterMeshEmulator::loadSaveFile(const char *romPath) {
    char savPath[64];
    romPathToSavePath(romPath, savPath, sizeof(savPath));
    File f = LittleFS.open(savPath, "r");
    if (!f) return;
    size_t n = f.read(cartRam_, sizeof(cartRam_));
    f.close();
    Serial.printf("[EMU] save loaded: %s (%u bytes)\n", savPath, (unsigned)n);
}

void MonsterMeshEmulator::writeSaveFile(const char *romPath) {
    char savPath[64];
    romPathToSavePath(romPath, savPath, sizeof(savPath));
    File f = LittleFS.open(savPath, "w");
    if (!f) { Serial.println("[EMU] save write failed"); return; }
    f.write(cartRam_, sizeof(cartRam_));
    f.close();
    Serial.printf("[EMU] save written: %s\n", savPath);
}
