#include "MonsterMeshModule.h"

#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH

#include "MeshService.h"
#include "Router.h"
#include "NodeDB.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "input/InputBroker.h"
#include <SD.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Wire.h>
#include "concurrency/LockGuard.h"
#include "SPILock.h"

#include "PokemonData.h"
#include "Gen1Species.h"

#if HAS_TFT
#include <LovyanGFX.hpp>
#endif

MonsterMeshModule *monsterMeshModule = nullptr;

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
      concurrency::OSThread("MonsterMesh"),
      shim_(transport_),
      lobby_(transport_, emu_)
{
    // We want to see all PRIVATE_APP packets on any channel, not just "our" channel,
    // because wantPacket filters by channel anyway.
    isPromiscuous = false;
    loopbackOk = false;
}

// ── setup() — called once after mesh is initialized ─────────────────────────

void MonsterMeshModule::setup()
{
    if (setupDone_) return;  // idempotent — prevent double-init
    setupDone_ = true;

    setupStatus_ = "setup started";
    LOG_INFO("[MonsterMesh] module setup\n");

    // Configure channel 1 as "MonsterMesh Center" if not already set
    setupStatus_ = "configuring channel";
    ensureMonsterMeshChannel();

    // Init transport
    transport_.begin();
    transport_.setNodeId(nodeDB->getNodeNum());

    // Init shim + lobby
    shim_.begin();
    shim_.setLobby(&lobby_);
    lobby_.setShim(&shim_);

    // Load saved stats (LittleFS should already be mounted by Meshtastic)
    lobby_.loadStats();

    // Wire serial link
    emu_.setSerialLink(&shim_);

    // Set up scanline callback for rendering
    emu_.setScanlineCallback(scanlineCallback, this);

    // Try to start emulator — SD shares SPI bus with TFT and LoRa, must use spiLock
    setupStatus_ = "SD init...";
    bool sdOk = false;
    {
        concurrency::LockGuard g(spiLock);
        sdOk = SD.begin(SPI_CS);
        if (!sdOk) {
            LOG_WARN("[MonsterMesh] SD.begin(CS=%d) failed, retrying with SPI bus...\n", SPI_CS);
            sdOk = SD.begin(SPI_CS, SPI, 4000000);
        }
        if (!sdOk) {
            LOG_WARN("[MonsterMesh] SD.begin() retry failed, trying without CS pin...\n");
            sdOk = SD.begin();
        }
    }

    if (sdOk) {
        LOG_INFO("[MonsterMesh] SD card mounted OK\n");

        // List root to help debug ROM path issues
        File root = SD.open("/");
        if (root) {
            File f = root.openNextFile();
            while (f) {
                LOG_INFO("[MonsterMesh] SD: %s (%d bytes)\n", f.name(), f.size());
                f = root.openNextFile();
            }
            root.close();
        }

        if (emu_.begin("/pokemon.gb")) {
            emuInitialized_ = true;
            LOG_INFO("[MonsterMesh] emulator initialized OK!\n");

            // Launch emulator task on Core 1
            xTaskCreatePinnedToCore(
                emuTaskEntry, "monstermesh_emu",
                16384,  // 16KB stack (peanut-gb + overlays)
                this,
                5,      // priority — high for smooth 60fps
                &emuTaskHandle_,
                1       // Core 1
            );

            // Auto-start emulator
            emulatorActive_ = true;
            setupStatus_ = "Running!";
#if HAS_SCREEN
            requestFocus();
#endif
        } else {
            setupStatus_ = "ROM /pokemon.gb not found";
            LOG_WARN("[MonsterMesh] emu_.begin('/pokemon.gb') failed — ROM not found?\n");
        }
    } else {
        setupStatus_ = "SD card init failed";
        LOG_WARN("[MonsterMesh] all SD init attempts failed — emulator disabled\n");
    }

    // Subscribe to InputBroker for keyboard events
    if (inputBroker) {
        inputObserver_.observe(inputBroker);
    }

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

    if (!emulatorActive_) return 0;  // let Meshtastic handle keys

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
    if (event->inputEvent == INPUT_BROKER_SELECT) {
        viewportRecenter_ = true;
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
        strncpy(ch.settings.name, "MonsterMesh", sizeof(ch.settings.name) - 1);
        ch.settings.name[sizeof(ch.settings.name) - 1] = '\0';
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
    // Push the raw payload into transport for BattleShim/Lobby to process
    if (mp.decoded.payload.size > 0) {
        transport_.pushReceivedPacket(
            mp.decoded.payload.bytes,
            mp.decoded.payload.size,
            mp.rx_rssi
        );
    }
    return ProcessMessage::STOP;  // consumed — don't pass to other modules
}

// ── runOnce() — OSThread periodic drain of tx queue ─────────────────────────

int32_t MonsterMeshModule::runOnce()
{
    // Lazy init — setup() is never called by Meshtastic, so we do it here
    if (!setupDone_) {
        if (millis() < 8000) return 500;  // wait for Meshtastic to fully boot
        setup();
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

// ── Keyboard — on base t-deck, InputBroker handles everything ────────────────
void MonsterMeshModule::installKeyboardHook() {}
void MonsterMeshModule::handleKeyFromLVGL(uint8_t c) { handleKeyPress(c); lastKeyMs_ = millis(); }
void MonsterMeshModule::pollKeyboard() {
    // Fallback: also poll I2C directly in case device-ui callback doesn't fire
    Wire.requestFrom((uint8_t)0x55, (uint8_t)1);
    if (!Wire.available()) return;
    uint8_t c = Wire.read();
    if (c == 0) return;
    handleKeyPress(c);
    lastKeyMs_ = millis();
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

    while (true) {
        // Run emulator frame (always, even when UI is showing Meshtastic screens)
        emu_.runFrame();

        // BattleShim tick (drives state machine + serial batch flush)
        shim_.tick();

        // ── Auto-save on battle end + ELO ────────────────────────────────
        uint8_t curBattle = emu_.readWRAM(Gen1::wIsInBattle);

        if (prevBattle_ == 0 && curBattle == 2 && opponentElo_ == 0) {
            uint16_t sid = shim_.sessionId();
            if (sid != 0) {
                uint32_t myId = transport_.nodeId();
                for (uint8_t i = 0; i < lobby_.peerCount(); i++) {
                    uint16_t testSid = (uint16_t)(myId ^ lobby_.peer(i).chipId);
                    if (testSid == sid) {
                        opponentElo_ = lobby_.peer(i).elo;
                        break;
                    }
                }
            }
        }

        if (prevBattle_ != 0 && curBattle == 0) {
            emu_.save();
            if (opponentElo_ > 0) {
                uint8_t partyCount = emu_.readWRAM(Gen1::wPartyCount);
                bool won = false;
                for (uint8_t i = 0; i < partyCount && i < 6; i++) {
                    uint16_t hp = ((uint16_t)emu_.readWRAM(Gen1::wPartyMons + i * 44 + 1) << 8) |
                                  emu_.readWRAM(Gen1::wPartyMons + i * 44 + 2);
                    if (hp > 0) { won = true; break; }
                }
                lobby_.recordResult(won, opponentElo_);
                opponentElo_ = 0;
            }
        }
        prevBattle_ = curBattle;

        // ── Lobby tick ───────────────────────────────────────────────────
        lobby_.tick(millis());

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

        // ── Lobby key input ──────────────────────────────────────────────
        uint8_t lk = lobbyKey_;
        if (lk) {
            lobbyKey_ = 0;
            // Process lobby key
            if (lobbyOpen_) {
                switch (lk) {
                    case 'w': case 'W': lobby_.navigateUp();   break;
                    case 's': case 'S': lobby_.navigateDown(); break;
                    case 'k': case 'K': lobby_.selectPeer();   break;
                    case 'l': case 'L':
                        if (lobby_.state() == MonsterMeshLobby::State::INCOMING)
                            lobby_.rejectIncoming();
                        else if (lobby_.state() == MonsterMeshLobby::State::CHALLENGING) {
                            lobby_.close();
                            lobby_.open();
                        }
                        break;
                }
            }
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

        vTaskDelayUntil(&lastWake, framePeriod);
    }
}

// ── Scanline callback — blits to TFT via Meshtastic's display ───────────────

void MonsterMeshModule::scanlineCallback(uint8_t line, const uint16_t *pixels320,
                                       int16_t screenY0, int16_t screenY1, void *ctx)
{
    MonsterMeshModule *self = static_cast<MonsterMeshModule *>(ctx);
    if (!self->emulatorActive_) return;  // don't draw if Meshtastic UI is showing

    // Access TFT directly via LovyanGFX (available on T-Deck)
    // The LGFX class is defined locally in TFTDisplay.cpp, so we use the base
    // class pointer exposed via getTftDevice(). For non-TFT builds, this is a no-op.
#if HAS_TFT
    extern lgfx::LGFX_Device *getLovyanGfx();
    lgfx::LGFX_Device *gfx = getLovyanGfx();
    if (!gfx) return;
    if (screenY0 >= 0 && screenY0 < PM_DISP_H)
        gfx->pushImage(0, screenY0, PM_DISP_W, 1, pixels320);
    if (screenY1 >= 0 && screenY1 < PM_DISP_H)
        gfx->pushImage(0, screenY1, PM_DISP_W, 1, pixels320);
#endif
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
    // ── ALT modifier (0x0c on T-Deck) ──────────────────────────────────
    if (ascii == 0x0c) {
        kbSym_ = !kbSym_;
        return;
    }

    // ── ALT+E or Ctrl+E: toggle emulator/Meshtastic UI ─────────────────
    if (ascii == 0x05 || (kbSym_ && (ascii == 'e' || ascii == 'E'))) {
        kbSym_ = false;
        emulatorActive_ = !emulatorActive_;
        if (emulatorActive_) {
#if HAS_SCREEN
            requestFocus();
#endif
        }
        return;
    }

    // Clear sym on any non-modifier key
    kbSym_ = false;

    if (!emulatorActive_) return;

    // ── Tab: debug overlay toggle ──────────────────────────────────────
    if (ascii == 0x09) {
        debugActive_ = !debugActive_;
        return;
    }

    // ── P: lobby toggle ────────────────────────────────────────────────
    if (ascii == 'p' || ascii == 'P') {
        if (lobbyOpen_) {
            lobby_.close();
            lobbyOpen_ = false;
        } else {
            lobby_.open();
            lobbyOpen_ = true;
        }
        return;
    }

    // ── Lobby capture mode ─────────────────────────────────────────────
    if (lobbyOpen_) {
        if (ascii == 'w' || ascii == 'W' || ascii == 's' || ascii == 'S' ||
            ascii == 'k' || ascii == 'K' || ascii == 'l' || ascii == 'L') {
            lobbyKey_ = ascii;
            return;
        }
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

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH
