#include "MonsterMeshEmulator.h"
#include "MonsterMeshAudio.h"
#include "SPILock.h"
#include "variant.h"
#include <SPI.h>
#include <SD.h>
#include <stdio.h>
#include <sys/stat.h>

// peanut_gb.h is a single-header library with all function bodies inline.
// It MUST only be included in this one translation unit.
// audio_read() and audio_write() are defined in MonsterMeshAudio.cpp
#define ENABLE_LCD 1
#define ENABLE_SOUND 1
#include "peanut_gb.h"

// Default DMG yellow-green palette (RGB565). MonsterMeshModule overwrites
// these on theme change via setEmulatorPalette().
uint16_t DMG_PALETTE[4] = { 0xFFFF, 0xAD55, 0x52AA, 0x0000 };

void setEmulatorPalette(uint16_t lightest, uint16_t light, uint16_t dark, uint16_t darkest) {
    // Index 0 = lightest (background), index 3 = darkest (text/sprite ink),
    // matching peanut_gb's pixel value 0..3 mapping.
    DMG_PALETTE[0] = lightest;
    DMG_PALETTE[1] = light;
    DMG_PALETTE[2] = dark;
    DMG_PALETTE[3] = darkest;
}

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
    // Clean up previous run if any
    running_ = false;
    if (audio_) { audio_->stop(); delete audio_; audio_ = nullptr; }
    if (gb_) { free(gb_); gb_ = nullptr; }

    strncpy(romPath_, romPath, sizeof(romPath_) - 1);
    romPath_[sizeof(romPath_) - 1] = '\0';

    if (!loadROM(romPath)) return false;

    gb_ = static_cast<struct gb_s *>(ps_malloc(sizeof(struct gb_s)));
    if (!gb_) {
        Serial.printf("[EMU] PSRAM alloc failed for gb_s (free=%u)\n", (unsigned)ESP.getFreePsram());
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
    // gb_->direct.frame_skip = true;  // disabled — didn't help audio

    // Initialize audio
    if (!audio_) {
        audio_ = new MonsterMeshAudio();
        if (!audio_->begin()) {
            Serial.println("[EMU] Audio init failed — continuing without sound");
            delete audio_;
            audio_ = nullptr;
        }
    }

    running_ = true;
    Serial.printf("[EMU] started — ROM: %s (%u bytes)\n", romPath, (unsigned)romSize_);
    return true;
}

// ── runFrame() ───────────────────────────────────────────────────────────────

void MonsterMeshEmulator::runFrame() {
    if (!running_) return;
    gb_->direct.joypad = ~joypadState_;
    gb_run_frame(gb_);

    // Generate and queue audio samples for this frame
    if (audio_) audio_->processFrame();
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

    // Stretch 144 GB lines to fill 240 screen rows (1.667x vertical)
    int16_t y0 = (int16_t)((uint16_t)line * PM_DISP_H / GB_SCREEN_H);
    int16_t y1 = (int16_t)(((uint16_t)line + 1) * PM_DISP_H / GB_SCREEN_H - 1);

    if (y1 < 0 || y0 >= PM_DISP_H) return;

    // 2x horizontal: 160 → 320 — DMG_PALETTE is now mutable so MonsterMeshModule
    // can re-skin it per active Themes::set().
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
    (void)gb; (void)tx;
}

static enum gb_serial_rx_ret_e pm_serialRx(struct gb_s *gb, uint8_t *rx) {
    (void)gb;
    *rx = 0xFF;
    return GB_SERIAL_RX_NO_CONNECTION;
}

// ── ROM loading ──────────────────────────────────────────────────────────────

bool MonsterMeshEmulator::loadROM(const char *path) {
    // Use Arduino SD library — POSIX fopen("/sd/...") is unreliable on some builds.
    // Path should be SD-relative like "/pokemon.gb" (what SD.open expects).
    // Also accept "/sd/..." prefix and strip it.
    const char *sdPath = path;
    if (strncmp(path, "/sd/", 4) == 0) sdPath = path + 3;  // "/sd/foo" → "/foo"
    if (strncmp(path, "/sd", 3) == 0 && path[3] == '\0') sdPath = "/";

    Serial.printf("[EMU] loadROM: path='%s' sdPath='%s'\n", path, sdPath);

    // Free previous ROM if reloading
    if (romData_) { free(romData_); romData_ = nullptr; romSize_ = 0; }

    // SD shares SPI bus with radio and TFT — must hold spiLock
    concurrency::LockGuard g(spiLock);

    // Full re-init of SD — end first, then begin fresh
    SD.end();
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    bool sdOk = SD.begin(SDCARD_CS, SPI);
    Serial.printf("[EMU] SD re-init: %d cardType=%d\n", (int)sdOk, (int)SD.cardType());
    if (!sdOk) {
        Serial.printf("[EMU] SD.begin() failed\n");
        return false;
    }

    // Try direct open first
    File f = SD.open(sdPath, FILE_READ);
    Serial.printf("[EMU] SD.open('%s') = %d\n", sdPath, (int)(bool)f);
    if (!f) {
        // Try with /sd prefix
        f = SD.open(path, FILE_READ);
        Serial.printf("[EMU] SD.open('%s') = %d\n", path, (int)(bool)f);
    }
    if (!f) {
        // Try directory iteration as last resort
        File dir = SD.open("/");
        Serial.printf("[EMU] SD.open('/') dir = %d isDir=%d\n", (int)(bool)dir, dir ? (int)dir.isDirectory() : -1);
        if (dir && dir.isDirectory()) {
            const char *targetName = strrchr(sdPath, '/');
            targetName = targetName ? targetName + 1 : sdPath;
            File entry = dir.openNextFile();
            while (entry) {
                const char *ename = entry.name();
                const char *slash = strrchr(ename, '/');
                const char *fname = slash ? slash + 1 : ename;
                Serial.printf("[EMU] dir entry: '%s'\n", fname);
                if (!entry.isDirectory() && strcasecmp(fname, targetName) == 0) {
                    f = entry;
                    Serial.printf("[EMU] Found match!\n");
                    break;
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
        }
    }
    if (!f) {
        Serial.printf("[EMU] All methods failed\n");
        return false;
    }
    romSize_ = f.size();
    Serial.printf("[EMU] ROM file opened: size=%u\n", (unsigned)romSize_);
    if (romSize_ == 0 || romSize_ > 1024 * 1024) {
        f.close();
        return false;
    }
    romData_ = static_cast<uint8_t *>(ps_malloc(romSize_));
    if (!romData_) {
        Serial.printf("[EMU] PSRAM alloc failed (%u bytes, free=%u)\n",
                      (unsigned)romSize_, (unsigned)ESP.getFreePsram());
        f.close();
        return false;
    }
    size_t n = f.read(romData_, romSize_);
    f.close();
    Serial.printf("[EMU] ROM read: %u / %u bytes\n", (unsigned)n, (unsigned)romSize_);
    if (n != romSize_) {
        free(romData_); romData_ = nullptr;
        return false;
    }
    Serial.printf("[EMU] ROM loaded OK: %u bytes\n", (unsigned)romSize_);
    return true;
}

void MonsterMeshEmulator::romPathToSavePath(const char *romPath, char *out, size_t outLen) {
    // Save sits next to the ROM on SD card — same path but .sav extension
    // romPath is SD-relative like "/pokemon.gb" or "/roms/pokemon.gb"
    // Strip /sd prefix if present
    const char *p = romPath;
    if (strncmp(p, "/sd/", 4) == 0) p = p + 3;  // "/sd/foo" → "/foo"
    else if (strncmp(p, "/sd", 3) == 0 && p[3] == '\0') p = "/";
    strncpy(out, p, outLen - 1);
    out[outLen - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) strcpy(dot, ".sav");
    else strncat(out, ".sav", outLen - strlen(out) - 1);
}

void MonsterMeshEmulator::loadSaveFile(const char *romPath) {
    char savPath[256];
    romPathToSavePath(romPath, savPath, sizeof(savPath));
    Serial.printf("[EMU] loading save: %s (ROM: %s)\n", savPath, romPath);

    // SD shares SPI bus with LoRa — hold spiLock around end/begin/open/read
    // to serialize against concurrent radio ops. writeSaveFile already does
    // this; without it here we've crashed loading SAV when LoRa was active
    // (logs end at "loading save:" line, no further output → hard panic).
    concurrency::LockGuard g(spiLock);
    SD.end();
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (!SD.begin(SDCARD_CS, SPI)) {
        Serial.println("[EMU] SD reinit failed for save load");
        return;
    }

    File f = SD.open(savPath, FILE_READ);
    if (!f) {
        Serial.printf("[EMU] no save file: %s\n", savPath);
        return;
    }
    size_t n = f.read(cartRam_, sizeof(cartRam_));
    f.close();
    Serial.printf("[EMU] save loaded: %s (%u bytes)\n", savPath, (unsigned)n);
}

void MonsterMeshEmulator::writeSaveFile(const char *romPath) {
    char savPath[256];
    romPathToSavePath(romPath, savPath, sizeof(savPath));
    Serial.printf("[EMU] writing save: %s (ROM: %s)\n", savPath, romPath);

    // SD shares SPI bus — reinit before access
    concurrency::LockGuard g(spiLock);
    SD.end();
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (!SD.begin(SDCARD_CS, SPI)) {
        Serial.printf("[EMU] SD reinit failed for save write\n");
        return;
    }

    File f = SD.open(savPath, FILE_WRITE);
    if (!f) { Serial.printf("[EMU] save write failed: %s\n", savPath); return; }
    size_t written = f.write(cartRam_, sizeof(cartRam_));
    f.close();
    Serial.printf("[EMU] save written: %s (%u bytes)\n", savPath, (unsigned)written);
}
