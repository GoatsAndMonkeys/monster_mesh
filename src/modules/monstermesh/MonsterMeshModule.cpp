#include "MonsterMeshModule.h"

#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH

#include "MeshService.h"
#include "Router.h"
#include "NodeDB.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "input/InputBroker.h"
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include "concurrency/LockGuard.h"
#include "SPILock.h"

#include "PowerFSM.h"
#include "MonsterMeshAudio.h"
#include "PokemonData.h"
#include "Gen1Species.h"
#include "RadioLibInterface.h"

// Provided by src/mesh/wifi/WiFiAPClient.cpp — used to park/unpark WiFi
// alongside LoRa when entering/exiting emulator or browser modes.
extern bool needReconnect;
extern void deinitWifi();
extern bool initWifi();

// LovyanGFX is available on T-Deck in both t-deck and t-deck-tft builds
#include <LovyanGFX.hpp>
#if HAS_TFT
#include <lvgl.h>
#include "display/lv_display_private.h"
#endif

MonsterMeshModule *monsterMeshModule = nullptr;

// Weak default — device-ui provides the real implementation when present
extern "C" __attribute__((weak)) void monstermesh_set_toggle_cb(void (*cb)(void)) { (void)cb; }

// Global LGFX pointer set by device-ui LGFXDriver::init_lgfx()
static lgfx::LGFX_Device *g_deviceUiLgfx = nullptr;
extern "C" void monstermesh_set_lgfx(void *ptr)
{
    g_deviceUiLgfx = static_cast<lgfx::LGFX_Device *>(ptr);
}

// Called from device-ui tools menu button via function pointer
static void mmToggle()
{
    if (monsterMeshModule) {
        monsterMeshModule->handleKeyFromLVGL(0x05); // toggle emulator on/off
    }
}

// Status getter for device-ui debug overlay
static const char *g_mmStatus = "module not created";
extern "C" const char *monstermesh_get_status(void)
{
    if (monsterMeshModule) {
        return monsterMeshModule->getSetupStatus();
    }
    return g_mmStatus;
}

// ── GB button bit positions ─────────────────────────────────────────────────
#define GB_BTN_A        (1 << 0)
#define GB_BTN_B        (1 << 1)
#define GB_BTN_SELECT   (1 << 2)
#define GB_BTN_START    (1 << 3)
#define GB_BTN_RIGHT    (1 << 4)
#define GB_BTN_LEFT     (1 << 5)
#define GB_BTN_UP       (1 << 6)
#define GB_BTN_DOWN     (1 << 7)

// ── Constructor ─────────────────────────────────────────────────────────────

MonsterMeshModule::MonsterMeshModule()
    : SinglePortModule("MonsterMesh", meshtastic_PortNum_PRIVATE_APP),
      concurrency::OSThread("MonsterMesh")
{
    // We want to see all PRIVATE_APP packets on any channel, not just "our" channel,
    // because wantPacket filters by channel anyway.
    isPromiscuous = false;
    loopbackOk = false;

    // Register toggle callback for device-ui tools menu button
    monstermesh_set_toggle_cb(mmToggle);
}

// ── setup() — called once after mesh is initialized ─────────────────────────

void MonsterMeshModule::setup()
{
    // Meshtastic does not reliably call setup() before runOnce() for all module
    // types, and the SPI bus / SD card may not yet be ready at this point.
    // All real initialization is deferred to the runOnce() lazy-init block below.
    LOG_INFO("[MonsterMesh] module registered\n");
    setupStatus_ = "waiting for boot...";
    // ensureMonsterMeshChannel();  // DISABLED — may crash if called before channels ready
    monsterMeshModule = this;
}

// ── handleInputEvent() — InputBroker callback ───────────────────────────────

int MonsterMeshModule::handleInputEvent(const InputEvent *event)
{
    if (!event) return 0;

    // Always process Ctrl+E regardless of emulator state
    if (event->kbchar == 0x05) {  // Ctrl+E
        handleKeyPress(0x05);
        return 0;
    }

    // Trackball press toggle is handled by GPIO 0 polling in monsterMeshKeyboardRead()

    if (!emulatorActive_ && !browserActive_) return 0;  // let Meshtastic handle keys

    // ANYKEY with kbchar = raw character
    if (event->inputEvent == INPUT_BROKER_ANYKEY && event->kbchar != 0) {
        handleKeyPress(event->kbchar);
        lastKeyMs_ = millis();
        return 0;
    }

    // Trackball events → viewport scroll
    if (event->inputEvent == INPUT_BROKER_UP) {
        viewportDelta_--;
        return 0;
    }
    if (event->inputEvent == INPUT_BROKER_DOWN) {
        viewportDelta_++;
        return 0;
    }

    return 0;
}

// ── ensureMonsterMeshChannel() ─────────────────────────────────────────────────

void MonsterMeshModule::ensureMonsterMeshChannel()
{
    // Check if channel 1 already has a name set
    auto &ch = channels.getByIndex(MONSTERMESH_CHANNEL);
    if (ch.role == meshtastic_Channel_Role_DISABLED ||
        strlen(ch.settings.name) == 0) {
        // Set up channel 1 as "MonsterMesh Center"
        ch.role = meshtastic_Channel_Role_SECONDARY;
        snprintf(ch.settings.name, sizeof(ch.settings.name), "MonsterMesh");
        // Use default PSK (no encryption for easy joining)
        ch.settings.psk.size = 1;
        ch.settings.psk.bytes[0] = 0; // no encryption
        channels.setChannel(ch);
        Serial.println("[MonsterMesh] channel 1 configured as 'MonsterMesh Center'");
    }
}

// ── wantPacket() — filter incoming packets ──────────────────────────────────

bool MonsterMeshModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // Accept PRIVATE_APP packets on our channel
    if (p->decoded.portnum == meshtastic_PortNum_PRIVATE_APP &&
        p->channel == MONSTERMESH_CHANNEL) {
        return true;
    }
    return false;
}

// ── handleReceived() — incoming mesh packet ─────────────────────────────────

ProcessMessage MonsterMeshModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    (void)mp;
    return ProcessMessage::STOP;
}

// ── runOnce() — OSThread periodic drain of tx queue ─────────────────────────

int32_t MonsterMeshModule::runOnce()
{
    // Register keyboard observer as early as possible (after 1s) so SYM+E works
    // even before the emulator finishes loading.
    if (!kbObserverRegistered_ && millis() > 1000 && inputBroker) {
        kbObserverRegistered_ = true;
        installKeyboardHook();
        inputObserver_.observe(inputBroker);
    }

    // Lazy init — setup() is never called by Meshtastic, so we do it here.
    // setupDone_ is only set true after SD mounts successfully (or retries exhausted),
    // so transient SD failures can be retried on subsequent runOnce() calls.
    if (!setupDone_) {
        if (millis() < 8000) {
            return 500;
        }

        // ── One-time subsystem init (guarded so retries don't re-init) ────────
        if (setupRetries_ == 0) {
            transport_.begin();
            transport_.setNodeId(nodeDB->getNodeNum());
        }


        // Mount SD via Arduino SD library — registers VFS at /sd
        // Must use same SPI bus config as FSCommon.cpp (shared bus with TFT)
        // Retries handle SPI bus contention with TFT driver
        snprintf(setupStatusBuf_, sizeof(setupStatusBuf_), "SD attempt %d/%d...", setupRetries_ + 1, MAX_SETUP_RETRIES);
        setupStatus_ = setupStatusBuf_;
        bool sdOk = false;
        {
            concurrency::LockGuard g(spiLock);
            SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
            sdOk = SD.begin(SDCARD_CS, SPI, 4000000U);
        }
        if (!sdOk) {
            setupRetries_++;
            if (setupRetries_ >= MAX_SETUP_RETRIES) {
                setupStatus_ = "SD mount FAILED";
                LOG_WARN("[MonsterMesh] SD.begin() failed after %d retries\n", MAX_SETUP_RETRIES);
                setupDone_ = true;
            } else {
                snprintf(setupStatusBuf_, sizeof(setupStatusBuf_), "SD retry %d/%d...", setupRetries_ + 1, MAX_SETUP_RETRIES);
                setupStatus_ = setupStatusBuf_;
                LOG_WARN("[MonsterMesh] SD.begin() failed, retry %d\n", setupRetries_);
            }
            return 2000; // retry in 2 seconds
        }
        setupStatus_ = "SD OK";

        // Register scanline callback (used once a ROM is loaded)
        emu_.setScanlineCallback(scanlineCallback, this);

        setupDone_ = true;

        // Keyboard hook installed early (at 1s) via kbObserverRegistered_ path above.
        // Re-install the LVGL hook here in case LVGL wasn't ready at 1s.
        if (!kbObserverRegistered_) {
            kbObserverRegistered_ = true;
            installKeyboardHook();
            if (inputBroker) inputObserver_.observe(inputBroker);
        } else {
            installKeyboardHook(); // re-run hook install in case LVGL indev wasn't ready yet
        }

        // Stay in Meshtastic UI at boot. User presses Ctrl+E to open the
        // ROM browser on demand — that path parks radios cleanly. Auto-open
        // at boot raced with LoRa/WiFi init and froze the device.
        setupStatus_ = "SD ready";
        LOG_INFO("[MonsterMesh] SD ready — press ALT to open ROM browser\n");
    }
    // Trackball press toggle is handled in handleInputEvent() via INPUT_BROKER_SELECT

    // Keep keyboard hook installed at all times. installKeyboardHook is now
    // idempotent and re-walks the indev list every call, so device-ui
    // recreating the keypad indev won't strand us without ALT detection.
    installKeyboardHook();

    // ── Direct ALT poll (Meshtastic UI only) ────────────────────────────────
    // The LVGL hook's peek-at-RAW was unreliable — LVGL's keypad indev poll
    // rate depends on UI state and theme. Owning the I2C bus here every
    // ~120ms guarantees ALT presses are caught while in the Meshtastic UI.
    // When emulator/browser is active, the LVGL hook owns I2C (RAW mode for
    // emu, KEY mode for browser) and we don't poll.
    if (setupDone_ && !emulatorActive_ && !browserActive_) {
        static uint32_t lastAltPoll = 0;
        static bool     altWas      = false;
        static uint32_t lastAltFire = 0;
        static uint32_t pollCount   = 0;
        static uint32_t lastDumpMs  = 0;
        uint32_t now = millis();
        if (now - lastAltPoll >= 120) {
            lastAltPoll = now;
            pollCount++;
            // Switch to RAW mode, read byte[0] (contains ALT at bit 0x10), revert.
            Wire.beginTransmission(0x55);
            Wire.write(0x03);
            uint8_t st1 = Wire.endTransmission();
            uint8_t got = Wire.requestFrom((uint8_t)0x55, (uint8_t)5);
            uint8_t b[5] = {};
            for (int i = 0; i < 5 && Wire.available(); i++) b[i] = Wire.read();
            Wire.beginTransmission(0x55);
            Wire.write(0x04);
            uint8_t st2 = Wire.endTransmission();

            // Dump raw state every 2s so we can confirm bus is live
            if (now - lastDumpMs > 2000) {
                lastDumpMs = now;
                LOG_INFO("[MonsterMesh] kb poll #%u st=%u/%u got=%u b=%02X %02X %02X %02X %02X\n",
                         (unsigned)pollCount, st1, st2, got, b[0], b[1], b[2], b[3], b[4]);
            }

            bool altNow = (b[0] & 0x10) != 0;
            if (altNow && !altWas && (now - lastAltFire > 600)) {
                lastAltFire = now;
                LOG_INFO("[MonsterMesh] ALT pressed (runOnce poll) → toggle\n");
                handleKeyPress(0x05);
            }
            altWas = altNow;
        }
    }

    // Keep PowerFSM awake while emulator or browser is active. Throttle —
    // every runOnce was logging "State: ON" and burying everything else.
    static uint32_t lastWakeMs = 0;
    if ((emulatorActive_ || browserActive_) && (millis() - lastWakeMs > 5000)) {
        lastWakeMs = millis();
        powerFSM.trigger(EVENT_INPUT);
    }
    // One-shot probe so we can see boot-time state on serial
    static bool probeLogged = false;
    if (!probeLogged && setupDone_) {
        probeLogged = true;
        LOG_INFO("[MonsterMesh] boot complete — emu=%d browser=%d\n",
                 (int)emulatorActive_, (int)browserActive_);
    }

    // Re-suppress LVGL flush if emulator or browser is active — screen sleep/wake
    // may restore the real flush callback behind our back
#if HAS_TFT
    if ((emulatorActive_ || browserActive_) && savedFlushCb_) {
        lv_display_t *disp = lv_display_get_default();
        if (disp && disp->flush_cb != nullptr) {
            lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
                lv_display_flush_ready(d);
            });
        }
        // NOTE: Do NOT call clearClipRect() or any LGFX method here — runOnce runs
        // on Core 0 and would race with scanline callback on Core 1, causing palette corruption
    }
#endif

    // blitFrame() moved to emulator task on Core 1 — runOnce() is too slow for screen updates

    // Process buffered browser keys and render
    if (browserActive_) {
        static uint32_t lastBrowserTick = 0;
        uint32_t now2 = millis();
        if (now2 - lastBrowserTick > 2000) {
            lastBrowserTick = now2;
            LOG_INFO("[MonsterMesh] browser tick: dirty=%d count=%d gfx=%p\n",
                     (int)browser_.isDirty(), browser_.count(), (void *)g_deviceUiLgfx);
        }
        uint8_t key = pendingBrowserKey_;
        if (key != 0) {
            pendingBrowserKey_ = 0;
            LOG_DEBUG("[MonsterMesh] browser key=0x%02X cursor=%d count=%d\n",
                      key, browser_.cursor(), browser_.count());
            bool selected;
            {
                concurrency::LockGuard g(spiLock);
                selected = browser_.handleKey(key);
            }
            LOG_DEBUG("[MonsterMesh] handleKey returned selected=%d\n", (int)selected);
            if (selected) {
                LOG_DEBUG("[MonsterMesh] selectedPath='%s'\n", browser_.selectedPath());
                launchROM(browser_.selectedPath());
            }
        }
        renderBrowser();
    }

    // Deferred save — triggered on emulator exit, done here outside LVGL callback
    if (pendingSave_) {
        pendingSave_ = false;
        emu_.save();
    }

    drainTxQueue();
    return 50;
}

void MonsterMeshModule::drainTxQueue()
{
    uint8_t buf[237];
    size_t len = 0;

    while (transport_.hasPendingSend()) {
        if (!transport_.dequeueSend(buf, len, sizeof(buf))) break;

        meshtastic_MeshPacket *p = router->allocForSending();
        p->to = NODENUM_BROADCAST;
        p->channel = MONSTERMESH_CHANNEL;
        p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
        p->decoded.payload.size = len;
        memcpy(p->decoded.payload.bytes, buf, len);

        service->sendToMesh(p);
    }
}

// ── Keyboard ─────────────────────────────────────────────────────────────────
//
// Approach 1: swap the LVGL indev read callback.
//
// The MUI (device-ui) registers a TDeckKeyboardInputDriver as an LVGL keypad
// indev during DeviceGUI::init(). LVGL calls its read_cb each frame, consuming
// the raw I2C byte and normalising it into LV_KEY_* codes.  By replacing that
// callback with our own we can intercept before normalisation happens:
//   - Emulator active → consume I2C byte, route to handleKeyPress(), tell LVGL nothing.
//   - Meshtastic UI   → call original callback so the MUI keeps working.

#if HAS_TFT
static lv_indev_t *g_kbIndev      = nullptr;
static bool        g_symActive    = false;  // KEY mode: SYM modifier latch
static bool        g_rawMode      = false;  // true when MCU is in RAW (5-byte) mode
static bool        g_symEConsumed = false;  // RAW mode: debounce SYM+E toggle

// Switch keyboard MCU between KEY mode (0x04, 1 byte) and RAW mode (0x03, 5 bytes).
// T-Deck keyboard MCU (ESP32-C3 at 0x55) supports this via PR #87.
static void kbSetMode(bool raw)
{
    Wire.beginTransmission(0x55);
    Wire.write(raw ? 0x03 : 0x04);
    Wire.endTransmission();
    g_rawMode = raw;
    LOG_INFO("[MonsterMesh] kb → %s mode\n", raw ? "RAW" : "KEY");
}

// T-Deck keyboard matrix column/row → RAW byte bit positions:
//   byte[0]: q=0x01  w=0x02  SYM=0x04  a=0x08  ALT=0x10  SPACE=0x20  Mic=0x40
//   byte[1]: e=0x01  s=0x02  d=0x04    p=0x08  x=0x10    z=0x20      LShift=0x40
//   byte[2]: r=0x01  g=0x02  t=0x04    RShift=0x08 v=0x10 c=0x20     f=0x40
//   byte[3]: u=0x01  h=0x02  y=0x04    Enter=0x08  b=0x10 n=0x20     j=0x40
//   byte[4]: o=0x01  l=0x02  i=0x04    BSP=0x08    $=0x10 m=0x20     k=0x40
static uint8_t rawBytesToJoypad(const uint8_t b[5])
{
    uint8_t joy = 0;
    if (b[0] & 0x02) joy |= GB_BTN_UP;     // W → Up
    if (b[0] & 0x08) joy |= GB_BTN_LEFT;   // A → Left
    if (b[1] & 0x02) joy |= GB_BTN_DOWN;   // S → Down
    if (b[1] & 0x04) joy |= GB_BTN_RIGHT;  // D → Right
    if (b[4] & 0x40) joy |= GB_BTN_A;      // K → A button
    if (b[4] & 0x02) joy |= GB_BTN_B;      // L → B button
    if (b[3] & 0x08) joy |= GB_BTN_START;  // Enter → Start
    if (b[0] & 0x20) joy |= GB_BTN_SELECT; // Space → Select
    return joy;
}

// We are the sole I2C reader for the T-Deck keyboard.
// RAW mode (emulator active): read 5 bytes, map to GB joypad directly.
// KEY mode (Meshtastic UI): read 1 byte, map to LVGL keys.
// SYM+E always toggles between modes regardless of current state.
static bool     g_micWasPressed  = false;  // mic button edge detection
static uint32_t g_micLastToggleMs = 0;
static uint8_t  g_micPollCounter = 0;      // only peek every N cycles

static void monsterMeshKeyboardRead(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;

    // Keep device-ui backlight alive while emulator or browser is active.
    bool keepAlive = (monsterMeshModule &&
                      (monsterMeshModule->isEmulatorActive() || monsterMeshModule->isBrowserActive()));
    if (keepAlive) {
        lv_display_trigger_activity(NULL);
    }

    // ── ALT + Mic button peek ──────────────────────────────────────────
    // ALT (byte[0] bit 0x10) = toggle screens, Mic (byte[0] bit 0x40) = toggle sound.
    // Peek at RAW mode every few cycles to check them, then switch back to KEY mode.
    // Skip when browser is active — the mode switching eats buffered keypresses.
    bool browserUp = monsterMeshModule && monsterMeshModule->isBrowserActive();
    if (!g_rawMode && monsterMeshModule && !browserUp && (++g_micPollCounter >= 3)) {
        g_micPollCounter = 0;
        // Quick switch to RAW, read button bits, switch back to KEY
        Wire.beginTransmission(0x55);
        Wire.write(0x03);  // RAW mode
        Wire.endTransmission();

        Wire.requestFrom((uint8_t)0x55, (uint8_t)5);
        uint8_t rb[5] = {};
        for (int i = 0; i < 5 && Wire.available(); i++) rb[i] = Wire.read();

        Wire.beginTransmission(0x55);
        Wire.write(0x04);  // back to KEY mode
        Wire.endTransmission();

        // ALT button → toggle screens
        bool altPressed = (rb[0] & 0x10) != 0;
        static bool g_altWasPressed = false;
        if (altPressed && !g_altWasPressed) {
            uint32_t now = millis();
            if (now - g_micLastToggleMs > 600) {
                g_micLastToggleMs = now;
                g_altWasPressed = true;
                LOG_INFO("[MonsterMesh] ALT pressed (KEY-mode peek) → toggle\n");
                monsterMeshModule->handleKeyFromLVGL(0x05);
                return;
            }
        }
        g_altWasPressed = altPressed;

        // Mic button → toggle sound
        bool micPressed = (rb[0] & 0x40) != 0;
        if (micPressed && !g_micWasPressed) {
            uint32_t now = millis();
            if (now - g_micLastToggleMs > 600) {
                g_micLastToggleMs = now;
                g_micWasPressed = true;
                monsterMeshModule->toggleSound();
                return;
            }
        }
        g_micWasPressed = micPressed;
    }

    if (g_rawMode) {
        // ── RAW mode: read 5-byte bitmask ────────────────────────────────
        Wire.requestFrom((uint8_t)0x55, (uint8_t)5);
        uint8_t b[5] = {};
        for (int i = 0; i < 5 && Wire.available(); i++) b[i] = Wire.read();

        // ALT button in RAW mode — toggle screens (exit emulator)
        bool altHeld = (b[0] & 0x10) != 0;
        static bool g_altWasHeldRaw = false;
        if (altHeld && !g_altWasHeldRaw) {
            uint32_t now = millis();
            if (now - g_micLastToggleMs > 600) {
                g_micLastToggleMs = now;
                if (monsterMeshModule) {
                    monsterMeshModule->setJoypadDirect(0);
                    monsterMeshModule->handleKeyFromLVGL(0x05);
                    kbSetMode(false);
                }
            }
        }
        g_altWasHeldRaw = altHeld;

        // Mic button in RAW mode — toggle sound
        bool micHeld = (b[0] & 0x40) != 0;
        if (micHeld && !g_micWasPressed) {
            uint32_t now = millis();
            if (now - g_micLastToggleMs > 600) {
                g_micLastToggleMs = now;
                if (monsterMeshModule) {
                    monsterMeshModule->toggleSound();
                }
            }
        }
        g_micWasPressed = micHeld;

        // O key (+) → volume up, I key (-) → volume down
        // byte[4]: o=0x01, i=0x04
        static bool g_volUpWas = false, g_volDnWas = false;
        bool volUp = (b[4] & 0x01) != 0;
        bool volDn = (b[4] & 0x04) != 0;
        if (volUp && !g_volUpWas && monsterMeshModule) monsterMeshModule->adjustVolume(1);
        if (volDn && !g_volDnWas && monsterMeshModule) monsterMeshModule->adjustVolume(-1);
        g_volUpWas = volUp;
        g_volDnWas = volDn;

        // Y key ()) → brightness up, T key (() → brightness down
        // byte[3]: y=0x04, byte[2]: t=0x04
        static bool g_brtUpWas = false, g_brtDnWas = false;
        bool brtUp = (b[3] & 0x04) != 0;
        bool brtDn = (b[2] & 0x04) != 0;
        if (brtUp && !g_brtUpWas && monsterMeshModule) monsterMeshModule->adjustBrightness(1);
        if (brtDn && !g_brtDnWas && monsterMeshModule) monsterMeshModule->adjustBrightness(-1);
        g_brtUpWas = brtUp;
        g_brtDnWas = brtDn;

        // SYM+E simultaneously → exit emulator, switch back to KEY mode
        bool symHeld = (b[0] & 0x04) != 0;
        bool eHeld   = (b[1] & 0x01) != 0;
        if (symHeld && eHeld) {
            if (!g_symEConsumed) {
                g_symEConsumed = true;
                if (monsterMeshModule) {
                    monsterMeshModule->setJoypadDirect(0);
                    monsterMeshModule->handleKeyFromLVGL(0x05); // toggles emulatorActive_
                    kbSetMode(false);
                }
            }
            return;
        }
        g_symEConsumed = false;

        // Map current key state directly to joypad (RAW = live state, no press/release)
        if (monsterMeshModule) {
            monsterMeshModule->setJoypadDirect(rawBytesToJoypad(b));
        }
        // LVGL gets nothing while emulator is active
        return;
    }

    // ── KEY mode: read 1-byte ASCII ──────────────────────────────────────
    Wire.requestFrom((uint8_t)0x55, (uint8_t)1);
    uint8_t key = Wire.available() ? Wire.read() : 0;

    if (key == 0) return;

    LOG_DEBUG("[MonsterMesh] key=0x%02X emu=%d\n", key, monsterMeshModule ? (int)monsterMeshModule->isEmulatorActive() : -1);

    // ── ALT+E (0x05 = Ctrl+E) toggles emulator on/off ───────────────────
    // On T-Deck, ALT+letter produces control codes (ALT+E = 0x05).
    // Also handle SYM(0x0C) + E as backup toggle.
    if (key == 0x05) {
        if (monsterMeshModule) {
            monsterMeshModule->handleKeyFromLVGL(0x05);
        }
        return;
    }

    // SYM modifier latch (0x0c = SYM or ALT+C)
    if (key == 0x0c) {
        g_symActive = true;
        return;
    }
    if (g_symActive && (key == 'e' || key == 'E')) {
        g_symActive = false;
        if (monsterMeshModule) {
            monsterMeshModule->handleKeyFromLVGL(0x05);
        }
        return;
    }
    g_symActive = false;

    // ── When emulator or browser is active, route keys to game instead of LVGL
    if (monsterMeshModule && (monsterMeshModule->isEmulatorActive() ||
                              monsterMeshModule->isBrowserActive())) {
        monsterMeshModule->handleKeyFromLVGL(key);
        return; // consume — don't pass to LVGL
    }

    // Normal MUI LVGL key mapping (replicates TDeckKeyboardInputDriver)
    switch (key) {
        case 0x0D: case '\n': data->key = LV_KEY_ENTER;    break;
        case 0x08:            data->key = LV_KEY_BACKSPACE; break;
        case 0x09:            data->key = LV_KEY_NEXT;      break;
        case 0x1B:            data->key = LV_KEY_ESC;       break;
        default:              data->key = key;               break;
    }
    data->state = LV_INDEV_STATE_PRESSED;
}
#endif // HAS_TFT

void MonsterMeshModule::installKeyboardHook()
{
#if HAS_TFT
    // Walk the indev list every time. If device-ui ever recreates the keypad
    // indev, our cached pointer goes stale — re-walk catches that.
    lv_indev_t *indev = nullptr;
    while ((indev = lv_indev_get_next(indev)) != nullptr) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) continue;
        if (indev == g_kbIndev && lv_indev_get_read_cb(indev) == monsterMeshKeyboardRead) {
            return;  // already hooked on this indev
        }
        // First install (or re-install on a new indev) — set MCU to KEY mode.
        Wire.beginTransmission(0x55);
        Wire.write(0x04);
        Wire.endTransmission();
        g_rawMode    = false;
        g_symActive  = false;
        g_symEConsumed = false;
        g_kbIndev = indev;
        lv_indev_set_read_cb(indev, monsterMeshKeyboardRead);
        LOG_INFO("[MonsterMesh] LVGL kb hook installed (indev=%p)\n", indev);
        return;
    }
#endif
}

void MonsterMeshModule::handleKeyFromLVGL(uint8_t c) { handleKeyPress(c); lastKeyMs_ = millis(); }

void MonsterMeshModule::toggleSound()
{
    if (emu_.audio_) {
        emu_.audio_->setMuted(!emu_.audio_->isMuted());
        LOG_INFO("[MonsterMesh] Sound %s\n", emu_.audio_->isMuted() ? "OFF" : "ON");
    }
}

void MonsterMeshModule::adjustVolume(int8_t delta)
{
    if (!emu_.audio_) return;
    int8_t vol = (int8_t)emu_.audio_->volume() + delta;
    if (vol < 0) vol = 0;
    if (vol > 8) vol = 8;
    emu_.audio_->setVolume((uint8_t)vol);
    LOG_INFO("[MonsterMesh] Volume: %d/8\n", vol);
}

void MonsterMeshModule::adjustBrightness(int8_t delta)
{
    lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
    if (!gfx) return;
    int16_t b = brightness_ + delta * 32;
    if (b < 16) b = 16;
    if (b > 255) b = 255;
    brightness_ = (uint8_t)b;
    gfx->setBrightness(brightness_);
    LOG_INFO("[MonsterMesh] Brightness: %d/255\n", brightness_);
}

void MonsterMeshModule::pollKeyboard() {
    // Unused: keyboard flows through LVGL hook (HAS_TFT) or InputBroker (non-TFT).
}

// ── Emulator task (Core 1) ──────────────────────────────────────────────────

void MonsterMeshModule::emuTaskEntry(void *pv)
{
    static_cast<MonsterMeshModule *>(pv)->emuTaskLoop();
}

void MonsterMeshModule::emuTaskLoop()
{
    const TickType_t framePeriod = pdMS_TO_TICKS(16);  // ~60fps
    TickType_t lastWake = xTaskGetTickCount();
    uint8_t frameCount = 0;

    while (true) {
        // Write to framebuffer every 3rd frame — render task blits to TFT separately
        frameCount++;
        renderFrame_ = (emulatorActive_ && frameCount >= 3);
        emu_.runFrame();
        if (renderFrame_) {
            frameCount = 0;
            // frameDirty_ is set by scanlineCallback — render task picks it up
        }
        renderFrame_ = false;

        // ── Auto-save on battle end ───────────────────────────────────────
        uint8_t curBattle = emu_.readWRAM(Gen1::wIsInBattle);
        if (prevBattle_ != 0 && curBattle == 0) {
            emu_.save();
        }
        prevBattle_ = curBattle;

        // ── Viewport scroll ──────────────────────────────────────────────
        if (viewportRecenter_) {
            emu_.centerViewport();
            viewportRecenter_ = false;
        }
        int8_t vd = viewportDelta_;
        if (vd != 0) {
            emu_.scrollViewport(vd);
            viewportDelta_ = 0;
        }

        // ── Auto-release keys after KEY_RELEASE_MS ──────────────────────
        // T-Deck keyboard sends press only, no release events.
        // Release all keys after a short hold time.
        if (kbMask_ && lastKeyMs_ && (millis() - lastKeyMs_ > KEY_RELEASE_MS)) {
            joypadState_ &= ~kbMask_;
            kbMask_ = 0;
        }

        // ── Push joypad state to emulator ────────────────────────────────
        emu_.setJoypad(joypadState_);

        // No periodic auto-save — SD operations on large cards cause input lag.
        // Saves happen on: emulator exit (toggle off) and battle end.

        vTaskDelayUntil(&lastWake, framePeriod);
    }
}

// ── Render task (separate from emulator, so SPI blit doesn't stall audio) ────

void MonsterMeshModule::renderTaskEntry(void *pv)
{
    static_cast<MonsterMeshModule *>(pv)->renderTaskLoop();
}

void MonsterMeshModule::renderTaskLoop()
{
    while (true) {
        if (emulatorActive_ && frameDirty_) {
            blitFrame();
        }
        // ~30fps render rate — emulator runs at 60fps independently
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

// ── Scanline callback — writes to PSRAM framebuffer (no SPI, no lock) ───────

void MonsterMeshModule::scanlineCallback(uint8_t line, const uint16_t *pixels320,
                                       int16_t screenY0, int16_t screenY1, void *ctx)
{
    MonsterMeshModule *self = static_cast<MonsterMeshModule *>(ctx);
    if (!self->renderFrame_ || !self->frameBuf_) return;

    // Write scanline to PSRAM framebuffer with byte swap
    for (int16_t y = screenY0; y <= screenY1; y++) {
        if (y >= 0 && y < PM_DISP_H) {
            uint16_t *row = &self->frameBuf_[y * PM_DISP_W];
            for (int i = 0; i < PM_DISP_W; i++)
                row[i] = __builtin_bswap16(pixels320[i]);
        }
    }
    self->frameDirty_ = true;
}

// ── Blit entire framebuffer to TFT in one SPI transaction ───────────────────

void MonsterMeshModule::blitFrame()
{
    if (!frameDirty_ || !frameBuf_) return;
    frameDirty_ = false;

    lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
    if (!gfx) return;

    // Push in 4 chunks of 60 lines, releasing spiLock between chunks
    // so the radio task can access SPI (prevents watchdog timeout)
    for (int chunk = 0; chunk < 4; chunk++) {
        int yStart = chunk * 60;
        int yEnd = yStart + 60;
        if (yEnd > PM_DISP_H) yEnd = PM_DISP_H;
        {
            concurrency::LockGuard g(spiLock);
            gfx->startWrite();
            for (int y = yStart; y < yEnd; y++) {
                gfx->pushImage(0, y, PM_DISP_W, 1, &frameBuf_[y * PM_DISP_W]);
            }
            gfx->endWrite();
        }
        if (chunk < 3) vTaskDelay(1);  // yield between chunks
    }
}

// ── drawFrame() — Meshtastic OLED/TFT UI frame ─────────────────────────────

#if HAS_SCREEN
void MonsterMeshModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                                int16_t x, int16_t y)
{
    // When in emulator mode, the actual rendering is done by the scanline
    // callback in the emulator task. This drawFrame just shows status text
    // if the emulator isn't running, or overlays.
    if (!emulatorActive_) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(ArialMT_Plain_16);
        display->drawString(x + 64, y + 20, "MonsterMesh");
        display->setFont(ArialMT_Plain_10);
        display->drawString(x + 64, y + 40, setupStatus_);
        return;
    }
}
#endif

// ── handleKeyPress() — process keyboard input ───────────────────────────────

void MonsterMeshModule::handleKeyPress(uint8_t ascii)
{
    // Keep screen awake on any keypress while emulator or browser is active
    if (emulatorActive_ || browserActive_) {
        powerFSM.trigger(EVENT_INPUT);
    }

    // ── Ctrl+E / mic button: toggle display modes ──────────────────────
    if (ascii == 0x05) {  // Ctrl+E
        if (browserActive_) {
            // ── Exit browser → Meshtastic UI ──────────────────────────────
            browserActive_ = false;
            exitEmulatorMode();  // emulatorActive_ already false
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            if (disp && savedFlushCb_) {
                lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                savedFlushCb_ = nullptr;
                lv_obj_invalidate(lv_screen_active());
            }
#endif
            return;
        }

        if (!emuInitialized_ && !setupDone_) {
            // Setup hasn't finished (SD not mounted yet)
            LOG_WARN("[MonsterMesh] not ready — status: %s\n", setupStatus_);
            return;
        }

        if (!emuInitialized_) {
            // ── No ROM loaded → open file browser ─────────────────────────
            browserActive_ = true;
            enterEmulatorMode();  // park radios — Meshtastic UI → browser
            {
                concurrency::LockGuard g(spiLock);
                browser_.open("/");
            }
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            if (disp && !savedFlushCb_) {
                savedFlushCb_ = (void *)disp->flush_cb;
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
                    lv_display_flush_ready(d);
                });
            }
#endif
            return;
        }

        // ── Toggle emulator on/off ────────────────────────────────────────
        emulatorActive_ = !emulatorActive_;
        if (emulatorActive_) enterEmulatorMode();   // Meshtastic UI → emulator
        else                 exitEmulatorMode();     // emulator → Meshtastic UI
        // Flag save for runOnce() — don't save here (SD write blocks LVGL callback)
        if (!emulatorActive_ && emu_.isRunning()) {
            pendingSave_ = true;
        }
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp) {
            if (emulatorActive_) {
                savedFlushCb_ = (void *)disp->flush_cb;
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
                    lv_display_flush_ready(d);
                });
                lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
                if (gfx) {
                    gfx->clearClipRect();
                }
            } else {
                lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
                if (gfx) {
                    gfx->clearClipRect();
                }
                if (savedFlushCb_) {
                    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                    savedFlushCb_ = nullptr;
                }
                lv_obj_invalidate(lv_screen_active());
            }
        }
#endif
#if HAS_SCREEN
        if (emulatorActive_) requestFocus();
#endif
        // Switch keyboard mode to match emulator state
        kbSetMode(emulatorActive_);
        return;
    }

    // ── File browser key handling ──────────────────────────────────────
    if (browserActive_) {
        // Backspace exits browser → Meshtastic UI (since mic button is
        // disabled in browser mode to avoid eating keypresses)
        if (ascii == 0x08) {
            browserActive_ = false;
            exitEmulatorMode();  // browser → Meshtastic UI
#if HAS_TFT
            lv_display_t *disp2 = lv_display_get_default();
            if (disp2 && savedFlushCb_) {
                lv_display_set_flush_cb(disp2, (lv_display_flush_cb_t)savedFlushCb_);
                savedFlushCb_ = nullptr;
                lv_obj_invalidate(lv_screen_active());
            }
#endif
            return;
        }
        // Buffer key for processing in runOnce — avoid doing SPI/rendering
        // from within the LVGL callback context
        pendingBrowserKey_ = ascii;
        return;
    }

    if (!emulatorActive_) return;

    // ── Tab: debug overlay toggle ──────────────────────────────────────
    if (ascii == 0x09) {
        debugActive_ = !debugActive_;
        return;
    }

    // ── Game Boy button mapping ────────────────────────────────────────
    uint8_t bit = 0;
    switch (ascii) {
        case 'w': case 'W': bit = GB_BTN_UP;     break;
        case 's': case 'S': bit = GB_BTN_DOWN;   break;
        case 'a': case 'A': bit = GB_BTN_LEFT;   break;
        case 'd': case 'D': bit = GB_BTN_RIGHT;  break;
        case 'k': case 'K': bit = GB_BTN_A;      break;
        case 'l': case 'L': bit = GB_BTN_B;      break;
        case '\r': case '\n': bit = GB_BTN_START;  break;
        case ' ': case 0x08: bit = GB_BTN_SELECT; break;
        default: return;
    }
    joypadState_ |= bit;
    kbMask_ |= bit;
}

// ── renderBrowser() — draw file browser to TFT ─────────────────────────────

void MonsterMeshModule::renderBrowser()
{
#if HAS_TFT
    if (!browser_.isDirty()) return;
    browser_.clearDirty();

    lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
    if (!gfx) return;
    LOG_INFO("[MonsterMesh] renderBrowser: drawing count=%d w=%d h=%d rot=%d\n",
             browser_.count(), gfx->width(), gfx->height(), (int)gfx->getRotation());

    // At textSize(1): 6x8px per char → 53 chars/line, 30 rows
    // Row layout: 14px per row, max ~26 chars with textSize(1) scaled x2 would wrap
    // Use textSize(1) for entries — fits more, no wrapping
    static constexpr int ROW_H = 14;
    static constexpr int LIST_Y = 28;
    static constexpr int MAX_ROWS = 14;
    static constexpr int MAX_CHARS = 52;  // 320px / 6px per char

    concurrency::LockGuard g(spiLock);
    gfx->startWrite();
    gfx->setClipRect(0, 0, 320, 240);
    gfx->fillScreen(0x0000);
    gfx->setTextWrap(false);  // prevent wrapping into next row

    // Title
    gfx->setTextSize(2);
    gfx->setTextColor(0x07E0);  // green
    gfx->setCursor(4, 2);
    gfx->print("Select ROM");

    // Current directory (right-aligned, small)
    gfx->setTextSize(1);
    gfx->setTextColor(0x7BEF);  // grey
    gfx->setCursor(200, 8);
    gfx->print(browser_.currentDir());

    gfx->setTextSize(1);

    if (browser_.count() == 0) {
        gfx->setTextSize(2);
        gfx->setTextColor(0xF800);  // red
        gfx->setCursor(4, LIST_Y + 20);
        gfx->print("No files found");
        gfx->setTextSize(1);
        gfx->setTextColor(0xFFFF);
        gfx->setCursor(4, LIST_Y + 44);
        gfx->print("Path: ");
        gfx->print(browser_.currentDir());
    } else {
        int scroll = browser_.scroll();
        int cursor = browser_.cursor();
        for (int i = 0; i < MAX_ROWS && (scroll + i) < browser_.count(); i++) {
            int idx = scroll + i;
            int y = LIST_Y + i * ROW_H;

            if (idx == cursor) {
                gfx->fillRect(0, y, 320, ROW_H, 0x000F);  // dark blue highlight
            }

            gfx->setCursor(4, y + 3);

            const auto &entry = browser_.entries()[idx];
            // Truncate name to fit screen
            char dispName[MAX_CHARS + 1];

            if (entry.isDir) {
                gfx->setTextColor(0xFFE0);  // yellow
                snprintf(dispName, sizeof(dispName), "[%s]", entry.name);
            } else {
                size_t nlen = strlen(entry.name);
                bool isRom = (nlen >= 3 && strcasecmp(entry.name + nlen - 3, ".gb") == 0) ||
                             (nlen >= 4 && strcasecmp(entry.name + nlen - 4, ".gbc") == 0);
                gfx->setTextColor(isRom ? 0x07E0 : 0x7BEF);  // green for ROMs, grey otherwise
                strncpy(dispName, entry.name, MAX_CHARS);
                dispName[MAX_CHARS] = '\0';
            }
            gfx->print(dispName);
        }
    }

    // Footer with status
    gfx->setTextColor(0x528A);
    gfx->setCursor(4, 212);
    gfx->print("W/S:Nav  K:Open  L:Back  ALT:Exit");
    // Show error/status if any
    if (setupStatus_ && strstr(setupStatus_, "FAIL")) {
        gfx->setCursor(4, 222);
        gfx->setTextColor(0xF800);  // red
        gfx->print(setupStatus_);
    }

    // Debug: show indev info
    gfx->setCursor(4, 232);
    gfx->setTextColor(0xF800);  // red
    {
        int nIndev = 0;
        char types[32] = {};
        lv_indev_t *ind = nullptr;
        while ((ind = lv_indev_get_next(ind)) != nullptr) {
            if (nIndev < 8) {
                char t[4];
                snprintf(t, sizeof(t), "%d ", (int)lv_indev_get_type(ind));
                strcat(types, t);
            }
            nIndev++;
        }
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "indevs:%d types:[%s] hook:%s", nIndev, types,
                 g_kbIndev ? "YES" : "NO");
        gfx->print(dbg);
    }

    gfx->clearClipRect();
    gfx->endWrite();
#endif
}

// ── launchROM() — load selected ROM and start emulator ──────────────────────

void MonsterMeshModule::launchROM(const char *path)
{
    // Browser returns SD-relative paths like "/pokemon.gb"
    // POSIX fopen needs VFS path "/sd/pokemon.gb"
    char vfsPath[FB_MAX_PATH + 4];
    snprintf(vfsPath, sizeof(vfsPath), "/sd%s", path);
    LOG_INFO("[MonsterMesh] Launching ROM: %s\n", vfsPath);

    // Don't hold spiLock here — SD.open() needs SPI access internally
    bool romOk = emu_.begin(vfsPath);
    if (!romOk) {
        LOG_WARN("[MonsterMesh] Failed to load ROM: %s\n", vfsPath);
        snprintf(setupStatusBuf_, sizeof(setupStatusBuf_), "FAIL: %s", vfsPath);
        setupStatus_ = setupStatusBuf_;
        browser_.markDirty();  // redraw browser
        return;
    }

    emuInitialized_ = true;
    browserActive_ = false;
    emulatorActive_ = true;

    // Allocate PSRAM framebuffer for rendering (320x240 RGB565 = 153,600 bytes)
    if (!frameBuf_) {
        frameBuf_ = static_cast<uint16_t *>(ps_malloc(PM_DISP_W * PM_DISP_H * sizeof(uint16_t)));
        if (frameBuf_) {
            memset(frameBuf_, 0, PM_DISP_W * PM_DISP_H * sizeof(uint16_t));
            LOG_INFO("[MonsterMesh] Framebuffer allocated in PSRAM (%u bytes)\n",
                     (unsigned)(PM_DISP_W * PM_DISP_H * sizeof(uint16_t)));
        } else {
            LOG_WARN("[MonsterMesh] PSRAM framebuffer alloc failed\n");
        }
    }

    // Create emulator FreeRTOS task on Core 1 (high priority — never stalls)
    if (!emuTaskHandle_) {
        xTaskCreatePinnedToCore(
            emuTaskEntry, "monstermesh_emu",
            16384, this, 5, &emuTaskHandle_, 1
        );
    }

    // Create render task on Core 0 (lower priority — blits framebuffer to TFT
    // without blocking the emulator task, so audio stays smooth)
    if (!renderTaskHandle_) {
        xTaskCreatePinnedToCore(
            renderTaskEntry, "monstermesh_render",
            4096, this, 2, &renderTaskHandle_, 0
        );
    }

    // Set up LGFX for emulator rendering (LVGL flush already suppressed)
#if HAS_TFT
    lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
    if (gfx) {
        gfx->clearClipRect();
    }
#endif

    kbSetMode(true);  // RAW mode for held-key d-pad input
    setupStatus_ = "Playing!";
    LOG_INFO("[MonsterMesh] ROM loaded, emulator started\n");
}

// ── Hard radio kill on mode switch ───────────────────────────────────────────
// Called on edge transitions (Meshtastic UI ↔ emulator/browser). Replaces all
// the soft TX-gate / IRQ-disable scaffolding from prior branches: when the
// user opens the emulator or ROM browser, both LoRa and WiFi are physically
// off until they ALT back. BLE is independent and stays on always.

void MonsterMeshModule::enterEmulatorMode()
{
    if (radioParked_) return;
    LOG_INFO("MonsterMesh: entering emulator mode — sleeping radios\n");
    if (RadioLibInterface::instance) {
        RadioLibInterface::instance->sleep();
    }
    LOG_INFO("MonsterMesh: radio asleep, calling deinitWifi\n");
    deinitWifi();
    LOG_INFO("MonsterMesh: deinitWifi returned — radios parked\n");
    radioParked_ = true;
}

void MonsterMeshModule::exitEmulatorMode()
{
    if (!radioParked_) return;
    LOG_INFO("MonsterMesh: exiting emulator mode — bringing radios back\n");
    if (RadioLibInterface::instance) {
        RadioLibInterface::instance->startReceive();
    }
    needReconnect = true;
    initWifi();
    radioParked_ = false;
}

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH
