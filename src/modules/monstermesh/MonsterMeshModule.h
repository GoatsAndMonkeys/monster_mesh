#pragma once

#include "configuration.h"

#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "mesh/MeshService.h"
#include "mesh/Router.h"
#include "input/InputBroker.h"
#include "Observer.h"
#include "MeshtasticTransport.h"
#include "MonsterMeshEmulator.h"
#include "MonsterMeshBattleShim.h"
#include "MonsterMeshLobby.h"
#include "MonsterMeshFileBrowser.h"
#include "PokemonDaycare.h"
#include "MonsterMeshTerminal.h"

// ── MonsterMeshModule ──────────────────────────────────────────────────────────
// Meshtastic module that runs a Game Boy Pokemon emulator with LoRa-based
// multiplayer battles via the mesh network.
//
// Architecture:
//   - Extends SinglePortModule (PRIVATE_APP port 256) for packet handling
//   - Extends OSThread for periodic mesh-send drain
//   - Emulator runs on a dedicated FreeRTOS task (Core 1)
//   - All mesh traffic goes on channel 1 ("MonsterMesh Center")
//   - Ctrl+E toggles between emulator view and Meshtastic UI
//
// The emulator keeps running in the background when the user switches to
// Meshtastic's normal UI (chat, map, etc). Pressing Ctrl+E switches back.

class MonsterMeshModule : public SinglePortModule, public concurrency::OSThread
{
  public:
    MonsterMeshModule();
    virtual ~MonsterMeshModule() {}

    // Which channel we use for MonsterMesh traffic
    static constexpr uint8_t MONSTERMESH_CHANNEL = 1;

    // Is the emulator view currently active (vs Meshtastic UI)?
    bool isEmulatorActive() const { return emulatorActive_; }
    bool isBrowserActive()  const { return browserActive_; }

  protected:
    // ── SinglePortModule overrides ──────────────────────────────────────────
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;

    // ── MeshModule UI overrides ─────────────────────────────────────────────
#if HAS_SCREEN
    virtual bool wantUIFrame() override { return true; }  // always show frame for debug
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                           int16_t x, int16_t y) override;
    virtual bool interceptingKeyboardInput() override { return emulatorActive_ || browserActive_; }
#endif

    // ── OSThread override ───────────────────────────────────────────────────
    virtual int32_t runOnce() override;

    // ── Module setup ────────────────────────────────────────────────────────
    virtual void setup() override;

  private:
    MeshtasticTransport   transport_;
    MonsterMeshEmulator      emu_;
    MonsterMeshBattleShim    shim_;
    MonsterMeshLobby         lobby_;
    MonsterMeshFileBrowser   browser_;
    PokemonDaycare           daycare_;
    MonsterMeshTerminal      terminal_;

    bool emulatorActive_     = false;
    uint8_t brightness_      = 255;
    volatile bool pendingSave_   = false;  // deferred save — done in runOnce() not callback
    bool browserActive_      = false;
    bool setupDone_          = false;
    bool kbObserverRegistered_ = false;
    uint8_t setupRetries_ = 0;
    static constexpr uint8_t MAX_SETUP_RETRIES = 10;
    const char *setupStatus_ = "waiting...";
    char setupStatusBuf_[64] = {};
    bool emuInitialized_  = false;

    // Emulator FreeRTOS task
    TaskHandle_t emuTaskHandle_ = nullptr;
    static void emuTaskEntry(void *pv);
    void emuTaskLoop();

    // Render task — blits framebuffer to TFT without blocking emulator
    TaskHandle_t renderTaskHandle_ = nullptr;
    static void renderTaskEntry(void *pv);
    void renderTaskLoop();

    // Keyboard input (LVGL hook for TFT builds, InputBroker for non-TFT)
    void pollKeyboard();
    void installKeyboardHook();
public:
    uint32_t lastKeyMs_ = 0;
    void handleKeyFromLVGL(uint8_t c);
    void handleKeyPress(uint8_t ascii);
    void toggleSound();
    void adjustVolume(int8_t delta);
    void adjustBrightness(int8_t delta);
    MonsterMeshTerminal &terminal() { return terminal_; }
    const char *getSetupStatus() const { return setupStatus_; }
    // RAW mode: set joypad directly from bitmask (bypasses press/release timer)
    void setJoypadDirect(uint8_t mask) { joypadState_ = mask; kbMask_ = 0; }
private:

    // Joypad state
    volatile uint8_t joypadState_ = 0;
    uint8_t kbMask_ = 0;

    // Lobby UI state
    bool lobbyOpen_ = false;
    volatile uint8_t lobbyKey_ = 0;

    // Buffered browser key (set by LVGL callback, consumed by runOnce)
    volatile uint8_t pendingBrowserKey_ = 0;
    // Deferred browser open (set by LVGL callback, executed in runOnce to avoid SD on LVGL task)
    volatile bool pendingBrowserOpen_ = false;

    // Auto-save tracking
    uint8_t prevBattle_ = 0;
    uint16_t opponentElo_ = 0;

    // Cable disconnect cooldown — ignore stale "cable on" for 10s after disconnect
    uint32_t cableOffMs_ = 0;

    // Pending challenge — waiting for Y/N reply from receiver
    uint32_t pendingChallengerFrom_ = 0;  // RECEIVER: node that sent us "mmc on" (waiting for our Y/N)
    uint32_t pendingChallengeMs_    = 0;  // RECEIVER: time challenge arrived (60s timeout)
    uint32_t waitingForAcceptFrom_  = 0;  // INITIATOR: node we sent "mmc on" to (waiting for Y/N back)

    const char *getShortName(uint32_t nodeId);

    // Viewport
    volatile int8_t viewportDelta_ = 0;
    volatile bool   viewportRecenter_ = false;

    // Debug overlay
    bool debugActive_ = false;

    // Hardware button toggle (GPIO 0 / mic button)
    volatile bool buttonTogglePending_ = false;
    uint32_t lastButtonMs_ = 0;
    static void IRAM_ATTR buttonISR(void *arg);

    // Saved LVGL flush callback (swapped out when emulator is active)
    void *savedFlushCb_ = nullptr;

    // Input observer — receives keyboard events from Meshtastic's InputBroker
    int handleInputEvent(const InputEvent *event);
    CallbackObserver<MonsterMeshModule, const InputEvent *> inputObserver_ =
        CallbackObserver<MonsterMeshModule, const InputEvent *>(this, &MonsterMeshModule::handleInputEvent);

    // Key release tracking (keyboard sends single press, no release)
    // lastKeyMs_ declared public above for LVGL callback access
    static constexpr uint32_t KEY_RELEASE_MS = 100;  // release after 100ms

    // Framebuffer rendering — scanline writes to PSRAM buffer, blitFrame pushes to TFT
    uint16_t *frameBuf_ = nullptr;  // 320x240 RGB565 in PSRAM
    volatile bool frameDirty_ = false;
    volatile bool renderFrame_ = false;  // true only on frames that should render
    static void scanlineCallback(uint8_t line, const uint16_t *pixels320,
                                  int16_t screenY0, int16_t screenY1, void *ctx);
    void blitFrame();

    // Send queued packets to mesh
    void sendTextDM(uint32_t to, const char *text);
    void drainTxQueue();

    // Ensure channel 1 is configured as "MonsterMesh Center"
    void ensureMonsterMeshChannel();

    // handleKeyPress declared public above for LVGL callback access

    // Render overlays on top of emulator
    void renderLobbyOverlay();
    void renderStatusOverlay();
    void renderDebugOverlay();

    // File browser
    void renderBrowser();
    void launchROM(const char *path);

    // Daycare
    void daycareCheckIn();
    void daycareCheckOut();
    void daycareStatus(uint32_t replyTo);
    void daycareAutoCheckIn();  // load last .sav from SD, auto check-in without emulator
    bool pendingAutoCheckin_ = false;
};

extern MonsterMeshModule *monsterMeshModule;

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH
