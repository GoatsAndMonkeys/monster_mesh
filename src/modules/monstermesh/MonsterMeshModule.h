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
#include "MonsterMeshFileBrowser.h"
#include "MonsterMeshTerminal.h"
#include "PokemonDaycare.h"
#include "MonsterMeshTextBattle.h"
#include "LordGyms.h"
#ifdef MESHTASTIC_EXCLUDE_MONSTERMESH_DUNGEON
// Stub no-op stand-ins so MonsterMeshModule still compiles when the
// roguelike dungeon crawler is excluded from the build. Same API surface
// the module touches; everything is inline + does nothing.
namespace lgfx { inline namespace v1 { class LGFX_Device; } }
class DungeonGame {
  public:
    DungeonGame(MeshtasticTransport &) {}
    void begin() {}
    void tick(uint32_t) {}
    bool isActive() const { return false; }
    void handlePacket(const uint8_t *, size_t) {}
    void handleLocalCommand(const char *, const char *) {}
};
class DungeonOverlay {
  public:
    DungeonOverlay(DungeonGame &) {}
    bool isActive() const { return false; }
    void open() {}
    void close() {}
    void forceRedraw() {}
    void render(lgfx::LGFX_Device *) {}
};
#else
#include "dungeon/DungeonGame.h"
#include "dungeon/DungeonOverlay.h"
#endif

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
    bool isEmulatorActive()  const { return emulatorActive_; }
    bool isBrowserActive()   const { return browserActive_; }
    bool isTerminalActive()  const { return terminalActive_; }
    // Terminal panel exists AND is currently the foregrounded LVGL widget.
    // Different from isTerminalActive(): once the user opens the terminal we
    // keep it alive when they navigate to other Meshtastic panels (chat,
    // nodes, etc.), so isTerminalActive() stays true. isTerminalForeground()
    // flips false the moment any ancestor is hidden by the nav system, so
    // the I2C keyboard reader knows to let chat panels see keystrokes.
    bool isTerminalForeground() const {
        return terminalActive_ && terminal_.hasInputFocus();
    }
    bool isDungeonActive()   const { return dungeonActive_; }

  protected:
    // ── SinglePortModule overrides ──────────────────────────────────────────
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;

    // ── MeshModule UI overrides ─────────────────────────────────────────────
#if HAS_SCREEN
    virtual bool wantUIFrame() override { return true; }  // always show frame for debug
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                           int16_t x, int16_t y) override;
    virtual bool interceptingKeyboardInput() override { return emulatorActive_ || browserActive_ || dungeonActive_; }
#endif

    // ── OSThread override ───────────────────────────────────────────────────
    virtual int32_t runOnce() override;

    // ── Module setup ────────────────────────────────────────────────────────
    virtual void setup() override;

  private:
    MeshtasticTransport   transport_;
    MonsterMeshEmulator      emu_;
    MonsterMeshFileBrowser   browser_;
    MonsterMeshTerminal      terminal_;
    PokemonDaycare           daycare_;
    MonsterMeshTextBattle    textBattle_{transport_};
    DungeonGame              dungeon_{transport_};
    DungeonOverlay           dungeonOverlay_{dungeon_};
    bool     dungeonActive_        = false;
    uint32_t lastDungeonRenderMs_  = 0;

    bool textBattleActive_   = false;
    bool textBattleStartReq_ = false;  // LVGL→runOnce flag to start a local fight
    // 0xFF = mirror match / neighbor pick. 0..7 = LoC gym battle.
    uint8_t gymBattleIdx_    = 0xFF;
    uint8_t gymTrainerIdx_   = 0;
    // Captured at battle start so the end-of-battle dispatcher knows
    // which gym/trainer to advance even after gymBattleIdx_ resets.
    uint8_t activeGymBattle_  = 0xFF;
    uint8_t activeGymTrainer_ = 0;
    // 0xFF = not an explore battle. 0..7 = LordRoutes index. Set by
    // requestExplore(); read at start (build wild party) + end (callback
    // to terminal_.onExploreBattleEnded).
    uint8_t exploreRouteIdx_  = 0xFF;
    uint8_t activeExploreRoute_ = 0xFF;
    uint8_t activeExploreLevel_ = 0;
    // E4 gauntlet bookkeeping. e4MemberIdx_ is the requested start (0..4),
    // activeE4Member_ tracks the current trainer during the chain. 0xFF =
    // not an E4 battle.
    uint8_t e4MemberIdx_      = 0xFF;
    uint8_t activeE4Member_   = 0xFF;

    // ── BBS gym fight state (Phase C-2 send-party-once model) ──────────────
    // Set by the bbsFightFn callback. Cleared on completion / timeout.
    uint32_t  bbsFightTarget_     = 0;       // gym node ID we sent REQUEST to
    uint32_t  bbsFightRequestMs_  = 0;       // millis() of last request
    bool      bbsFightAwaitParty_ = false;   // collecting TEXT_BATTLE_PARTY chunks
    bool      bbsFightActive_     = false;   // local battle is running, expect RESULT
    bool      bbsBattleStartPending_ = false; // chunks complete; runOnce will fillScreen + startLocal
    uint8_t   bbsPartyChunks_[512] = {};     // reassembly buffer (Gen1Party = 404 B)
    uint8_t   bbsPartyChunkMask_  = 0;       // bitmask of received chunks
    uint8_t   bbsPartyTotal_      = 0;       // expected chunk count from sender
    Gen1Party bbsGymParty_        = {};      // reassembled gym party

    // ── MMG gym ladder ─────────────────────────────────────────────────────
    // Bulk-dump path (preferred): challenger sends BBS_LADDER_REQUEST once;
    // gym replies with BBS_LADDER_NAMES + BBS_LADDER_PARTIES carrying all
    // 5 trainers. Challenger then runs every fight locally with no
    // mid-ladder LoRa. Falls back to the legacy per-trainer
    // BBS_FIGHT_REQUEST chain if no bulk reply arrives within ~5s.
    uint8_t   bbsLadderTrainerIdx_     = 0xFF;  // 0..4 = current trainer; 0xFF = no ladder
    uint8_t   bbsLadderCount_          = 5;     // total trainers in the ladder
    bool      bbsLadderRequestPending_ = false; // legacy path: runOnce re-fires REQUEST

    // Bulk-dump cache.
    char      bbsLadderNames_[5][17]   = {};
    Gen1Party bbsLadderParties_[5]     = {};
    bool      bbsLadderHaveNames_      = false;
    bool      bbsLadderHaveParties_    = false;
    uint32_t  bbsLadderRequestSentMs_  = 0;     // for bulk-reply timeout fallback
    bool      bbsLadderBulkActive_     = false; // we're driving the bulk path
    bool      bbsLadderStartPending_   = false; // both bulk packets in; runOnce kicks off battle 0

    bool emulatorActive_     = false;
    bool terminalActive_     = false;
    uint8_t brightness_      = 255;
    volatile bool pendingSave_ = false;  // deferred save — done in runOnce() not callback
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
    void ejectROM();   // SYM+ALT: pause to browser, keep cart loaded
    void clearCart();  // [Eject Cart] entry in browser: actually unload
    void toggleTerminal();  // map-button hook from device-ui

    // Called by the LVGL indev tick on the LVGL thread. If a SAV-loaded
    // party has been staged by runOnce, push it into the terminal widget
    // here so all LVGL ops stay on the LVGL thread.
    void tryConsumeStagedParty();

    // Send a text DM to a node (or NODENUM_BROADCAST). Used by daycare
    // callbacks for visitor messages and broadcasts.
    void sendTextDM(uint32_t to, const char *text);
    void sendMmbPartyChunks(uint32_t to, const Gen1Party &party);
    // Broadcast TEXT_BATTLE_START (the seed + handshake header) when the
    // sender's Y-DM is received. Doing this from the module — instead of
    // inside startNetworkedAsInitiator — lets the receiver arm its
    // mmbPartyRxFrom_ before our party chunks arrive (avoiding a deadlock
    // where chunks were ignored because the receiver hadn't yet seen a
    // BATTLE_START to know to expect them).
    void sendMmbBattleStart(uint32_t seed);

    // Run an in-game daycare check-in for the most recent party loaded from
    // SAV. Safe to call when no ROM is loaded — silently no-ops.
    void daycareCheckInFromStagedParty();

    // T2: ask runOnce to begin a local CPU battle on the LoRa thread. The
    // LVGL thread sets the flag from the terminal `fight` command; runOnce
    // suppresses LVGL flush, clears the screen, builds a CPU rival party,
    // and calls textBattle_.startLocal().
    void requestLocalTextBattle() { textBattleStartReq_ = true; }
    bool isTextBattleActive() const { return textBattleActive_; }

    // L3: request a gym battle. gymIdx 0..7, trainerIdx 0..4 (4 = leader).
    // Sets the same kick-off flags as a local battle but flagged as a gym
    // fight so runOnce builds the gym party from LordGyms.h instead of a
    // daycare neighbor.
    void requestGymBattle(uint8_t gymIdx, uint8_t trainerIdx) {
        textBattleStartReq_ = true;
        gymBattleIdx_       = gymIdx;
        gymTrainerIdx_      = trainerIdx;
    }

    // L4: request a wild explore battle on `routeIdx` (0..7). Module's
    // runOnce builds the wild party from LordRoutes.cpp and starts a local
    // text battle. Tracked separately from gym/regular fights so the
    // battle-end path can call back into terminal_.onExploreBattleEnded.
    void requestExplore(uint8_t routeIdx) {
        textBattleStartReq_ = true;
        exploreRouteIdx_    = routeIdx;
    }

    // Indigo Plateau: 5-trainer gauntlet (4 Elite Four + Champion).
    // memberIdx 0..4. The module's end-of-battle dispatch chains members
    // until 4 falls or the player wipes. Resumes from `e4MemberIdx_`.
    void requestE4Battle(uint8_t memberIdx) {
        textBattleStartReq_ = true;
        e4MemberIdx_        = memberIdx;
    }

    // T4: resolve `peerShort` to a node num via NodeDB, print result to
    // the terminal panel, and arm a deferred MMT:ON DM. Public so the
    // terminal-fn lambda can call it (terminal_ is private otherwise).
    void challengePeerByShortName(const char *peerShort);

    // Fill `buf` with a multi-line daycare status report (newline-separated).
    // Used by the terminal `daycare` command.
    void daycareStatusString(char *buf, size_t bufLen);
    // Fill `buf` with the achievements list. Used by the terminal
    // `achievements` command.
    void achievementsString(char *buf, size_t bufLen);
    const char *getSetupStatus() const { return setupStatus_; }
    // RAW mode: set joypad directly from bitmask (bypasses press/release timer)
    void setJoypadDirect(uint8_t mask) { joypadState_ = mask; kbMask_ = 0; }
private:

    // Joypad state
    volatile uint8_t joypadState_ = 0;
    uint8_t kbMask_ = 0;

    // Buffered browser key (set by LVGL callback, consumed by runOnce)
    volatile uint8_t pendingBrowserKey_ = 0;

    // Set true by LVGL thread on browser entry; consumed by runOnce on LoRa
    // thread which then runs the SD reinit + scan. Keeps LVGL thread fast.
    volatile bool browserNeedsScan_ = false;
    // ALT pressed in chat/terminal → user wants ROM loader. LVGL thread
    // sets this flag and returns immediately; runOnce on the LoRa thread
    // does the spiLock-guarded fillScreen + flush_cb swap + radio park.
    // Avoids the LVGL-vs-DeviceUI deadlock that froze the screen during
    // the 250-node restore.
    volatile bool pendingBrowserActivate_ = false;

    // Set true by LVGL thread when terminal opens; consumed by runOnce on LoRa
    // thread which loads the party from SAV. Keeps the LVGL thread free of
    // SD I/O so the panel paints immediately.
    volatile bool terminalNeedsParty_ = false;

    // Set by LoRa-thread runOnce after a successful SAV load. The LVGL
    // keypad indev callback (which runs on the LVGL thread) consumes this
    // and pushes the party into terminal_, so all LVGL widget ops happen
    // on the LVGL thread.
    Gen1Party terminalStagedParty_ = {};
    volatile bool terminalPartyStaged_ = false;

    // Battle-end LVGL cleanup: set true from the LoRa-thread runOnce when
    // a battle wraps up. tryConsumeStagedParty (LVGL thread) drains it and
    // does the lv_obj_invalidate / lv_refr_now / refocus work. Keeps all
    // LVGL widget ops on the LVGL thread; the lgfx fillScreen is safe from
    // any thread because spiLock guards it.
    volatile bool pendingBattleEndCleanup_ = false;

    // Battle-result callback deferral. The runOnce dispatch fills these
    // and sets pendingBattleEndedCb_ instead of calling terminal_.onXxxEnded
    // directly — those callbacks lv_label_create() into the terminal output,
    // which is not safe from the LoRa thread. tryConsumeStagedParty drains
    // it on the LVGL thread.
    enum class StagedEndKind : uint8_t { NONE, GYM, EXPLORE, E4 };
    StagedEndKind stagedEndKind_ = StagedEndKind::NONE;
    uint8_t       stagedEndA_     = 0;   // gymIdx / routeIdx / memberIdx
    uint8_t       stagedEndB_     = 0;   // trainerIdx / encounter level
    bool          stagedEndWon_   = false;
    volatile bool pendingBattleEndedCb_ = false;

    // Per-faint XP drain. textBattle accumulates pendingXp_ across each
    // turn (one drop per defeated opponent); runOnce drains it on every
    // tick into stagedXp_, and tryConsumeStagedParty (LVGL thread) flushes
    // that into terminal_.creditBattleXp. Keeps SAV writes off the LoRa
    // thread.
    volatile bool pendingXpAwardCb_ = false;
    uint32_t      stagedXp_[6]      = {};

    // Battle XP write-back to /<rom>.sav on the SD card. Captured at SAV
    // load, the path lets us write back AFTER a battle ends without
    // re-scanning the SD card. Gated on !emulatorActive_ + no cart loaded
    // so we never trample emu state mid-game.
    char          loadedSavPath_[256] = {};
    volatile bool pendingSavWriteBack_ = false;

    // Event-driven daycare-XP → .sav flush. Tracks the last daycare event
    // we've synced; when daycare.getLastEventTime() advances past it (a
    // new event = potential XP change) we flush the .sav once. SD I/O
    // only happens when XP actually moves — typically a couple times per
    // hour at most.
    uint32_t      lastSavSyncedEventTime_ = 0;

    // T4: simple challenge handshake — sender stores who they're awaiting
    // a reply from, and handleReceived parses any DM from that peer for
    // Y/N. Drains stage outbound TX from runOnce per
    // feedback_mm_defer_tx_from_router.md.
    volatile bool pendingMmtOnTx_         = false;  // queued challenge DM
    uint32_t      mmtOnTxTarget_          = 0;
    uint32_t      mmtAwaitingReplyFrom_   = 0;      // sender: peer we expect a Y/N from
    char          mmtPeerShort_[12]       = {};     // peer's short_name for prompts
    volatile bool pendingMmtAccepted_     = false;  // peer said yes
    volatile bool pendingMmtDeclined_     = false;  // peer said no
    volatile bool pendingMmtAcceptedTx_   = false;  // queue "battle start" DM
    uint32_t      mmtAcceptedTxTarget_    = 0;

    // T4 phase 3: live PvP launch. Sender flips
    // pendingMmtBattleAsInitiator_ when the peer's Y arrives — runOnce
    // calls textBattle_.startNetworkedAsInitiator and sets up the screen.
    // Receiver flips pendingMmtBattleAsReceiver_ when a TEXT_BATTLE_START
    // packet arrives — runOnce calls startNetworkedAsReceiver with the
    // saved seed/peer.
    volatile bool pendingMmtBattleAsInitiator_ = false;
    volatile bool pendingMmtBattleAsReceiver_  = false;
    uint32_t      mmtBattlePeer_     = 0;
    uint32_t      mmtBattleSeed_     = 0;
    uint16_t      mmtBattleSession_  = 0;
    // millis() when pendingMmtBattleAsReceiver_ was set. If the terminal party
    // hasn't loaded within 30s, the challenge is stale and we discard it so a
    // latent TEXT_BATTLE_START packet doesn't auto-launch a battle next time the
    // user opens the terminal.
    uint32_t      mmtBattleReceivePendingMs_ = 0;

    // Receiver-side PvP gate: only allow a TEXT_BATTLE_START to auto-launch
    // a battle if the same peer just sent us a "Do you want to battle..." DM
    // within the last 60 s. Stops other-agent gauntlet/dungeon stray
    // TEXT_BATTLE_START packets from auto-bouncing the user into PvP.
    uint32_t      mmtChallengerPeer_     = 0;
    uint32_t      mmtChallengerExpireMs_ = 0;

    // Decoded Gen 1 trainer name from the most recent SAV load. 7 chars + NUL.
    char stagedTrainerName_[8] = {};

    // One-shot: party-load auto-trigger has fired. We trigger as early as
    // possible (right after SD mount completes) since reading the SAV is just
    // an SD read, not a LoRa op.
    bool autoPartyLoadDone_ = false;

    // One-shot: the first daycare beacon broadcast has fired. We defer this
    // past ~30s into boot so the beacon TX doesn't race the PacketAPI /
    // NodeInfo init window that previously caused a boot loop
    // (feedback_mm_no_boot_beacon). Periodic beacons take over from there.
    bool firstBeaconDone_ = false;

    // Reciprocal-beacon trigger: when handleReceived adds a NEW daycare
    // neighbor we set this. runOnce drains it on the LoRa thread and fires
    // a forceBeacon() so the peer learns about us within seconds instead
    // of waiting up to 15 min for our next BEACON_INTERVAL_MS broadcast.
    // Throttled so a burst of new neighbors only triggers one reply beacon.
    volatile bool pendingReplyBeacon_ = false;
    uint32_t lastReplyBeaconMs_ = 0;

    // ── MMB direct-party-exchange protocol ──────────────────────────────
    // Beacon-derived opponent parties were producing self-fights when both
    // T-Decks ran the same SAV (or when the neighbor table had stale data).
    // After handshake (Y received OR TEXT_BATTLE_START received) both sides
    // send their full Gen1Party via TEXT_BATTLE_PARTY chunks point-to-point,
    // and the engine doesn't launch until each side has the other's party.
    //
    // mmbPartyTxTarget_: peer we owe a party to (0 = nothing to send).
    //   Set on Y receipt (sender) or TEXT_BATTLE_START receipt (receiver).
    //   runOnce drains by emitting chunks.
    // mmbPartyRxFrom_: peer we expect chunks from (0 = not collecting).
    //   Set at the same time as mmbPartyTxTarget_.
    // mmbOppParty_: assembled party once all chunks arrive.
    // mmbOppPartyReady_: true once chunks fully assembled and parsed.
    volatile uint32_t mmbPartyTxTarget_  = 0;
    volatile uint32_t mmbPartyRxFrom_    = 0;
    Gen1Party         mmbOppParty_       = {};
    volatile bool     mmbOppPartyReady_  = false;
    // Chunk reassembly buffer — Gen1Party = 404 bytes, with 100 bytes per
    // chunk that's 5 chunks. 8 * 100 = 800 bytes max. Lazy-allocated in
    // PSRAM on first incoming chunk so it doesn't take heap from the emu
    // task stack alloc (16KB) when a ROM is launched.
    uint8_t          *mmbPartyChunks_   = nullptr;
    static constexpr size_t MMB_PARTY_CHUNKS_BYTES = 800;
    uint8_t           mmbPartyChunkMask_ = 0;
    uint8_t           mmbPartyTotal_     = 0;

    // Last daycare event time we DM'd. When the daycare's lastEventTime moves
    // past this, runOnce DMs the new event. Covers both periodic events
    // (generated in tick()) and arrival events (generated from
    // handleReceived when a new neighbor's beacon arrives).
    uint32_t lastDmedEventTime_ = 0;

    // Auto check-in/out flags set by enter/exitEmulatorMode (LVGL thread) and
    // drained in runOnce on the LoRa thread. Keeps SD I/O off the LVGL thread.
    // checkOut: stop daycare while emulator/browser owns the SD bus.
    // checkin:  reload party from the (possibly updated) SAV and re-checkin.
    volatile bool pendingAutoCheckOut_ = false;
    volatile bool pendingAutoCheckin_  = false;

    // True when the user has scrolled up to the virtual [Eject Cart] row at
    // the top of the browser (only visible when emuInitialized_ is true).
    bool ejectFocused_ = false;

    // Set true by LVGL thread on emu/browser exit; consumed by runOnce on
    // LoRa thread which then runs RadioLibInterface::startReceive(). The
    // startReceive call goes through setStandby/checkNotification which
    // hangs on the LVGL thread.
    volatile bool radioNeedsRx_ = false;

    // Auto-save tracking
    uint8_t prevBattle_ = 0;

    // Emu task idle signal — set by emuTaskLoop when it parks itself
    // (because emulatorActive_=false), cleared as soon as it's running a
    // frame. The ALT-exit path polls this so it knows the emu task has
    // finished any in-progress runFrame + auto-save before we issue our
    // own SAV flush + screen wipe. Avoids the spiLock collisions between
    // emu task SD writes and the LVGL thread.
    volatile bool emuTaskIdle_ = true;

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
    // Separate saved flush cb for dungeon overlay — keeps it independent of
    // emulator/browser/textbattle savedFlushCb_ so mode-detection doesn't cross.
    void *dungeonFlushCb_ = nullptr;

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
    void drainTxQueue();

    // Ensure channel 1 is configured as "MonsterMesh Center"
    void ensureMonsterMeshChannel();

    // handleKeyPress declared public above for LVGL callback access

    // Render overlays on top of emulator
    void renderStatusOverlay();
    void renderDebugOverlay();

    // ── Hard radio kill on emulator/browser entry ──────────────────────────
    // Called on edge transitions between Meshtastic UI ↔ emulator/browser.
    // Puts LoRa to sleep + WiFi.mode(WIFI_OFF) on entry, brings them back
    // on exit. BLE is independent and stays on.
    void enterEmulatorMode();
    void exitEmulatorMode();
    bool radioParked_ = false;  // tracks current park state to avoid double-toggle
    bool wifiBooted_  = false;  // deferred WiFi: false until first auto-init or emu-exit

    // File browser
    void renderBrowser();
    void launchROM(const char *path);
};

extern MonsterMeshModule *monsterMeshModule;

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH
