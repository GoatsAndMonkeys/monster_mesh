// SPDX-License-Identifier: MIT
//
// GauntletModule — Meshtastic SinglePortModule that runs the multiplayer
// Pokemon Gym ladder. Players send DMs to the gym node:
//
//   !gym                      — show gym info + leader
//   !gym challenge            — start a challenge (prompts for party)
//   !gym roster               — show ranked ladder
//   !gym leader               — show current leader's party
//   !gym records              — show previous leaders
//   !gym help                 — command list
//   <party CSV>               — submit party after !gym challenge
//                                 e.g. "Pikachu,Charizard:60,Blastoise"
//
// Storage: /monstermesh/gauntlet.dat (FSCom).
// Battles: Gen1BattleEngine in CPU-vs-CPU auto-resolve mode (no LoRa rounds).
//
// Disable at compile time with -DMESHTASTIC_EXCLUDE_GAUNTLET=1

#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GAUNTLET

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "Observer.h"
#include "GauntletData.h"
#include "GauntletStorage.h"

class GauntletModule : public SinglePortModule, public concurrency::OSThread
{
  public:
    GauntletModule();

    // Read-only state access (used by potential UI overlays).
    const GauntletState &state() const { return state_; }

    // Apply a member configuration from a UI surface (on-device slide). Mirrors
    // the DM wizard's last step. memberIdx 0..4 (4 = leader). level 0 = stock,
    // 1..100 = boss override (other party members scale by canonical delta).
    // Returns false on bad arguments.
    bool gymAdminApply(uint8_t gymIdx, uint8_t memberIdx, uint8_t level);

    // On-device "Reset Admin" path. Clears the stored admin nodeNum + the
    // gym preset and IMMEDIATELY opens a 120 s claim window so a phone can
    // DM `admin set` and become the new admin. Only callable from the
    // device-side slide (physical-presence trust); the DM admin-reset path
    // does NOT reopen the window — for that you must reboot.
    void gymAdminResetAndOpenWindow();

    // Auto-populate the gym when no admin / preset is configured. Picks 4
    // random grunts from the 32-grunt pool (sorted ascending by canonical
    // boss level) and a random leader from {8 gym leaders, 4 Elite Four,
    // Champion}. Scales the leader down if their canonical boss out-levels
    // slot 4. Persists the choice — sticky across reboots.
    void autoSetup();

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    virtual int32_t runOnce() override;

#if HAS_SCREEN
    // Carousel frame — sits after Pikachu's frame in the carousel.
    virtual bool wantUIFrame() override { return true; }
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                            int16_t x, int16_t y) override;
    // While the admin slide is open, block carousel navigation.
    virtual bool interceptingKeyboardInput() override { return admUi_ != ADM_OFF; }
#endif

  private:
    GauntletState   state_;
    GauntletSession sessions_[GAUNTLET_SESSION_MAX];

    GauntletSession *getOrCreateSession(uint32_t nodeNum);
    void             expireSessions();

    // DM dispatch
    void handleCommand(const meshtastic_MeshPacket &mp,
                       GauntletSession &sess, const char *cmd);
    // `ping` — discovery probe reply for MonsterMesh Terminal `bbs` listing.
    // Replies with: GYM:<gymName>|<badge>|<leaderName>|<rosterSize>
    void handleCmdPing(const meshtastic_MeshPacket &mp);

    // ── Networked Gen 1 battle handler (Phase C-2) ──────────────────────────
    // Speaks the BattlePacket / TEXT_BATTLE_* protocol over PRIVATE_APP so the
    // MonsterMesh Terminal `bbs fight N` command lands in its existing battle
    // screen. Uses the same Gen1BattleEngine the rest of monstermesh uses,
    // with the gym acting as side 0 (CPU-driven via cpuPickAction).
    void handleBattlePacket(const meshtastic_MeshPacket &mp);
    void onNetBattleEnd();
    void teardownNetBattle();
    void sendBattleStart(uint32_t seed);
    void sendBattleAction(uint8_t actionType, uint8_t index);
    void buildGymBattleParty(struct Gen1Party &out);

    // ── Per-challenger ladder progress ───────────────────────────────────────
    // The MM Terminal challenger drives a 5-trainer ladder by re-firing
    // BBS_FIGHT_REQUEST after each win. We track per-sender progress so the
    // gym serves trainer 0..4 in sequence. Eviction at 10 min idle.
    struct GauntletLadderProgress {
        uint32_t nodeNum;        // 0 = empty slot
        uint8_t  ladderIdx;      // 0..4 = trainer to serve next
        uint32_t lastSeenMs;
    };
    static constexpr uint8_t  LADDER_SLOTS    = 8;
    static constexpr uint32_t LADDER_IDLE_MS  = 10UL * 60UL * 1000UL;  // 10 min
    GauntletLadderProgress ladders_[LADDER_SLOTS] = {};

    GauntletLadderProgress *lookupLadder(uint32_t nodeNum);
    GauntletLadderProgress *lookupOrInsertLadder(uint32_t nodeNum);
    void                    expireLadders();
    void                    buildLadderTrainerParty(struct Gen1Party &out, uint8_t idx);

    // BBS gym discovery — passive announcement.
    // The gauntlet broadcasts BBS_REPLY on the MonsterMesh channel every
    // BBS_BEACON_INTERVAL_MS so MM Terminals can build their `bbs` cache
    // without ever sending a probe (which would leak to LongFast). Same
    // packet format as a query response, just unsolicited.
    void sendBbsReply(uint32_t target, uint8_t channel);
    static constexpr uint8_t  MM_CHANNEL              = 1;        // MonsterMesh Center
    static constexpr uint32_t BBS_BEACON_INTERVAL_MS  = 1200000;  // 20 min
    static constexpr uint32_t BBS_BEACON_FIRST_MS     = 60000;    // 60s after boot

    // Forward-declare so the .h doesn't need to pull in the engine.
    class NetBattle; // opaque holder for Gen1BattleEngine + state
    NetBattle *netBattle_ = nullptr;
    uint32_t   netOpponent_   = 0;
    uint16_t   netSessionId_  = 0;
    uint32_t   netLastRxMs_   = 0;
    uint32_t   lastBeaconMs_  = 0;     // 0 = never beaconed yet

    // ── On-frame admin slide ─────────────────────────────────────────────────
    // The gym carousel frame doubles as the admin UI: short press cycles, long
    // press confirms. Physical-presence trust — no ACL on this path.
    enum AdminUi : uint8_t {
        ADM_OFF = 0,
        ADM_MAIN,            // Setup / Reset Admin / Back
        ADM_PICK_GYM,
        ADM_PICK_MEMBER,
        ADM_LEVEL_CHOICE,    // Stock / Custom
        ADM_LEVEL_TENS,
        ADM_LEVEL_ONES,
        ADM_SUMMARY,         // gym member level summary, auto-closes
        ADM_RESET_CONFIRM,   // Yes / No
        ADM_CLAIM_WINDOW,    // live countdown after reset (120 s)
        ADM_ADMIN_CLAIMED,   // "Admin set: SHORTNAME" splash on successful claim
        ADM_WINDOW_CLOSED,   // "window closed" splash when timer expired
        ADM_SHOW_ADMIN,      // display current admin shortname + nodeNum
    };
    AdminUi  admUi_         = ADM_OFF;
    uint8_t  admMainCursor_ = 0;
    uint8_t  admGymIdx_     = 0;
    uint8_t  admMemberIdx_  = 0;
    uint8_t  admChoice_     = 0;
    uint8_t  admTens_       = 0;
    uint8_t  admOnes_       = 0;
    uint8_t  admResetConfirm_ = 0;
    uint32_t admSummaryUntil_ = 0;
    uint32_t lastDrawMs_    = 0;

    int handleInputEvent(const InputEvent *event);
    CallbackObserver<GauntletModule, const InputEvent *> inputObserver_ =
        CallbackObserver<GauntletModule, const InputEvent *>(
            this, &GauntletModule::handleInputEvent);

    void admEnter();
    void admExit();
    void admShortPress();
    void admLongPress();
    void admApplyAndShowSummary();
    void drawGymStatus(OLEDDisplay *d, int16_t ox, int16_t oy);
    void drawAdminSlide(OLEDDisplay *d, int16_t ox, int16_t oy);

    // ── Admin ACL ────────────────────────────────────────────────────────────
    // For 120 seconds after boot the gym accepts the first admin DM as a claim:
    // that sender's nodeNum is stamped into state_.adminNodeNum and saved.
    // Subsequent admin DMs only succeed if their sender matches. To re-claim
    // (e.g. after `admin reset`) the device MUST be physically rebooted —
    // no remote re-open. The on-device admin slide bypasses this entirely
    // (physical presence implies trust).
    uint32_t   adminClaimUntilMs_ = 0;
    bool       isAdminAuthorized(uint32_t sender);
    static constexpr uint32_t ADMIN_CLAIM_WINDOW_MS = 120000;
    // (handleParty / runGauntlet removed — gym no longer simulates battles.)

    // State updates
    void promoteToLeader(const meshtastic_MeshPacket &mp, GauntletSession &sess);
    void insertAtRank(const meshtastic_MeshPacket &mp, GauntletSession &sess,
                       uint8_t rank);

    // Reply helpers
    void sendReply(const meshtastic_MeshPacket &req, const char *text);
    const char *senderShortName(uint32_t nodeNum);
};

extern GauntletModule *gauntletModule;

#endif // !MESHTASTIC_EXCLUDE_GAUNTLET
