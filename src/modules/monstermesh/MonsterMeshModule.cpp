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
#include "LordRoutes.h"
#include "LordE4.h"
#include "LordLogic.h"
// Header-only gym helpers shared with the gauntlet (gym) module — used for
// reconstructing parties from the bulk ladder dump. Pure inline code; no
// link-time dependency on GauntletModule.cpp.
#include "gauntlet/Gen1MinimalStats.h"
#include "showdown_gen1_moves.h"
#include "RadioLibInterface.h"
#if HAS_TFT
#include "graphics/view/TFT/Themes.h"
#endif

// Provided by src/mesh/wifi/WiFiAPClient.cpp — used to park/unpark WiFi
// alongside LoRa when entering/exiting emulator or browser modes.
extern bool needReconnect;
extern bool wifiSuppressed;
extern bool g_meshSuspended;
extern void deinitWifi();

// Shared ALT-press debounce: both the runOnce poll path and the LVGL KEY-mode
// peek path can see the same I2C kb-byte microseconds apart, otherwise.
// One global timestamp prevents enterEmulatorMode being called twice.
static uint32_t g_lastAltFireMs = 0;

// KEY-mode peek edge-detector state (file-scope so the SYM+ALT eject path can
// reset it: a still-held ALT after eject must NOT be seen as a fresh edge).
static bool g_keyPeekAltWasPressed = false;
static bool g_keyPeekAltSeenLow    = false;

// Forward declaration — definition lives further down in this TU but runOnce
// needs to call it for the deferred terminal party load.
static bool loadPartyFromSavOnSd(const char *romPath, Gen1Party &out,
                                 char *resolvedSavOut, size_t resolvedSavLen,
                                 char *trainerNameOut, size_t trainerNameLen);
static bool patchSavOnSdWithDaycareXp(const char *romPath, PokemonDaycare &dc);
static bool patchSdSavPathWithDaycareXp(const char *sdRel, PokemonDaycare &dc);
static bool writePartyToSavOnSd(const char *savPath, const Gen1Party &party);
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

// Called from device-ui's map button (we repurposed it as the terminal entry).
// device-ui hands us the LVGL panel to parent into so the left nav stays visible.
extern "C" __attribute__((weak)) void monstermesh_set_terminal_cb(void (*cb)(void *parent)) { (void)cb; }
static lv_obj_t *g_terminalParent = nullptr;
// ALT-close suppresses re-open from the map-button callback for a brief
// window. LVGL's keypad indev forwards an ALT press as a CLICK on the
// focused widget, which would otherwise immediately re-fire mmTerminalToggle
// and re-open the panel we just closed.
static uint32_t g_terminalReopenBlockUntilMs = 0;
static void mmTerminalToggle(void *parent)
{
    if (millis() < g_terminalReopenBlockUntilMs) return;
    g_terminalParent = static_cast<lv_obj_t *>(parent);
    if (monsterMeshModule) monsterMeshModule->toggleTerminal();
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
    monstermesh_set_terminal_cb(mmTerminalToggle);
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

    // Text battle steals all keys while active (otherwise the post-battle
    // "press any key" never reaches textBattle and the user is stuck).
    if (textBattleActive_) {
        if (event->inputEvent == INPUT_BROKER_ANYKEY && event->kbchar != 0) {
            handleKeyPress(event->kbchar);
            lastKeyMs_ = millis();
        }
        return 1;
    }

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
    // No-op if channel 1 already exists. The user owns the PSK setup —
    // they configured it as the MonsterMesh chat channel and we just ride
    // along (same path daycare beacons use).
    auto &ch = channels.getByIndex(MONSTERMESH_CHANNEL);
    if (ch.role == meshtastic_Channel_Role_DISABLED ||
        strlen(ch.settings.name) == 0) {
        // Only write if the slot is completely empty — first-boot setup.
        ch.role = meshtastic_Channel_Role_SECONDARY;
        snprintf(ch.settings.name, sizeof(ch.settings.name), "MonsterMesh");
        ch.settings.psk.size = 1;
        ch.settings.psk.bytes[0] = 0; // user-driven PSK setup expected
        channels.setChannel(ch);
        Serial.println("[MonsterMesh] channel 1 default-named 'MonsterMesh' (set PSK in app)");
    }
}

// ── wantPacket() — filter incoming packets ──────────────────────────────────

bool MonsterMeshModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // Accept PRIVATE_APP packets on any channel — daycare beacons go out on
    // the primary channel (so they ride alongside normal mesh traffic), and
    // future MM packets may use MONSTERMESH_CHANNEL. handleReceived() filters
    // by payload type, so the channel filter is now redundant.
    if (p->decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        return true;
    }
    // T4: peek at TEXT_MESSAGE_APP DMs only when we have an outstanding
    // mmt challenge to that sender. handleReceived parses Y/N and returns
    // CONTINUE so the standard text pipeline still delivers to phone.
    if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
        nodeDB && p->to == nodeDB->getNodeNum() && !isBroadcast(p->to) &&
        mmtAwaitingReplyFrom_ != 0 && p->from == mmtAwaitingReplyFrom_) {
        return true;
    }
    // (BBS_REPLY arrives via PRIVATE_APP, already accepted above.)
    return false;
}

// ── handleReceived() — incoming mesh packet ─────────────────────────────────

ProcessMessage MonsterMeshModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Daycare beacons: PRIVATE_APP packets that exactly match the DaycareBeacon
    // wire size. Forward to the daycare manager; everything else is ignored.
    if (mp.decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        LOG_INFO("[MonsterMesh] PRIVATE_APP RX: ch=%u sz=%u from=0x%08X\n",
                 (unsigned)mp.channel, (unsigned)mp.decoded.payload.size,
                 (unsigned)mp.from);

        // BBS gym discovery — BBS_REPLY from a gym we probed. Parse and
        // forward to the terminal cache. Length-prefixed strings:
        //   u8 nameLen | name[] | u8 badgeLen | badge[]
        //   u8 leaderLen | leader[] | u8 rosterSize
        if (mp.decoded.payload.size >= BATTLELINK_HDR_SIZE) {
            const BattlePacket *bp =
                (const BattlePacket *)mp.decoded.payload.bytes;

            // T4 phase 3: live PvP routing. TEXT_BATTLE_START kicks off a
            // receiver-side battle; ACTION/HASH/FORFEIT feed the running
            // battle's engine. Module catches START to extract the seed
            // (textBattle.handlePacket throws it away). Defer the actual
            // engine spin-up to runOnce so the screen ops happen on the
            // main loop.
            // TEXT_BATTLE_START shares type byte 0x60 with DaycareBeacon
            // (which is 150+ bytes). Gate by EXACT length so a beacon
            // doesn't get misrouted as a stray battle-start. Battle-start
            // is exactly BATTLELINK_HDR_SIZE + 14 bytes.
            if ((PktType)bp->type == PktType::TEXT_BATTLE_START &&
                mp.decoded.payload.size == BATTLELINK_HDR_SIZE + 14 &&
                mp.from != nodeDB->getNodeNum()) {
                if (!textBattleActive_ && !pendingMmtBattleAsReceiver_ &&
                    !pendingMmtBattleAsInitiator_) {
                    uint32_t seed = ((uint32_t)bp->payload[0] << 24) |
                                    ((uint32_t)bp->payload[1] << 16) |
                                    ((uint32_t)bp->payload[2] <<  8) |
                                              bp->payload[3];
                    mmtBattlePeer_    = mp.from;
                    mmtBattleSeed_    = seed;
                    mmtBattleSession_ = bp->sessionId();
                    pendingMmtBattleAsReceiver_  = true;
                    mmtBattleReceivePendingMs_   = millis();
                    LOG_INFO("[MonsterMesh] PvP: TEXT_BATTLE_START rx from 0x%08X seed=0x%08X\n",
                             (unsigned)mp.from, (unsigned)seed);
                }
                return ProcessMessage::CONTINUE;
            }
            if (((PktType)bp->type == PktType::TEXT_BATTLE_ACTION ||
                 (PktType)bp->type == PktType::TEXT_BATTLE_FORFEIT ||
                 (PktType)bp->type == PktType::TEXT_BATTLE_HASH) &&
                mp.from != nodeDB->getNodeNum()) {
                if (textBattleActive_) {
                    textBattle_.handlePacket(mp.from,
                                              mp.decoded.payload.bytes,
                                              mp.decoded.payload.size);
                }
                return ProcessMessage::CONTINUE;
            }

            // Dungeons and MonstersMesh — route to dungeon game engine
            if ((PktType)bp->type == PktType::DUNGEON_BEACON ||
                (PktType)bp->type == PktType::DUNGEON_JOIN   ||
                (PktType)bp->type == PktType::DUNGEON_JOIN_ACK ||
                (PktType)bp->type == PktType::DUNGEON_CMD    ||
                (PktType)bp->type == PktType::DUNGEON_STATE  ||
                (PktType)bp->type == PktType::DUNGEON_MSG    ||
                (PktType)bp->type == PktType::DUNGEON_PROMPT) {
                dungeon_.handlePacket(mp.decoded.payload.bytes,
                                      mp.decoded.payload.size);
                return ProcessMessage::CONTINUE;
            }

            if ((PktType)bp->type == PktType::BBS_REPLY) {
                size_t   plen = mp.decoded.payload.size - BATTLELINK_HDR_SIZE;
                const uint8_t *p = bp->payload;
                size_t pos = 0;
                auto pull = [&](char *out, size_t outCap) -> bool {
                    if (pos >= plen) return false;
                    uint8_t len = p[pos++];
                    if (pos + len > plen) return false;
                    size_t cp = (len + 1 < outCap) ? len : outCap - 1;
                    memcpy(out, p + pos, cp);
                    out[cp] = '\0';
                    pos += len;
                    return true;
                };
                char gym[24], badge[24], leader[16];
                if (pull(gym, sizeof(gym)) && pull(badge, sizeof(badge)) &&
                    pull(leader, sizeof(leader)) && pos < plen) {
                    uint8_t roster = p[pos];
                    terminal_.onBbsReply(mp.from, gym, badge, leader, roster);
                }
                return ProcessMessage::CONTINUE;
            }

            // ── Bulk-ladder reply: NAMES (5 trainer name strings) ────────
            if ((PktType)bp->type == PktType::BBS_LADDER_NAMES &&
                bbsLadderBulkActive_ && mp.from == bbsFightTarget_) {
                size_t plen = mp.decoded.payload.size - BATTLELINK_HDR_SIZE;
                const uint8_t *p = bp->payload;
                size_t pos = 0;
                if (pos >= plen) return ProcessMessage::CONTINUE;
                uint8_t tc = p[pos++];
                if (tc > 5) tc = 5;
                memset(bbsLadderNames_, 0, sizeof(bbsLadderNames_));
                bool ok = true;
                for (uint8_t i = 0; i < tc; ++i) {
                    if (pos >= plen) { ok = false; break; }
                    uint8_t nl = p[pos++];
                    if (pos + nl > plen) { ok = false; break; }
                    uint8_t copy = (nl < 16) ? nl : 16;
                    memcpy(bbsLadderNames_[i], p + pos, copy);
                    bbsLadderNames_[i][copy] = '\0';
                    pos += nl;
                }
                if (ok) {
                    bbsLadderHaveNames_ = true;
                    LOG_INFO("[MonsterMesh] MMG bulk: NAMES rx (%u trainers)\n",
                             (unsigned)tc);
                    if (bbsLadderHaveParties_) bbsLadderStartPending_ = true;
                }
                return ProcessMessage::CONTINUE;
            }

            // ── Bulk-ladder reply: PARTIES (5 minimal mon-lists) ─────────
            if ((PktType)bp->type == PktType::BBS_LADDER_PARTIES &&
                bbsLadderBulkActive_ && mp.from == bbsFightTarget_) {
                size_t plen = mp.decoded.payload.size - BATTLELINK_HDR_SIZE;
                const uint8_t *p = bp->payload;
                size_t pos = 0;
                if (pos >= plen) return ProcessMessage::CONTINUE;
                uint8_t tc = p[pos++];
                if (tc > 5) tc = 5;
                memset(bbsLadderParties_, 0, sizeof(bbsLadderParties_));
                bool ok = true;
                for (uint8_t t = 0; t < tc && ok; ++t) {
                    if (pos >= plen) { ok = false; break; }
                    uint8_t mc = p[pos++];
                    if (mc > 6) { ok = false; break; }
                    Gen1Party &gp = bbsLadderParties_[t];
                    gp.count = mc;
                    for (uint8_t m = 0; m < mc; ++m) {
                        if (pos + 6 > plen) { ok = false; break; }
                        uint8_t dex   = p[pos++];
                        uint8_t level = p[pos++];
                        uint8_t mv0   = p[pos++];
                        uint8_t mv1   = p[pos++];
                        uint8_t mv2   = p[pos++];
                        uint8_t mv3   = p[pos++];
                        Gen1MinimalStats s = gen1MinimalStats(dex, level);
                        uint8_t internal   = gen1DexToInternal(dex);
                        Gen1Pokemon &pk    = gp.mons[m];
                        memset(&pk, 0, sizeof(pk));
                        pk.species  = internal;
                        pk.boxLevel = level;
                        pk.level    = level;
                        auto setBe16 = [](uint8_t *dst, uint16_t v) {
                            dst[0] = (uint8_t)(v >> 8); dst[1] = (uint8_t)v;
                        };
                        setBe16(pk.maxHp, s.hp);
                        setBe16(pk.hp,    s.hp);
                        setBe16(pk.atk,   s.atk);
                        setBe16(pk.def,   s.def);
                        setBe16(pk.spd,   s.spd);
                        setBe16(pk.spc,   s.spc);
                        pk.type1 = s.type1;
                        pk.type2 = s.type2;
                        pk.dvs[0] = 0x88;
                        pk.dvs[1] = 0x88;
                        pk.moves[0] = mv0;
                        pk.moves[1] = mv1;
                        pk.moves[2] = mv2;
                        pk.moves[3] = mv3;
                        for (uint8_t s2 = 0; s2 < 4; ++s2) {
                            const Gen1MoveData *mv = gen1Move(pk.moves[s2]);
                            pk.pp[s2] = mv ? mv->pp : (pk.moves[s2] ? 25 : 0);
                        }
                        gp.species[m] = internal;
                        const char *nm = gen1SpeciesName(internal);
                        snprintf((char *)gp.nicknames[m], 11, "%s", nm ? nm : "MON");
                    }
                }
                if (ok) {
                    bbsLadderHaveParties_ = true;
                    LOG_INFO("[MonsterMesh] MMG bulk: PARTIES rx (%u trainers)\n",
                             (unsigned)tc);
                    if (bbsLadderHaveNames_) bbsLadderStartPending_ = true;
                }
                return ProcessMessage::CONTINUE;
            }

            // BBS gym fight reply — TEXT_BATTLE_PARTY chunks of the gym's
            // Gen1Party. Reassemble; once complete, kick off a local battle.
            if ((PktType)bp->type == PktType::TEXT_BATTLE_PARTY &&
                bbsFightAwaitParty_ && mp.from == bbsFightTarget_) {
                if (mp.decoded.payload.size < BATTLELINK_HDR_SIZE + 2)
                    return ProcessMessage::CONTINUE;
                uint8_t partIdx   = bp->payload[0];
                uint8_t partTotal = bp->payload[1];
                size_t  dataLen   = mp.decoded.payload.size -
                                    BATTLELINK_HDR_SIZE - 2;
                const size_t CHUNK = BATTLELINK_MAX_PAYLOAD - 2;
                if (partTotal == 0 || partTotal > 8) return ProcessMessage::CONTINUE;
                if (partIdx >= partTotal)            return ProcessMessage::CONTINUE;
                size_t off = (size_t)partIdx * CHUNK;
                if (off + dataLen > sizeof(bbsPartyChunks_))
                    return ProcessMessage::CONTINUE;
                memcpy(bbsPartyChunks_ + off, bp->payload + 2, dataLen);
                bbsPartyTotal_     = partTotal;
                bbsPartyChunkMask_ |= (uint8_t)(1u << partIdx);

                uint8_t fullMask = (uint8_t)((1u << partTotal) - 1u);
                if ((bbsPartyChunkMask_ & fullMask) == fullMask) {
                    // Full party received. DON'T call textBattle_.startLocal
                    // here — handleReceived runs on the LoRa router thread
                    // and the LovyanGFX screen-clear must happen on the
                    // main loop. Stage a flag; runOnce picks it up.
                    if (sizeof(bbsGymParty_) <= sizeof(bbsPartyChunks_))
                        memcpy(&bbsGymParty_, bbsPartyChunks_,
                               sizeof(bbsGymParty_));
                    bbsFightAwaitParty_      = false;
                    bbsBattleStartPending_   = true;
                    LOG_INFO("[MonsterMesh] BBS gym party received (%u chunks) "
                             "from %08X — staging local battle\n",
                             (unsigned)partTotal, (unsigned)mp.from);
                }
                return ProcessMessage::CONTINUE;
            }
        }

        // Tolerate older firmware that broadcast the beacon without the
        // trailing ngPlusTier byte. Copy into a local zero-initialized
        // struct so the missing byte reads as tier 0.
        if (mp.decoded.payload.size >= sizeof(DaycareBeacon) - 1) {
            DaycareBeacon beaconBuf;
            memset(&beaconBuf, 0, sizeof(beaconBuf));
            size_t copyLen = mp.decoded.payload.size < sizeof(beaconBuf)
                                 ? mp.decoded.payload.size
                                 : sizeof(beaconBuf);
            memcpy(&beaconBuf, mp.decoded.payload.bytes, copyLen);
            const DaycareBeacon *beacon = &beaconBuf;
            // type=0x60 is the daycare-beacon discriminator (PokemonDaycare.cpp:433).
            if (beacon->type == 0x60 && beacon->nodeId != nodeDB->getNodeNum()) {
                LOG_INFO("[MonsterMesh] daycare beacon RX from 0x%08X '%s/%s' party=%u ng+%u\n",
                         (unsigned)beacon->nodeId, beacon->shortName,
                         beacon->gameName, (unsigned)beacon->partyCount,
                         (unsigned)beacon->ngPlusTier);
                uint8_t prevCount = daycare_.getNeighborCount();
                daycare_.handleBeacon(*beacon);
                // New trainer arrived — fire an arrival event. The event is
                // safe (state mutation only, no TX). Its DM is dispatched in
                // runOnce via the lastDmedEventTime_ watermark; we never send
                // from this router-context handler.
                if (daycare_.getNeighborCount() > prevCount) {
                    daycare_.triggerArrivalEvent(*beacon);
                }
            } else {
                LOG_INFO("[MonsterMesh] PRIVATE_APP not daycare beacon (type=0x%02X self=%d)\n",
                         (unsigned)beacon->type,
                         (int)(beacon->nodeId == nodeDB->getNodeNum()));
            }
        }
    }
    // (BBS gym discovery used to parse GYM: text DMs here. The discovery
    // protocol now lives entirely on PRIVATE_APP via BBS_PING / BBS_REPLY
    // BattlePackets — handled in the PRIVATE_APP block above. Nothing to
    // do here for BBS in the text-message path.)

    // T4: parse the peer's Y/N reply to our outstanding challenge.
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
        mmtAwaitingReplyFrom_ != 0 && mp.from == mmtAwaitingReplyFrom_) {
        const char *txt = (const char *)mp.decoded.payload.bytes;
        size_t      len = mp.decoded.payload.size;
        // Skip leading whitespace; first non-space char is the verdict.
        size_t i = 0;
        while (i < len && (txt[i] == ' ' || txt[i] == '\t')) ++i;
        char first = (i < len) ? txt[i] : '\0';
        if (first == 'Y' || first == 'y') {
            pendingMmtAccepted_   = true;
            pendingMmtAcceptedTx_ = true;
            mmtAcceptedTxTarget_  = mp.from;
            mmtAwaitingReplyFrom_ = 0;
        } else if (first == 'N' || first == 'n') {
            pendingMmtDeclined_   = true;
            mmtAwaitingReplyFrom_ = 0;
        }
        // Always CONTINUE so the standard chat pipeline still delivers
        // the reply DM to the user's phone app.
        return ProcessMessage::CONTINUE;
    }
    return ProcessMessage::STOP;
}

void MonsterMeshModule::challengePeerByShortName(const char *peerShort)
{
    if (!nodeDB || !peerShort || !peerShort[0]) {
        terminal_.printLine("mmt: empty target");
        return;
    }
    size_t total = nodeDB->getNumMeshNodes();
    uint32_t resolved = 0;
    char matchedShort[12] = {};
    for (size_t i = 0; i < total; ++i) {
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || !n->has_user) continue;
        if (n->num == nodeDB->getNodeNum()) continue;     // skip self
        if (strcasecmp(n->user.short_name, peerShort) == 0) {
            resolved = n->num;
            strncpy(matchedShort, n->user.short_name, sizeof(matchedShort) - 1);
            break;
        }
    }
    char buf[80];
    if (resolved == 0) {
        snprintf(buf, sizeof(buf),
                 "mmt: no node '%s' in NodeDB. Try after they NodeInfo.",
                 peerShort);
        terminal_.printLine(buf);
        LOG_WARN("[MonsterMesh] %s\n", buf);
        return;
    }
    snprintf(buf, sizeof(buf), "Challenging %s — waiting for reply...",
             matchedShort);
    terminal_.printLine(buf);
    LOG_INFO("[MonsterMesh] mmt: challenging %s = 0x%08X\n",
             matchedShort, (unsigned)resolved);
    mmtOnTxTarget_       = resolved;
    pendingMmtOnTx_      = true;
    mmtAwaitingReplyFrom_ = resolved;
    strncpy(mmtPeerShort_, matchedShort, sizeof(mmtPeerShort_) - 1);
    mmtPeerShort_[sizeof(mmtPeerShort_) - 1] = '\0';
}

void MonsterMeshModule::sendTextDM(uint32_t to, const char *text)
{
    if (!text) return;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) return;
    p->to = to;
    p->channel = channels.getPrimaryIndex();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t len = strlen(text);
    if (len > sizeof(p->decoded.payload.bytes)) len = sizeof(p->decoded.payload.bytes);
    memcpy(p->decoded.payload.bytes, text, len);
    p->decoded.payload.size = len;
    service->sendToMesh(p);
    LOG_INFO("[MonsterMesh] sent DM to 0x%08X: %s\n", (unsigned)to, text);
}

void MonsterMeshModule::achievementsString(char *buf, size_t bufLen)
{
    if (!buf || bufLen == 0) return;
    size_t off = 0;
    #define ACH_APPEND(...) do { \
        if (off < bufLen - 1) { \
            int _w = snprintf(buf + off, bufLen - off, __VA_ARGS__); \
            if (_w > 0) off += (size_t)_w; \
            if (off >= bufLen) off = bufLen - 1; \
        } \
    } while (0)

    const auto &st = daycare_.getState();
    uint8_t earned = 0;
    for (uint8_t i = 0; i < ACH_COUNT; ++i) {
        if (st.achievementFlags & (1ULL << i)) ++earned;
    }
    ACH_APPEND("Achievements: %u/%u\n", (unsigned)earned, (unsigned)ACH_COUNT);
    if (earned == 0) {
        ACH_APPEND("(none yet -- keep playing)\n");
    } else {
        for (uint8_t i = 0; i < ACH_COUNT; ++i) {
            if (st.achievementFlags & (1ULL << i)) {
                ACH_APPEND("  %s\n", achievementDefs[i].name);
            }
        }
    }
    buf[off] = '\0';
    #undef ACH_APPEND
}

void MonsterMeshModule::daycareStatusString(char *buf, size_t bufLen)
{
    if (!buf || bufLen == 0) return;
    size_t off = 0;
    #define DC_APPEND(...) do { \
        if (off < bufLen - 1) { \
            int _w = snprintf(buf + off, bufLen - off, __VA_ARGS__); \
            if (_w > 0) off += (size_t)_w; \
            if (off >= bufLen) off = bufLen - 1; \
        } \
    } while (0)

    DC_APPEND("Daycare: %s\n", daycare_.isActive() ? "active" : "idle");

    uint8_t nc = daycare_.getNeighborCount();
    DC_APPEND("Neighbors: %u\n", (unsigned)nc);
    const auto *ns = daycare_.getNeighbors();
    for (uint8_t i = 0; i < nc && i < 6; ++i) {
        char tierLabel[8];
        if (ns[i].ngPlusTier == 0) snprintf(tierLabel, sizeof(tierLabel), "Kanto");
        else                       snprintf(tierLabel, sizeof(tierLabel), "NG+%u",
                                            (unsigned)ns[i].ngPlusTier);
        DC_APPEND("  %s/%s Lv%u %s\n",
                  ns[i].shortName[0] ? ns[i].shortName : "?",
                  ns[i].nickname[0]  ? ns[i].nickname  : "?",
                  (unsigned)ns[i].level,
                  tierLabel);
    }

    const auto &evt = daycare_.getLastEvent();
    if (evt.message[0]) {
        uint32_t ago = (millis() - daycare_.getLastEventTime()) / 1000;
        DC_APPEND("Last event %us ago:\n", (unsigned)ago);
        DC_APPEND("  %s\n", evt.message);
        if (evt.xp) DC_APPEND("  +%u xp\n", (unsigned)evt.xp);
    } else {
        DC_APPEND("No events yet.\n");
    }
    buf[off] = '\0';
    #undef DC_APPEND
}

void MonsterMeshModule::daycareCheckInFromStagedParty()
{
    // Use the most recently-staged party (loaded from SAV) to check in.
    if (!terminalPartyStaged_ && !terminal_.hasParty()) return;
    const Gen1Party &p = terminalPartyStaged_ ? terminalStagedParty_ : terminal_.getParty();
    if (p.count == 0 || p.count > 6) return;

    // Compose simple parallel arrays for the legacy checkIn signature.
    uint8_t species[6] = {};
    uint8_t levels[6]  = {};
    char    nicks[6][11] = {};
    for (uint8_t i = 0; i < p.count; ++i) {
        // Gen 1 SAV stores species as the internal hex code (0x01-0xBE), NOT
        // the pokedex number. Daycare expects dex numbers — convert via the
        // pret/pokered table in DaycareSavPatcher.h. Without this the wrong
        // species name surfaces in event DMs (e.g. Mew shown as Spearow).
        uint8_t internal = p.species[i];
        species[i] = internalToDex[internal];
        levels[i]  = p.mons[i].level;
        gen1NameToAscii(p.nicknames[i], 11, nicks[i], sizeof(nicks[i]));
    }
    const char *shortName = (owner.short_name[0] != '\0') ? owner.short_name : "MM";
    const char *gameName  = (stagedTrainerName_[0] != '\0') ? stagedTrainerName_ : nicks[0];
    daycare_.checkIn(species, levels, nicks, p.count, shortName, gameName);
    LOG_INFO("[MonsterMesh] daycare: checked in %u pokemon as %s/%s\n",
             (unsigned)p.count, shortName, gameName);
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
            dungeon_.begin();
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

        // ── Daycare ─────────────────────────────────────────────────────
        // Init only — DO NOT call forceBeacon() here; broadcasting during the
        // early-boot PacketAPI/NodeInfo window caused a ~10s reset loop. The
        // periodic beacon timer fires on its own cadence later.
        daycare_.init();
        terminal_.setDaycareStatusFn(
            [](void *ctx, char *buf, size_t n) {
                static_cast<MonsterMeshModule *>(ctx)->daycareStatusString(buf, n);
            }, this);
        terminal_.setDaycareForceEventFn(
            [](void *ctx) {
                static_cast<MonsterMeshModule *>(ctx)->daycare_.forceEvent();
            }, this);
        terminal_.setDaycareAchievementsFn(
            [](void *ctx, char *buf, size_t n) {
                static_cast<MonsterMeshModule *>(ctx)->achievementsString(buf, n);
            }, this);
        terminal_.setBeaconFn(
            [](void *ctx) {
                static_cast<MonsterMeshModule *>(ctx)->daycare_.forceBeacon();
            }, this);
        terminal_.setLoraOnFn(
            [](void *ctx) {
                // Force LoRa TX back on at every layer:
                //   1. clear MM-side radio park (g_meshSuspended etc.)
                //   2. flip the Meshtastic config flag tx_enabled = true
                //   3. persist so it survives reboot
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                self->exitEmulatorMode();
                config.lora.tx_enabled = true;
                if (nodeDB) nodeDB->saveToDisk(SEGMENT_CONFIG);
                LOG_INFO("[MonsterMesh] lora cmd: tx_enabled=true persisted\n");
            }, this);
        terminal_.setFightFn(
            [](void *ctx) {
                static_cast<MonsterMeshModule *>(ctx)->requestLocalTextBattle();
            }, this);
        terminal_.setGymFightFn(
            [](void *ctx, uint8_t gymIdx, uint8_t trainerIdx) {
                static_cast<MonsterMeshModule *>(ctx)
                    ->requestGymBattle(gymIdx, trainerIdx);
            }, this);
        terminal_.setExploreFn(
            [](void *ctx, uint8_t routeIdx) {
                static_cast<MonsterMeshModule *>(ctx)
                    ->requestExplore(routeIdx);
            }, this);
        terminal_.setE4FightFn(
            [](void *ctx, uint8_t memberIdx) {
                static_cast<MonsterMeshModule *>(ctx)
                    ->requestE4Battle(memberIdx);
            }, this);
        terminal_.setMmtChallengeFn(
            [](void *ctx, const char *peerShort) {
                static_cast<MonsterMeshModule *>(ctx)
                    ->challengePeerByShortName(peerShort);
            }, this);
        // BBS gym discovery probe — user-initiated.  Sends a single silent
        // BBS_PING BattlePacket on MonsterMesh channel 1 / PRIVATE_APP (port
        // 256). Same back channel daycare beacons use. NEVER on
        // TEXT_MESSAGE_APP. NEVER on channel 0. Gyms reply unicast with
        // BBS_REPLY (parsed in handleReceived above). Asymmetric model:
        // peer-to-peer T-Decks beacon themselves; gyms do NOT — they only
        // answer probes.  See feedback memory `feedback_no_public_beacons`.
        terminal_.setBbsProbeFn(
            [](void *ctx) {
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                if (!service || !router) return;
                meshtastic_MeshPacket *out = router->allocForSending();
                if (!out) return;
                out->to              = NODENUM_BROADCAST;
                out->channel         = MONSTERMESH_CHANNEL;
                out->want_ack        = false;
                out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
                uint8_t buf[BATTLELINK_HDR_SIZE];
                BattlePacket *pkt = (BattlePacket *)buf;
                memset(buf, 0, sizeof(buf));
                pkt->type = (uint8_t)PktType::BBS_PING;
                pkt->setSessionId(0);
                pkt->seq = 0;
                memcpy(out->decoded.payload.bytes, buf, sizeof(buf));
                out->decoded.payload.size = sizeof(buf);
                service->sendToMesh(out, RX_SRC_LOCAL, true);
                (void)self;
            }, this);
        // BBS gym fight — send-party-once model.
        //   1. Send a tiny BBS_FIGHT_REQUEST to the gym
        //   2. Gym responds with TEXT_BATTLE_PARTY chunks (its full Gen1Party)
        //   3. Once reassembled, run the battle entirely LOCAL (player vs CPU)
        //   4. On battle end, send BBS_FIGHT_RESULT back to the gym
        // Way fewer LoRa packets than per-turn lockstep, no party-mirror
        // weirdness because we have the gym's actual party.
        terminal_.setBbsFightFn(
            [](void *ctx, uint32_t gymNodeNum) {
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                if (!self->terminal_.hasParty()) {
                    self->terminal_.printLine("no party loaded — load a SAV first");
                    return;
                }
                if (self->bbsFightActive_ || self->bbsFightAwaitParty_) {
                    self->terminal_.printLine("bbs fight already in progress");
                    return;
                }
                // Bulk-ladder path: send BBS_LADDER_REQUEST once. Falls
                // back to legacy BBS_FIGHT_REQUEST after 5s if the gym
                // doesn't speak the bulk protocol (handled in runOnce).
                if (!service || !router) return;
                meshtastic_MeshPacket *out = router->allocForSending();
                if (!out) return;
                out->to              = gymNodeNum;
                out->channel         = MONSTERMESH_CHANNEL;
                out->want_ack        = false;
                out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;

                uint8_t buf[BATTLELINK_HDR_SIZE];
                BattlePacket *pkt = (BattlePacket *)buf;
                memset(buf, 0, sizeof(buf));
                pkt->type = (uint8_t)PktType::BBS_LADDER_REQUEST;
                pkt->setSessionId((uint16_t)(millis() & 0xFFFF));
                pkt->seq = 0;
                memcpy(out->decoded.payload.bytes, buf, sizeof(buf));
                out->decoded.payload.size = sizeof(buf);
                service->sendToMesh(out, RX_SRC_LOCAL, true);

                self->bbsFightTarget_     = gymNodeNum;
                self->bbsFightRequestMs_  = millis();
                self->bbsFightAwaitParty_ = true;
                self->bbsFightActive_     = false;
                self->bbsPartyChunkMask_  = 0;
                self->bbsPartyTotal_      = 0;
                // Bulk-ladder cache reset.
                self->bbsLadderTrainerIdx_     = 0;
                self->bbsLadderCount_          = 5;
                self->bbsLadderRequestPending_ = false;
                self->bbsLadderHaveNames_      = false;
                self->bbsLadderHaveParties_    = false;
                self->bbsLadderBulkActive_     = true;
                self->bbsLadderStartPending_   = false;
                self->bbsLadderRequestSentMs_  = millis();
                memset(self->bbsLadderNames_,   0, sizeof(self->bbsLadderNames_));
                memset(self->bbsLadderParties_, 0, sizeof(self->bbsLadderParties_));
                self->terminal_.printLine("Connecting to gym (bulk ladder)...");
            }, this);
        terminal_.setDungeonFn(
            [](void *ctx, const char *verb, const char *arg) {
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                self->dungeon_.handleLocalCommand(verb, arg);
                if (self->dungeon_.isActive() && !self->dungeonActive_) {
                    self->dungeonActive_ = true;
                    self->dungeonOverlay_.open();
                }
            }, this);
        daycare_.setSendDm([](uint32_t dest, const char *msg, void *ctx) {
            auto *self = static_cast<MonsterMeshModule *>(ctx);
            self->sendTextDM(dest, msg);
        }, this);
        daycare_.setBroadcast([](const char *msg, void *ctx) {
            auto *self = static_cast<MonsterMeshModule *>(ctx);
            // Achievement / daycare announcements ride MM channel 1 (the
            // MonsterMesh chat channel) so other MM users see them in
            // their feed without polluting the public LongFast channel.
            (void)self;
            if (!msg) return;
            meshtastic_MeshPacket *p = router->allocForSending();
            if (!p) return;
            p->to              = NODENUM_BROADCAST;
            p->channel         = MONSTERMESH_CHANNEL;
            p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            size_t len = strlen(msg);
            if (len > sizeof(p->decoded.payload.bytes)) len = sizeof(p->decoded.payload.bytes);
            memcpy(p->decoded.payload.bytes, msg, len);
            p->decoded.payload.size = len;
            service->sendToMesh(p);
            LOG_INFO("[MonsterMesh] mm-ch broadcast: %s\n", msg);
        }, this);
        daycare_.setSendBeacon([](const DaycareBeacon &beaconIn, void *ctx) {
            (void)ctx;
            DaycareBeacon beacon = beaconIn;
            beacon.nodeId     = nodeDB->getNodeNum();
            beacon.ngPlusTier = lordCurrentNgPlusTier();
            meshtastic_MeshPacket *p = router->allocForSending();
            if (!p) {
                LOG_WARN("[MonsterMesh] daycare beacon: packet alloc failed\n");
                return;
            }
            p->to = NODENUM_BROADCAST;
            p->channel = channels.getPrimaryIndex();
            p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
            size_t sz = sizeof(DaycareBeacon);
            if (sz > sizeof(p->decoded.payload.bytes)) sz = sizeof(p->decoded.payload.bytes);
            memcpy(p->decoded.payload.bytes, &beacon, sz);
            p->decoded.payload.size = sz;
            service->sendToMesh(p);
            LOG_INFO("[MonsterMesh] daycare beacon TX: ch=%u sz=%u party=%u name='%s/%s'\n",
                     (unsigned)p->channel, (unsigned)sz, (unsigned)beacon.partyCount,
                     beacon.shortName, beacon.gameName);
        }, this);

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

            // Ignore garbage reads (I2C bus error returns 0xFF on every byte,
            // or NACK gives got<5). Only trust valid frames.
            bool valid = (st1 == 0 && got == 5 && b[0] != 0xFF);
            if (valid) {
                bool altNow = (b[0] & 0x10) != 0;
                static bool altSeenLow = false;  // require a clean low baseline before firing
                if (!altNow) altSeenLow = true;
                if (altNow && !altWas && altSeenLow && (now - g_lastAltFireMs > 1000)) {
                    g_lastAltFireMs = now;
                    LOG_INFO("[MonsterMesh] ALT pressed (runOnce poll) → toggle\n");
                    handleKeyPress(0x05);
                }
                altWas = altNow;
            }
        }
    }

    // Keep PowerFSM awake while emulator or browser is active. Throttle —
    // every runOnce was logging "State: ON" and burying everything else.
    static uint32_t lastWakeMs = 0;
    if ((emulatorActive_ || browserActive_) && (millis() - lastWakeMs > 5000)) {
        lastWakeMs = millis();
        powerFSM.trigger(EVENT_INPUT);
    }

#if HAS_TFT
    // Sync emulator palette to active theme. DMG/GBC/Pocket use their own
    // 4 base shades; Pokemon Red/Blue use Pocket; Dark/Light keep the stock
    // yellow-green DMG palette so the emulator looks like a real Game Boy.
    static int lastThemeIdx = -1;
    int curIdx = (int)Themes::get();
    if (curIdx != lastThemeIdx) {
        lastThemeIdx = curIdx;
        auto rgb565 = [](uint32_t aarrggbb) -> uint16_t {
            uint8_t r = (aarrggbb >> 16) & 0xFF, g = (aarrggbb >> 8) & 0xFF, b = aarrggbb & 0xFF;
            return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        };
        // Each Game-Boy palette: lightest (bg) → darkest (ink). Bases match
        // Themes.cpp shade tables exactly so the emulator and UI feel cohesive.
        struct Pal { uint32_t lightest, light, dark, darkest; };
        const Pal dmg    = { 0xff9BBC0F, 0xff8BAC0F, 0xff306230, 0xff0F380F };
        const Pal gbc    = { 0xffE0F8D0, 0xff88C070, 0xff346856, 0xff081820 };
        const Pal pocket = { 0xffC4E878, 0xff88D048, 0xff306230, 0xff0F380F };
        const Pal red    = { 0xffFFE0E0, 0xffFF9090, 0xffB81818, 0xff300000 };
        const Pal blue   = { 0xffE0E8FF, 0xff90B0F0, 0xff1838A0, 0xff000820 };
        const Pal stock  = { 0xffFFFFFF, 0xffAAAAAA, 0xff555555, 0xff000000 }; // dark/light fallback
        Pal p;
        switch (Themes::get()) {
            case Themes::eDmgGreen:    p = dmg;    break;
            case Themes::eGbcGreen:    p = gbc;    break;
            case Themes::ePocketGreen: p = pocket; break;
            // Red/Blue keep the rest of the UI in their cartridge tint, but
            // the emulator looks best in classic GBC shades (red/blue would
            // make grass + water unreadable).
            case Themes::ePokemonRed:
            case Themes::ePokemonBlue: p = gbc;    break;
            default:                   p = stock;  break;  // Dark/Light → stock GB
        }
        setEmulatorPalette(rgb565(p.lightest), rgb565(p.light), rgb565(p.dark), rgb565(p.darkest));
    }
#endif

    // Heartbeat: every 10s, log free heap + thread state so we can see if
    // and when the LoRa thread stops ticking. Emu-mode soak crashes look
    // like runOnce going silent at some point — this tells us when.
    static uint32_t lastHeartbeatMs = 0;
    if ((emulatorActive_ || browserActive_) && (millis() - lastHeartbeatMs > 10000)) {
        lastHeartbeatMs = millis();
        LOG_INFO("[MM heartbeat] free=%u psram=%u largest=%u up=%u",
                 (unsigned)ESP.getFreeHeap(),
                 (unsigned)ESP.getFreePsram(),
                 (unsigned)ESP.getMaxAllocHeap(),
                 (unsigned)millis());
    }
    // One-shot probe so we can see boot-time state on serial
    static bool probeLogged = false;
    if (!probeLogged && setupDone_) {
        probeLogged = true;
        LOG_INFO("[MonsterMesh] boot complete — emu=%d browser=%d\n",
                 (int)emulatorActive_, (int)browserActive_);
    }

    // Radio + WiFi state sync on the LoRa thread. LVGL thread only flips
    // radioParked_/radioNeedsRx_; we reconcile here so LVGL stays snappy.
    if (radioNeedsRx_) {
        radioNeedsRx_ = false;
        if (RadioLibInterface::instance) {
            LOG_INFO("[MonsterMesh] sync: re-arming LoRa RX\n");
            RadioLibInterface::instance->startReceive();
        }
    }
    if (radioParked_ && wifiBooted_) {
        LOG_INFO("[MonsterMesh] sync: tearing WiFi down\n");
        ::deinitWifi();
        wifiBooted_ = false;
    } else if (!radioParked_ && !wifiBooted_ && millis() > 30000) {
        wifiBooted_ = true;
        LOG_INFO("[MonsterMesh] sync: bringing WiFi up\n");
        wifiSuppressed = false;
        needReconnect = true;
        ::initWifi();
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
        // Deferred SD scan — LVGL thread sets browserNeedsScan_ on entry.
        // We do the scan here on the LoRa thread so the LVGL thread doesn't
        // race with renderBrowser for spiLock and end up wedged.
        if (browserNeedsScan_) {
            browserNeedsScan_ = false;
            concurrency::LockGuard g(spiLock);
            browser_.open("/");
        }
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
            LOG_DEBUG("[MonsterMesh] browser key=0x%02X cursor=%d count=%d eject=%d\n",
                      key, browser_.cursor(), browser_.count(), (int)ejectFocused_);
            // [Eject Cart] virtual row at top of browser, only when a ROM is loaded.
            if (emuInitialized_) {
                if ((key == 'w' || key == 'W') && browser_.cursor() == 0 && !ejectFocused_) {
                    ejectFocused_ = true;
                    browser_.markDirty();
                    renderBrowser();
                    return 100;
                }
                if (ejectFocused_) {
                    if (key == 's' || key == 'S') {
                        ejectFocused_ = false;
                        browser_.markDirty();
                        renderBrowser();
                        return 100;
                    }
                    if (key == 'k' || key == 'K' || key == '\r' || key == '\n') {
                        ejectFocused_ = false;
                        clearCart();
                        renderBrowser();
                        return 100;
                    }
                }
            }
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

    // Battle-XP write-back to /<rom>.sav. Gated on:
    //   - !emulatorActive_  (no emu owning the SD bus / WRAM mirror)
    //   - !emu_.isRunning() (no cart actually loaded — even if the user is
    //     in browser/terminal, a running emu still has WRAM authority)
    //   - terminal has a party  (something to write)
    //   - we know the SAV path
    // Drops the pending flag silently if any gate fails so the next battle
    // re-arms it; the SAV stays unchanged in that case.
    // T4: drain pending MMT TX. handleReceived stages a flag; runOnce
    // sends the actual DM here so we never call router->allocForSending
    // from the router context (per feedback_mm_defer_tx_from_router.md).
    // The body is human-readable so the recipient sees a normal DM in
    // their Meshtastic app — the "MMT:ON" prefix is just our parser key.
    if (pendingMmtOnTx_) {
        pendingMmtOnTx_ = false;
        if (mmtOnTxTarget_) {
            // Plain Meshtastic DM — recipient sees this in their phone app
            // chat. No internal MMT: state machine; the receiver just
            // reads + replies normally. Battle-start trigger lands later.
            sendTextDM(mmtOnTxTarget_,
                       "Do you want to battle in MonsterMesh? Reply Y or N.");
            LOG_INFO("[MonsterMesh] mmt challenge DM → 0x%08X\n",
                     (unsigned)mmtOnTxTarget_);
        }
    }
    // T4 reply drain: peer's Y/N to our outstanding challenge.
    if (pendingMmtAccepted_) {
        pendingMmtAccepted_ = false;
        char buf[80];
        snprintf(buf, sizeof(buf),
                 "%s accepted! Starting battle...",
                 mmtPeerShort_[0] ? mmtPeerShort_ : "Peer");
        terminal_.printLine(buf);
        LOG_INFO("[MonsterMesh] mmt accept from %s — kicking off PvP\n",
                 mmtPeerShort_[0] ? mmtPeerShort_ : "(?)");
        // T4 phase 3: stage the actual battle launch. runOnce drains it on
        // the main loop, where it's safe to swap LVGL's flush_cb and clear
        // the LGFX framebuffer.
        if (mmtAcceptedTxTarget_ && terminal_.hasParty()) {
            mmtBattlePeer_ = mmtAcceptedTxTarget_;
            pendingMmtBattleAsInitiator_ = true;
        }
    }
    if (pendingMmtDeclined_) {
        pendingMmtDeclined_ = false;
        char buf[80];
        snprintf(buf, sizeof(buf), "%s fled.",
                 mmtPeerShort_[0] ? mmtPeerShort_ : "Peer");
        terminal_.printLine(buf);
        LOG_INFO("[MonsterMesh] mmt decline from %s\n",
                 mmtPeerShort_[0] ? mmtPeerShort_ : "(?)");
    }
    if (pendingMmtAcceptedTx_) {
        pendingMmtAcceptedTx_ = false;
        if (mmtAcceptedTxTarget_) {
            sendTextDM(mmtAcceptedTxTarget_,
                       "Battle on! Open the MonsterMesh terminal.");
        }
    }

    // Event-driven daycare-XP flush to .sav. Daycare bumps its
    // lastEventTime each time runEventCycle fires (the only path that can
    // change totalXpGained). When that timestamp moves past our high-
    // watermark, sync once. SD only spins up on real XP change.
    {
        bool cartLoaded = emuInitialized_ || emu_.isRunning();
        uint32_t evtTime = daycare_.getLastEventTime();
        if (!emulatorActive_ && !browserActive_ && !cartLoaded &&
            daycare_.isActive() && loadedSavPath_[0] &&
            evtTime != 0 && evtTime != lastSavSyncedEventTime_) {
            lastSavSyncedEventTime_ = evtTime;
            const char *sdRel = loadedSavPath_;
            if (strncmp(sdRel, "/sd", 3) == 0) sdRel += 3;
            concurrency::LockGuard g(spiLock);
            if (patchSdSavPathWithDaycareXp(sdRel, daycare_)) {
                LOG_INFO("[MonsterMesh] daycare→sav flush: '%s'\n", sdRel);
            }
        }
    }

    if (pendingSavWriteBack_) {
        pendingSavWriteBack_ = false;
        // Cart-loaded gate matches the daycare patcher: emu_.running_ stays
        // true once a cart starts and never resets, so we OR with
        // emuInitialized_ which DOES go false on Eject Cart. Both must be
        // false (= cart not in memory) before we touch the SAV — otherwise
        // the emu's next writeSaveFile would clobber our XP patch.
        bool cartLoaded = emuInitialized_ || emu_.isRunning();
        if (!emulatorActive_ && !browserActive_ &&
            !cartLoaded && terminal_.hasParty() &&
            loadedSavPath_[0]) {
            const Gen1Party &p = terminal_.getParty();
            concurrency::LockGuard g(spiLock);
            (void)writePartyToSavOnSd(loadedSavPath_, p);
        } else {
            LOG_INFO("[MonsterMesh] sav writeback: skipped (emu=%d br=%d cart=%d hasParty=%d path=%d)\n",
                     (int)emulatorActive_, (int)browserActive_,
                     (int)cartLoaded, (int)terminal_.hasParty(),
                     (int)(loadedSavPath_[0] != 0));
        }
    }

    // Deferred terminal party load — runs on the LoRa thread so the LVGL
    // thread can paint the terminal panel immediately without waiting on
    // SD reinit + directory scan.
    if (terminalNeedsParty_) {
        terminalNeedsParty_ = false;
        Gen1Party p = {};
        bool loaded = false;
        char resolvedSav[256] = {};
        char trainerName[8] = {};
        {
            concurrency::LockGuard g(spiLock);
            loaded = loadPartyFromSavOnSd(emu_.romPath(),
                                          p, resolvedSav, sizeof(resolvedSav),
                                          trainerName, sizeof(trainerName));
        }
        if (!loaded && emu_.isRunning()) {
            p.count = emu_.readWRAM(Gen1::wPartyCount);
            if (p.count > 6) p.count = 6;
            emu_.readWRAMRange(Gen1::wPartySpecies,  p.species,                7);
            emu_.readWRAMRange(Gen1::wPartyMons,     (uint8_t *)p.mons,        sizeof(p.mons));
            emu_.readWRAMRange(Gen1::wPartyMonOT,    (uint8_t *)p.otNames,     sizeof(p.otNames));
            emu_.readWRAMRange(Gen1::wPartyMonNicks, (uint8_t *)p.nicknames,   sizeof(p.nicknames));
            loaded = true;
        }
        if (loaded) {
            terminalStagedParty_ = p;
            // Remember where the SAV came from so battle XP can be written
            // back after a fight (gated on !emulatorActive_ in runOnce).
            strncpy(loadedSavPath_, resolvedSav, sizeof(loadedSavPath_) - 1);
            loadedSavPath_[sizeof(loadedSavPath_) - 1] = '\0';
            // Stash the decoded trainer name so the daycare check-in can use
            // it instead of falling back to party[0]'s nickname.
            strncpy(stagedTrainerName_, trainerName, sizeof(stagedTrainerName_) - 1);
            stagedTrainerName_[sizeof(stagedTrainerName_) - 1] = '\0';
            terminalPartyStaged_ = true;  // LVGL thread will pick this up
            // First successful SAV load doubles as daycare check-in: the
            // background beacons advertise this party to the mesh. The very
            // first beacon TX is deferred to the runOnce 30s gate above.
            daycareCheckInFromStagedParty();
        }
        LOG_INFO("[MonsterMesh] terminal party load: loaded=%d count=%d sav='%s' rom='%s'\n",
                 (int)loaded, (int)p.count,
                 resolvedSav[0] ? resolvedSav : "(none)",
                 emu_.romPath()[0] ? emu_.romPath() : "(none)");
    }

    // Auto-load the party as soon as SD is mounted. SAV reading is just an
    // SD read — no LoRa traffic — so it's safe before the PacketAPI/NodeInfo
    // window settles. The first beacon TX is deferred separately below.
    if (setupDone_ && !autoPartyLoadDone_ && !terminalNeedsParty_ &&
        !terminal_.hasParty() &&
        !emulatorActive_ && !browserActive_) {
        autoPartyLoadDone_ = true;
        terminalNeedsParty_ = true;
        LOG_INFO("[MonsterMesh] auto party load triggered (SD ready)\n");
    }

    // Auto-checkOut on emulator/browser entry — flush daycare-earned XP back
    // to the .sav on SD (additive only, never reduces) so the next time the
    // user plays the cart, the gains are already in the file. The patch
    // touches only EXP / level / 5 stats per party slot + the checksum.
    //
    // **Safety gate**: never write the .sav if a cart is currently loaded.
    // The emulator owns cartRam_ at that point and would overwrite our patch
    // when it next saves on its own. Only write when no ROM has been
    // launched this session, OR the cart has been ejected (emuInitialized_
    // false). Otherwise, just stop daycare cleanly.
    if (pendingAutoCheckOut_) {
        pendingAutoCheckOut_ = false;
        if (daycare_.isActive()) {
            const char *romPath = emu_.romPath();
            bool cartLoaded = emuInitialized_ || emu_.isRunning();
            bool patched = false;
            if (!cartLoaded && romPath && romPath[0]) {
                concurrency::LockGuard g(spiLock);
                patched = patchSavOnSdWithDaycareXp(romPath, daycare_);
            }
            if (!patched) {
                // Cart loaded, no ROM path, or write failed — stop daycare
                // cleanly without touching the SAV.
                daycare_.checkOut(nullptr);
            }
            LOG_INFO("[MonsterMesh] daycare auto checked-out (cart=%d patched=%d)\n",
                     (int)cartLoaded, (int)patched);
        }
    }

    // Auto-checkIn on emulator/browser exit — reload SAV and re-checkin so
    // beacons reflect any XP/level changes earned during the play session.
    if (pendingAutoCheckin_ && setupDone_ && !emulatorActive_ && !browserActive_) {
        pendingAutoCheckin_ = false;
        if (!daycare_.isActive()) {
            terminalNeedsParty_ = true;  // forces fresh SAV read + checkIn
            LOG_INFO("[MonsterMesh] daycare auto check-in queued (emulator exit)\n");
        }
    }

    // First beacon broadcast — deferred to ~30s post-boot so the LoRa TX
    // doesn't race the PacketAPI/NodeInfo init window
    // (feedback_mm_no_boot_beacon). After this, the daycare's periodic
    // BEACON_INTERVAL_MS timer takes over (every 15 min).
    if (setupDone_ && !firstBeaconDone_ && daycare_.isActive() &&
        !emulatorActive_ && !browserActive_ && millis() > 30000) {
        firstBeaconDone_ = true;
        daycare_.forceBeacon();
        LOG_INFO("[MonsterMesh] first daycare beacon fired (30s gate)\n");
    }

    // Daycare tick — only run while in the Meshtastic UI. The radio is asleep
    // in emu/browser mode, so beacons would just queue up uselessly.
    if (setupDone_ && !emulatorActive_ && !browserActive_ && daycare_.isActive()) {
        daycare_.tick(millis());
    }

    // ── T4 phase 3: live PvP battle launch (sender-side only) ────────────
    // Receiver-side auto-launch was dropping users into spurious battles
    // when other-agent gauntlet/dungeon code emitted stray
    // TEXT_BATTLE_START packets. Drop any pending-receiver flag here so it
    // never fires the launch path.
    if (pendingMmtBattleAsReceiver_) {
        pendingMmtBattleAsReceiver_ = false;
        LOG_INFO("[MonsterMesh] PvP: ignoring incoming TEXT_BATTLE_START (auto-receiver disabled)\n");
    }

    if (pendingMmtBattleAsInitiator_ &&
        !textBattleActive_ && setupDone_ && !emulatorActive_ &&
        !browserActive_ && terminalActive_ && terminal_.hasParty()) {
        bool asInitiator = pendingMmtBattleAsInitiator_;
        pendingMmtBattleAsInitiator_ = false;
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && !savedFlushCb_) {
            savedFlushCb_ = (void *)disp->flush_cb;
            lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                lv_display_flush_ready(d);
            });
        }
#endif
        if (g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            g_deviceUiLgfx->clearClipRect();
            g_deviceUiLgfx->fillScreen(0x0000);
        }
        textBattleActive_ = true;
        if (asInitiator) {
            textBattle_.startNetworkedAsInitiator(mmtBattlePeer_,
                                                   terminal_.getParty());
            char hdr[40];
            snprintf(hdr, sizeof(hdr), "MMT vs %.4s",
                     mmtPeerShort_[0] ? mmtPeerShort_ : "Peer");
            textBattle_.setHeader(hdr);
            LOG_INFO("[MonsterMesh] PvP: started as initiator vs 0x%08X\n",
                     (unsigned)mmtBattlePeer_);
        } else {
            textBattle_.startNetworkedAsReceiver(mmtBattlePeer_,
                                                  terminal_.getParty(),
                                                  mmtBattleSeed_);
            char hdr[40];
            snprintf(hdr, sizeof(hdr), "MMT incoming");
            textBattle_.setHeader(hdr);
            LOG_INFO("[MonsterMesh] PvP: started as receiver from 0x%08X seed=0x%08X\n",
                     (unsigned)mmtBattlePeer_, (unsigned)mmtBattleSeed_);
        }
    }

    // ── MMG bulk ladder: kick off battle 0 once both reply packets in ─────
    if (bbsLadderStartPending_ && !textBattleActive_ && setupDone_ &&
        !emulatorActive_ && !browserActive_ && terminal_.hasParty()) {
        bbsLadderStartPending_ = false;
        bbsLadderTrainerIdx_ = 0;
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && !savedFlushCb_) {
            savedFlushCb_ = (void *)disp->flush_cb;
            lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                lv_display_flush_ready(d);
            });
        }
#endif
        if (g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            g_deviceUiLgfx->clearClipRect();
            g_deviceUiLgfx->fillScreen(0x0000);
        }
        textBattleActive_ = true;
        bbsFightActive_   = true;
        textBattle_.startLocal(terminal_.getParty(),
                                bbsLadderParties_[0],
                                "YOU",
                                bbsLadderNames_[0][0] ? bbsLadderNames_[0] : "GYM");
        char hdr[40];
        snprintf(hdr, sizeof(hdr), "MM Gym - %.16s 1/5",
                 bbsLadderNames_[0][0] ? bbsLadderNames_[0] : "Trainer");
        textBattle_.setHeader(hdr);
        LOG_INFO("[MonsterMesh] MMG bulk ladder started (trainer 0/5)\n");
    }

    // ── MMG gym ladder: send next REQUEST after a win ─────────────────────
    if (bbsLadderRequestPending_ && bbsFightTarget_ && service && router) {
        bbsLadderRequestPending_ = false;
        meshtastic_MeshPacket *out = router->allocForSending();
        if (out) {
            out->to              = bbsFightTarget_;
            out->channel         = MONSTERMESH_CHANNEL;
            out->want_ack        = false;
            out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
            uint8_t buf[BATTLELINK_HDR_SIZE];
            BattlePacket *pkt = (BattlePacket *)buf;
            memset(buf, 0, sizeof(buf));
            pkt->type = (uint8_t)PktType::BBS_FIGHT_REQUEST;
            pkt->setSessionId((uint16_t)(millis() & 0xFFFF));
            pkt->seq = 0;
            memcpy(out->decoded.payload.bytes, buf, sizeof(buf));
            out->decoded.payload.size = sizeof(buf);
            service->sendToMesh(out, RX_SRC_LOCAL, true);
            bbsFightAwaitParty_ = true;
            LOG_INFO("[MonsterMesh] MMG ladder: requesting trainer %u/%u\n",
                     (unsigned)bbsLadderTrainerIdx_ + 1,
                     (unsigned)bbsLadderCount_);
        }
    }

    // ── MMG ladder continuation — chunks complete with battle still up ────
    // Distinct from the fresh-start path: here the textBattle is already
    // active from the previous trainer; just swap opponents.
    if (bbsBattleStartPending_ && textBattleActive_ &&
        bbsLadderTrainerIdx_ != 0xFF && bbsLadderTrainerIdx_ > 0) {
        bbsBattleStartPending_ = false;
        textBattle_.nextOpponent(bbsGymParty_, "GYM");
        char hdr[40];
        snprintf(hdr, sizeof(hdr), "MM Gym - trainer %u/%u",
                 (unsigned)bbsLadderTrainerIdx_ + 1,
                 (unsigned)bbsLadderCount_);
        textBattle_.setHeader(hdr);
        bbsFightActive_ = true;
        LOG_INFO("[MonsterMesh] MMG ladder: trainer %u/%u in the ring\n",
                 (unsigned)bbsLadderTrainerIdx_ + 1,
                 (unsigned)bbsLadderCount_);
    }

    // ── BBS gym fight start (Phase C-2) ───────────────────────────────────
    // The PRIVATE_APP TEXT_BATTLE_PARTY chunk handler stages a flag once the
    // gym's full Gen1Party has been reassembled. Run the screen-clear +
    // startLocal here on the main loop (LovyanGFX must be touched from this
    // thread, same as the regular fight path below).
    if (bbsBattleStartPending_ && !textBattleActive_ && setupDone_ &&
        !emulatorActive_ && !browserActive_ && terminal_.hasParty()) {
        bbsBattleStartPending_ = false;
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && !savedFlushCb_) {
            savedFlushCb_ = (void *)disp->flush_cb;
            lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                lv_display_flush_ready(d);
            });
        }
#endif
        if (g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            g_deviceUiLgfx->clearClipRect();
            g_deviceUiLgfx->fillScreen(0x0000);
        }
        textBattleActive_ = true;
        bbsFightActive_   = true;
        textBattle_.startLocal(terminal_.getParty(), bbsGymParty_,
                                "YOU", "GYM");
        if (bbsLadderTrainerIdx_ != 0xFF) {
            char hdr[40];
            snprintf(hdr, sizeof(hdr), "MM Gym - trainer %u/%u",
                     (unsigned)bbsLadderTrainerIdx_ + 1,
                     (unsigned)bbsLadderCount_);
            textBattle_.setHeader(hdr);
        } else {
            textBattle_.setHeader("MM Gym Battle");
        }
        LOG_INFO("[MonsterMesh] BBS local battle started vs gym 0x%08X\n",
                 (unsigned)bbsFightTarget_);
    }

    // ── T2: text battle ────────────────────────────────────────────────────
    // Start: the terminal `fight` command set the request flag. Suppress the
    // LVGL flush the same way the emulator path does, clear the screen, and
    // hand the staged party + a CPU mirror-match to startLocal().
    if (textBattleStartReq_ && !textBattleActive_ && setupDone_ &&
        !emulatorActive_ && !browserActive_ && terminal_.hasParty()) {
        textBattleStartReq_ = false;
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && !savedFlushCb_) {
            savedFlushCb_ = (void *)disp->flush_cb;
            lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                lv_display_flush_ready(d);
            });
        }
#endif
        if (g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            g_deviceUiLgfx->clearClipRect();
            g_deviceUiLgfx->fillScreen(0x0000);
        }
        // CPU rival: pick a real trainer we've seen on the mesh via
        // daycare beacons. Build their Gen1Party from the beacon's species
        // + level + nickname + moves. Daycare beacons carry pokedex numbers
        // (post internalToDex conversion at check-in), so we reverse-map
        // back to internal hex codes here — the battle engine's
        // initBattlePokeFromSave will then convert internal → dex via the
        // same table we used at SAV load, keeping both code paths
        // consistent.
        Gen1Party cpuParty = {};
        char rivalTag[6] = "RIVAL";
        char hdrText[40] = {};   // applied AFTER startLocal (which clears it)

        // E4: Indigo Plateau gauntlet (4 Elite Four + Champion). Build the
        // requested member's party. Chain to next member is handled in the
        // post-battle dispatch below.
        if (e4MemberIdx_ < 5) {
            if (lordBuildE4Party(e4MemberIdx_, cpuParty)) {
                const LordE4Member *m = lordE4Member(e4MemberIdx_);
                const char *tn = m ? m->name : "?";
                snprintf(rivalTag, sizeof(rivalTag), "%.4s", tn);
                snprintf(hdrText, sizeof(hdrText), "Indigo Plateau - %s %u/5",
                         tn, (unsigned)e4MemberIdx_ + 1);
                activeE4Member_ = e4MemberIdx_;
                LOG_INFO("[MonsterMesh] e4 %u (%s): %u pokemon\n",
                         (unsigned)e4MemberIdx_, tn, (unsigned)cpuParty.count);
            } else {
                LOG_WARN("[MonsterMesh] e4: bad member %u\n", (unsigned)e4MemberIdx_);
                e4MemberIdx_ = 0xFF;
            }
        }
        // L4: explore battles bypass everything else — wild encounter from
        // the route pool keyed by gym progress. Single mon, no chain.
        if (e4MemberIdx_ >= 5 && exploreRouteIdx_ < 8) {
            if (lordPickWildEncounter(exploreRouteIdx_, cpuParty) &&
                cpuParty.count > 0) {
                snprintf(rivalTag, sizeof(rivalTag), "WILD");
                const LordRoute *r = lordRoute(exploreRouteIdx_);
                snprintf(hdrText, sizeof(hdrText), "%s — wild encounter",
                         r ? r->name : "Route");
                activeExploreRoute_ = exploreRouteIdx_;
                activeExploreLevel_ = cpuParty.mons[0].level;
                LOG_INFO("[MonsterMesh] explore route %u: %s lv%u\n",
                         (unsigned)exploreRouteIdx_,
                         r ? r->name : "?", (unsigned)activeExploreLevel_);
            } else {
                LOG_WARN("[MonsterMesh] explore: bad route %u\n",
                         (unsigned)exploreRouteIdx_);
                exploreRouteIdx_ = 0xFF;
            }
        }
        // L3: gym battles bypass the neighbor-pick path. Build the gym
        // trainer's party via lordBuildGymParty and tag the rival with the
        // first 4 chars of the trainer's name (each grunt has their own).
        if (e4MemberIdx_ >= 5 && exploreRouteIdx_ >= 8 && gymBattleIdx_ < 8) {
            const LordGym *g = lordGym(gymBattleIdx_);
            if (g && lordBuildGymParty(gymBattleIdx_, gymTrainerIdx_, cpuParty)) {
                const char *tn = g->trainers[gymTrainerIdx_].name;
                if (!tn || !tn[0]) tn = g->leaderName;
                snprintf(rivalTag, sizeof(rivalTag), "%.4s", tn);
                LOG_INFO("[MonsterMesh] gym %u: %s — trainer %u (%s), %u pokemon\n",
                         (unsigned)gymBattleIdx_, g->leaderName,
                         (unsigned)gymTrainerIdx_, tn, (unsigned)cpuParty.count);
                // Full trainer name, no truncation — header band is wide
                // enough for "Cerulean City - Janice 3/5" etc.
                snprintf(hdrText, sizeof(hdrText), "%s - %s %u/5",
                         g->city, tn, (unsigned)gymTrainerIdx_ + 1);
                activeGymBattle_ = gymBattleIdx_;
                activeGymTrainer_ = gymTrainerIdx_;
            } else {
                LOG_WARN("[MonsterMesh] gym %u: build party failed, falling back\n",
                         (unsigned)gymBattleIdx_);
                gymBattleIdx_ = 0xFF;  // fall through to neighbor pick
            }
        }

        const auto *peers = daycare_.getNeighbors();
        uint8_t peerCount = daycare_.getNeighborCount();
        if (e4MemberIdx_ >= 5 && exploreRouteIdx_ >= 8 && gymBattleIdx_ >= 8 && peerCount > 0 && peers) {
            uint8_t pick = (uint8_t)(esp_random() % peerCount);
            const auto &n = peers[pick];
            uint8_t party = n.partyCount > 6 ? 6 : n.partyCount;
            cpuParty.count = party;
            for (uint8_t i = 0; i < party; ++i) {
                uint8_t dex = n.party[i].species;
                uint8_t internal = (dex < 152) ? dexToInternal[dex] : 0;
                cpuParty.species[i] = internal;
                cpuParty.mons[i].species = internal;
                cpuParty.mons[i].level   = n.party[i].level;
                cpuParty.mons[i].boxLevel = n.party[i].level;
                memcpy(cpuParty.mons[i].moves, n.party[i].moves, 4);
                // Encode level→exp roughly so the engine's stat math (which
                // re-reads exp) starts at the right level baseline.
                uint32_t exp = expForLevel(dex, n.party[i].level);
                cpuParty.mons[i].exp[0] = (exp >> 16) & 0xFF;
                cpuParty.mons[i].exp[1] = (exp >> 8)  & 0xFF;
                cpuParty.mons[i].exp[2] =  exp        & 0xFF;
                // Average DVs (8 across the board) — daycare beacons don't
                // carry DVs, and a fair fight against an unknown party is
                // best served by a vanilla baseline.
                cpuParty.mons[i].dvs[0] = 0x88;
                cpuParty.mons[i].dvs[1] = 0x88;
                // Copy nickname (already ASCII in the beacon).
                for (uint8_t j = 0; j < 10 && n.party[i].nickname[j]; ++j) {
                    cpuParty.nicknames[i][j] = (uint8_t)n.party[i].nickname[j];
                }
            }
            // Trainer tag: Meshtastic short name (4 chars) of the partner.
            snprintf(rivalTag, sizeof(rivalTag), "%.4s", n.shortName);
            LOG_INFO("[MonsterMesh] text battle: rival = %s (%u pokemon)\n",
                     rivalTag, (unsigned)party);
        } else if (e4MemberIdx_ >= 5 && exploreRouteIdx_ >= 8 && gymBattleIdx_ >= 8) {
            // No neighbors AND no gym requested — fall back to a "wild
            // trainer" picked at random from the LoC roster. Better than a
            // mirror match: gives a real opponent with their own party so
            // the user can practice solo.
            uint8_t gIdx = (uint8_t)(esp_random() % 8);
            uint8_t tIdx = (uint8_t)(esp_random() % 5);
            const LordGym *g = lordGym(gIdx);
            if (g && lordBuildGymParty(gIdx, tIdx, cpuParty)) {
                snprintf(rivalTag, sizeof(rivalTag), "%.4s", g->leaderName);
                LOG_INFO("[MonsterMesh] text battle: wild trainer %u/%u (%s)\n",
                         gIdx, tIdx, g->leaderName);
            } else {
                cpuParty = terminal_.getParty();
                LOG_INFO("[MonsterMesh] text battle: fallback mirror\n");
            }
        }
        // After this point the gym + explore + e4 slots are consumed —
        // reset so the next `fight` defaults to neighbor-pick again.
        gymBattleIdx_ = 0xFF;
        exploreRouteIdx_ = 0xFF;
        e4MemberIdx_ = 0xFF;
        const char *ourTag = (owner.short_name[0] != '\0') ? owner.short_name : "ME";
        textBattle_.startLocal(terminal_.getParty(), cpuParty, ourTag, rivalTag);
        if (hdrText[0]) textBattle_.setHeader(hdrText);
        textBattleActive_ = true;
    }

    // Tick + render while active.
    if (textBattleActive_ && setupDone_) {
        // Mid-ladder prompt: as soon as the engine resolves a win in a
        // bulk-MMG fight with another trainer on deck, replace the
        // "press any key to exit" text on the result screen with one
        // that signals there's more coming. Set early (before isActive
        // flips false on user dismissal) so the player sees it.
        if (bbsLadderBulkActive_ &&
            bbsLadderTrainerIdx_ != 0xFF &&
            bbsLadderTrainerIdx_ + 1 < bbsLadderCount_ &&
            textBattle_.engineResult() == Gen1BattleEngine::Result::P1_WIN) {
            textBattle_.setEndPrompt("Press any key for next gym member.");
        } else if (bbsLadderBulkActive_ &&
                   textBattle_.engineResult() != Gen1BattleEngine::Result::ONGOING) {
            // Final fight in the ladder — clear the override so the
            // default exit text shows.
            textBattle_.setEndPrompt("");
        }
        textBattle_.tick(millis());
        if (textBattle_.dirty() && g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            textBattle_.render(g_deviceUiLgfx);
            textBattle_.clearDirty();
        }
        // Drain per-faint XP that the engine accumulated this turn. The
        // LVGL thread (tryConsumeStagedParty) credits each slot directly
        // to the saved party — no further splitting.
        uint32_t xp[6] = {};
        if (textBattle_.consumePendingXp(xp)) {
            for (uint8_t i = 0; i < 6; ++i) stagedXp_[i] += xp[i];
            pendingXpAwardCb_ = true;
        }
        // Battle ended — gym gauntlets chain straight into the next
        // trainer without healing the player. End cleanup only fires when
        // we either (a) lost, or (b) cleared the leader.
        if (!textBattle_.isActive()) {
            bool gauntletContinue = false;
            if (activeGymBattle_ < 8) {
                bool won = (textBattle_.engineResult() ==
                            Gen1BattleEngine::Result::P1_WIN);
                if (won && activeGymTrainer_ < 4) {
                    // Build the next trainer's party and stay in-battle.
                    Gen1Party nextParty = {};
                    uint8_t nextIdx = activeGymTrainer_ + 1;
                    if (lordBuildGymParty(activeGymBattle_, nextIdx, nextParty)) {
                        const LordGym *g = lordGym(activeGymBattle_);
                        const char *tn = g ? g->trainers[nextIdx].name : "NEXT";
                        char tag[6];
                        snprintf(tag, sizeof(tag), "%.4s", tn);
                        textBattle_.nextOpponent(nextParty, tag);
                        char hdr[40];
                        snprintf(hdr, sizeof(hdr), "%s - %s %u/5",
                                 g ? g->city : "Gym", tn,
                                 (unsigned)nextIdx + 1);
                        textBattle_.setHeader(hdr);
                        activeGymTrainer_ = nextIdx;
                        gauntletContinue = true;
                        LOG_INFO("[MonsterMesh] gym %u: chain to trainer %u (%s)\n",
                                 (unsigned)activeGymBattle_, (unsigned)nextIdx, tn);
                    }
                }
                if (!gauntletContinue) {
                    // Either lost, or cleared the leader. Stage the
                    // callback for the LVGL thread to deliver — the
                    // terminal callbacks lv_label_create into the output
                    // which isn't safe from the LoRa thread.
                    stagedEndKind_ = StagedEndKind::GYM;
                    stagedEndA_    = activeGymBattle_;
                    stagedEndB_    = activeGymTrainer_;
                    stagedEndWon_  = won;
                    pendingBattleEndedCb_ = true;
                    activeGymBattle_  = 0xFF;
                }
            } else if (activeExploreRoute_ < 8) {
                // L4 wild battle just ended — single-encounter (no chain).
                bool won = (textBattle_.engineResult() ==
                            Gen1BattleEngine::Result::P1_WIN);
                stagedEndKind_ = StagedEndKind::EXPLORE;
                stagedEndA_    = activeExploreRoute_;
                stagedEndB_    = activeExploreLevel_;
                stagedEndWon_  = won;
                pendingBattleEndedCb_ = true;
                activeExploreRoute_ = 0xFF;
                activeExploreLevel_ = 0;
            } else if (!bbsFightActive_ && bbsLadderTrainerIdx_ != 0xFF) {
                // Between-trainers state: result already handled on a
                // prior tick, we're waiting for the next party's chunks
                // to arrive. Keep the battle UI alive so the user doesn't
                // snap back to the terminal between trainers.
                gauntletContinue = true;
            } else if (bbsFightActive_) {
                // A trainer fight just finished. Behavior splits:
                //   bulk path  → advance locally with the cached parties;
                //                send BBS_FIGHT_RESULT only at the end.
                //   legacy     → send result every fight, re-request next.
                bool won = (textBattle_.engineResult() ==
                            Gen1BattleEngine::Result::P1_WIN);

                bool inLadder = (bbsLadderTrainerIdx_ != 0xFF);
                bool isFinal  = !inLadder ||
                                !won ||
                                (bbsLadderTrainerIdx_ + 1 >= bbsLadderCount_);

                if (bbsLadderBulkActive_ && inLadder && won &&
                    bbsLadderTrainerIdx_ + 1 < bbsLadderCount_) {
                    // BULK path, mid-ladder win: advance locally, no LoRa.
                    bbsLadderTrainerIdx_++;
                    textBattle_.healPlayer();
                    const char *nm = bbsLadderNames_[bbsLadderTrainerIdx_];
                    textBattle_.nextOpponent(bbsLadderParties_[bbsLadderTrainerIdx_],
                                              nm[0] ? nm : "GYM");
                    char hdr[40];
                    snprintf(hdr, sizeof(hdr), "MM Gym - %.16s %u/%u",
                             nm[0] ? nm : "Trainer",
                             (unsigned)bbsLadderTrainerIdx_ + 1,
                             (unsigned)bbsLadderCount_);
                    textBattle_.setHeader(hdr);
                    gauntletContinue = true;
                    LOG_INFO("[MonsterMesh] MMG bulk: advance to trainer %u/%u\n",
                             (unsigned)bbsLadderTrainerIdx_ + 1,
                             (unsigned)bbsLadderCount_);
                } else if (!bbsLadderBulkActive_ && inLadder && won &&
                           bbsLadderTrainerIdx_ + 1 < bbsLadderCount_) {
                    // LEGACY path, mid-ladder win: per-trainer ack + new
                    // request (1-mon parties from old protocol).
                    if (service && router && bbsFightTarget_) {
                        meshtastic_MeshPacket *out = router->allocForSending();
                        if (out) {
                            out->to              = bbsFightTarget_;
                            out->channel         = MONSTERMESH_CHANNEL;
                            out->want_ack        = false;
                            out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
                            const meshtastic_NodeInfoLite *self =
                                nodeDB ? nodeDB->getMeshNode(nodeDB->getNodeNum())
                                       : nullptr;
                            const char *name =
                                (self && self->has_user) ? self->user.short_name : "anon";
                            uint8_t nameLen = (uint8_t)strlen(name);
                            if (nameLen > 12) nameLen = 12;
                            uint8_t buf[BATTLELINK_HDR_SIZE + 14];
                            BattlePacket *pkt = (BattlePacket *)buf;
                            memset(buf, 0, sizeof(buf));
                            pkt->type = (uint8_t)PktType::BBS_FIGHT_RESULT;
                            pkt->setSessionId(0);
                            pkt->seq = 0;
                            pkt->payload[0] = 1;
                            pkt->payload[1] = nameLen;
                            memcpy(pkt->payload + 2, name, nameLen);
                            size_t total = BATTLELINK_HDR_SIZE + 2 + nameLen;
                            memcpy(out->decoded.payload.bytes, buf, total);
                            out->decoded.payload.size = total;
                            service->sendToMesh(out, RX_SRC_LOCAL, true);
                        }
                    }
                    bbsLadderTrainerIdx_++;
                    textBattle_.healPlayer();
                    char hdr[40];
                    snprintf(hdr, sizeof(hdr),
                             "MM Gym - awaiting trainer %u/%u...",
                             (unsigned)bbsLadderTrainerIdx_ + 1,
                             (unsigned)bbsLadderCount_);
                    textBattle_.setHeader(hdr);
                    bbsFightActive_     = false;
                    bbsFightAwaitParty_ = false;
                    bbsPartyChunkMask_  = 0;
                    bbsPartyTotal_      = 0;
                    bbsLadderRequestPending_ = true;
                    gauntletContinue = true;
                } else if (isFinal) {
                    // Ladder ended (final win, loss, or non-ladder fight) —
                    // send a single result packet.
                    if (service && router && bbsFightTarget_) {
                        meshtastic_MeshPacket *out = router->allocForSending();
                        if (out) {
                            out->to              = bbsFightTarget_;
                            out->channel         = MONSTERMESH_CHANNEL;
                            out->want_ack        = false;
                            out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
                            const meshtastic_NodeInfoLite *self =
                                nodeDB ? nodeDB->getMeshNode(nodeDB->getNodeNum())
                                       : nullptr;
                            const char *name =
                                (self && self->has_user) ? self->user.short_name : "anon";
                            uint8_t nameLen = (uint8_t)strlen(name);
                            if (nameLen > 12) nameLen = 12;
                            uint8_t buf[BATTLELINK_HDR_SIZE + 14];
                            BattlePacket *pkt = (BattlePacket *)buf;
                            memset(buf, 0, sizeof(buf));
                            pkt->type = (uint8_t)PktType::BBS_FIGHT_RESULT;
                            pkt->setSessionId(0);
                            pkt->seq = 0;
                            pkt->payload[0] = won ? 1 : 0;
                            pkt->payload[1] = nameLen;
                            memcpy(pkt->payload + 2, name, nameLen);
                            size_t total = BATTLELINK_HDR_SIZE + 2 + nameLen;
                            memcpy(out->decoded.payload.bytes, buf, total);
                            out->decoded.payload.size = total;
                            service->sendToMesh(out, RX_SRC_LOCAL, true);
                        }
                    }
                    if (won && inLadder) {
                        terminal_.printLine("MM Gym cleared! You bested the leader.");
                    }
                    LOG_INFO("[MonsterMesh] BBS fight ended — %s sent to gym 0x%08X\n",
                             won ? "WIN" : "LOSS", (unsigned)bbsFightTarget_);
                    bbsLadderTrainerIdx_  = 0xFF;
                    bbsLadderBulkActive_  = false;
                    bbsFightActive_       = false;
                    bbsFightAwaitParty_   = false;
                    bbsFightTarget_       = 0;
                    bbsPartyChunkMask_    = 0;
                }
            } else if (activeE4Member_ < 5) {
                // Indigo Plateau: chain to next member on win, like the gym
                // gauntlet. Final win/loss bubbles to terminal.onE4BattleEnded.
                bool won = (textBattle_.engineResult() ==
                            Gen1BattleEngine::Result::P1_WIN);
                if (won && activeE4Member_ < 4) {
                    Gen1Party nextParty = {};
                    uint8_t nextIdx = activeE4Member_ + 1;
                    if (lordBuildE4Party(nextIdx, nextParty)) {
                        const LordE4Member *m = lordE4Member(nextIdx);
                        char tag[6];
                        snprintf(tag, sizeof(tag), "%.4s",
                                 m ? m->name : "NEXT");
                        textBattle_.nextOpponent(nextParty, tag);
                        char hdr[40];
                        snprintf(hdr, sizeof(hdr), "Indigo Plateau - %s %u/5",
                                 m ? m->name : "?", (unsigned)nextIdx + 1);
                        textBattle_.setHeader(hdr);
                        activeE4Member_ = nextIdx;
                        gauntletContinue = true;
                        LOG_INFO("[MonsterMesh] e4: chain to %u (%s)\n",
                                 (unsigned)nextIdx, m ? m->name : "?");
                    }
                }
                if (!gauntletContinue) {
                    stagedEndKind_ = StagedEndKind::E4;
                    stagedEndA_    = activeE4Member_;
                    stagedEndB_    = 0;
                    stagedEndWon_  = won;
                    pendingBattleEndedCb_ = true;
                    activeE4Member_ = 0xFF;
                }
            }
            if (gauntletContinue) {
                // Battle continues with the next trainer — keep dirty
                // flag so the next render shows the new opponent.
            } else {
                textBattleActive_ = false;
#if HAS_TFT
                // Wipe the lgfx-rendered battle frame so the terminal doesn't
                // have to fight through press-any-key pixels. lgfx ops are
                // SPI-locked and safe from this (LoRa) thread.
                if (g_deviceUiLgfx) {
                    concurrency::LockGuard g(spiLock);
                    g_deviceUiLgfx->clearClipRect();
                    g_deviceUiLgfx->fillScreen(0x0000);
                }
                // Defer LVGL widget ops (flush_cb restore, invalidate,
                // refr_now, refocus) to the LVGL thread — calling them from
                // here corrupted LVGL state and left the terminal textarea
                // unable to receive keypresses after a battle ended.
                pendingBattleEndCleanup_ = true;
                // Schedule SAV write-back of the (possibly XP-leveled)
                // party. The drain in this same runOnce loop checks that
                // the emulator isn't running and no cart is loaded so we
                // never trample emu state. Win or lose — XP earned this
                // fight is permanent.
                if (loadedSavPath_[0]) pendingSavWriteBack_ = true;
#endif
                LOG_INFO("[MonsterMesh] text battle: ended\n");
            }
        }
    }

    // Dungeons and MonstersMesh — tick + render overlay
    if (setupDone_) {
        dungeon_.tick(millis());
        if (dungeonActive_ && g_deviceUiLgfx) {
            uint32_t now = millis();
            if (now - lastDungeonRenderMs_ > 5000) {
                // 5-second gap → device likely woke from light sleep.
                // Re-steal LVGL flush so the Meshtastic UI doesn't paint over us.
#if HAS_TFT
                lv_display_t *disp = lv_display_get_default();
                if (disp) {
                    dungeonFlushCb_ = (void *)disp->flush_cb;
                    lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                        lv_display_flush_ready(d);
                    });
                    if (g_deviceUiLgfx) g_deviceUiLgfx->clearClipRect();
                }
#endif
                dungeonOverlay_.forceRedraw();
            }
            concurrency::LockGuard g(spiLock);
            dungeonOverlay_.render(g_deviceUiLgfx);
            lastDungeonRenderMs_ = millis();
        }
    }

    // Drain any newly-fired daycare event, regardless of whether it was
    // generated by tick() or by triggerArrivalEvent() in handleReceived.
    // Running here keeps all DM TX on the LoRa thread, never the router or
    // LVGL thread.
    if (setupDone_ && !emulatorActive_ && !browserActive_) {
        uint32_t evtTime = daycare_.getLastEventTime();
        if (evtTime != 0 && evtTime != lastDmedEventTime_) {
            lastDmedEventTime_ = evtTime;
            const auto &evt = daycare_.getLastEvent();
            if (evt.message[0]) {
                sendTextDM(nodeDB->getNodeNum(), evt.message);
                LOG_INFO("[MonsterMesh] event DM: %s\n", evt.message);
                if (evt.targetNodeId != 0 &&
                    evt.targetNodeId != nodeDB->getNodeNum()) {
                    // POV swap: arrival events already supply remoteMessage.
                    // For periodic events the engine only fills evt.message
                    // (in the local trainer's POV — "Your X met THEIR-Y").
                    // We do a lightweight runtime rewrite so the partner
                    // reads it from THEIR POV.
                    const char *ourTag = (owner.short_name[0] != '\0')
                                          ? owner.short_name : "ME";
                    const char *theirTag = "";
                    {
                        const auto *peers = daycare_.getNeighbors();
                        uint8_t pc = daycare_.getNeighborCount();
                        for (uint8_t i = 0; i < pc; ++i) {
                            if (peers[i].nodeId == evt.targetNodeId) {
                                theirTag = peers[i].shortName;
                                break;
                            }
                        }
                    }
                    char swapped[200] = {};
                    const char *partnerMsg;
                    if (evt.remoteMessage[0]) {
                        partnerMsg = evt.remoteMessage;
                    } else {
                        // Walk the source string emitting characters; substitute
                        // "Your " → "<ourTag>'s " and "<theirTag>-" or
                        // "<theirTag>'s " → "your ".
                        size_t o = 0;
                        size_t tlen = theirTag[0] ? strlen(theirTag) : 0;
                        for (const char *p = evt.message;
                             *p && o < sizeof(swapped) - 1; ) {
                            if (strncmp(p, "Your ", 5) == 0) {
                                int w = snprintf(swapped + o,
                                                 sizeof(swapped) - o,
                                                 "%s's ", ourTag);
                                if (w > 0) o += (size_t)w;
                                p += 5;
                                continue;
                            }
                            if (tlen > 0 && strncmp(p, theirTag, tlen) == 0) {
                                if (p[tlen] == '-') {
                                    int w = snprintf(swapped + o,
                                                     sizeof(swapped) - o,
                                                     "your ");
                                    if (w > 0) o += (size_t)w;
                                    p += tlen + 1;
                                    continue;
                                }
                                if (p[tlen] == '\'' && p[tlen + 1] == 's' &&
                                    p[tlen + 2] == ' ') {
                                    int w = snprintf(swapped + o,
                                                     sizeof(swapped) - o,
                                                     "your ");
                                    if (w > 0) o += (size_t)w;
                                    p += tlen + 3;
                                    continue;
                                }
                            }
                            swapped[o++] = *p++;
                        }
                        swapped[o] = '\0';
                        partnerMsg = swapped[0] ? swapped : evt.message;
                    }
                    sendTextDM(evt.targetNodeId, partnerMsg);
                    LOG_INFO("[MonsterMesh] event DM → peer 0x%08X: %s\n",
                             (unsigned)evt.targetNodeId, partnerMsg);
                }
            }
        }
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

    // Keep device-ui backlight alive while emulator, browser, or text battle
    // is active. The text-battle screen renders via lgfx (not LVGL), so the
    // device-ui inactivity monitor doesn't see those frames as activity —
    // we have to poke it explicitly here so playing a fight doesn't blank
    // the screen mid-turn.
    bool keepAlive = (monsterMeshModule &&
                      (monsterMeshModule->isEmulatorActive() ||
                       monsterMeshModule->isBrowserActive() ||
                       monsterMeshModule->isTextBattleActive()));
    if (keepAlive) {
        lv_display_trigger_activity(NULL);
    }

    // Pick up any SAV-loaded party that runOnce staged for us — this runs on
    // the LVGL thread, so it's safe to mutate widgets here.
    if (monsterMeshModule) monsterMeshModule->tryConsumeStagedParty();

    // ── ALT + Mic button peek ──────────────────────────────────────────
    // ALT (byte[0] bit 0x10) = toggle screens, Mic (byte[0] bit 0x40) = toggle sound.
    // Peek at RAW mode every few cycles to check them, then switch back to KEY mode.
    // The brief RAW window may drop a key the user presses during it, but this
    // is the only way to detect ALT-alone (ALT+E in KEY mode produces 0x05 but
    // ALT alone produces nothing). Run in browser mode too so users can ALT-out.
    if (!g_rawMode && monsterMeshModule && (++g_micPollCounter >= 3)) {
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
        if (!altPressed) g_keyPeekAltSeenLow = true;
        if (altPressed && !g_keyPeekAltWasPressed && g_keyPeekAltSeenLow) {
            uint32_t now = millis();
            if (now - g_lastAltFireMs > 1000) {
                g_lastAltFireMs = now;
                g_micLastToggleMs = now;
                g_keyPeekAltWasPressed = true;
                LOG_INFO("[MonsterMesh] ALT pressed (KEY-mode peek) → toggle\n");
                monsterMeshModule->handleKeyFromLVGL(0x05);
                return;
            }
        }
        g_keyPeekAltWasPressed = altPressed;

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

        // SYM+ALT held → eject ROM, return to browser. Check BEFORE ALT-alone
        // so the user can hold both without triggering the Meshtastic-exit path.
        bool symAltHeld = (b[0] & 0x14) == 0x14;  // SYM=0x04 + ALT=0x10
        static bool g_symAltConsumed = false;
        if (symAltHeld && !g_symAltConsumed) {
            g_symAltConsumed = true;
            if (monsterMeshModule) {
                monsterMeshModule->setJoypadDirect(0);
                monsterMeshModule->ejectROM();
                kbSetMode(false);  // browser uses KEY mode
            }
            // Force the KEY-mode peek's edge detector to require a fresh
            // release+press. Otherwise the still-held ALT after eject looks
            // like a rising edge and toggles the user back to Meshtastic.
            g_keyPeekAltSeenLow = false;
            g_keyPeekAltWasPressed = true;
            return;
        }
        if (!symAltHeld) g_symAltConsumed = false;

        // ALT button in RAW mode — toggle screens (exit emulator)
        bool altHeld = (b[0] & 0x10) != 0;
        static bool g_altWasHeldRaw = false;
        static bool g_altSeenLowRaw = false;
        if (!altHeld) g_altSeenLowRaw = true;
        if (altHeld && !g_altWasHeldRaw && g_altSeenLowRaw) {
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

    // ── When any MonsterMesh mode owns the screen, consume the key and route it
    // directly to handleKeyFromLVGL instead of letting LVGL also process it.
    // Without this, Enter in terminal mode fires BOTH LV_EVENT_READY (onSubmit)
    // AND the InputBroker path (onKey), executing the command twice and producing
    // spurious "unknown command" errors on every other keypress.
    if (monsterMeshModule && (monsterMeshModule->isEmulatorActive() ||
                              monsterMeshModule->isBrowserActive()  ||
                              monsterMeshModule->isTerminalActive()  ||
                              monsterMeshModule->isTextBattleActive() ||
                              monsterMeshModule->isDungeonActive())) {
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

void MonsterMeshModule::ejectROM()
{
    // SYM+ALT: pause cartridge into the browser. We keep emuInitialized_=true
    // and the loaded ROM, so the user can pick a different cart OR explicitly
    // eject via the [Eject Cart] entry that the browser surfaces when a cart
    // is loaded.
    LOG_INFO("[MonsterMesh] Pause to ROM browser — cart kept loaded\n");
    if (emu_.isRunning()) {
        pendingSave_ = true;
    }
    emulatorActive_ = false;     // stops emuTaskLoop runFrame (and audio)
    browserActive_ = true;       // show browser
    browserNeedsScan_ = true;    // re-scan SD for ROM list
    setJoypadDirect(0);
}

// ── D6 — write daycare-earned XP back to the SAV on SD ─────────────────────
// Reads the .sav next to `romPath`, hands it to PokemonDaycare::checkOut()
// for additive XP patching (the patcher only ever increases EXP — it reads
// the live SRAM value and adds the daycare delta on top, then recalcs level
// + 5 stat fields and fixes the checksum byte). Writes the patched 32KB
// back. If anything fails along the way, the original .sav is untouched.
//
// Returns true if the SAV was both read and re-written successfully. Even
// then the patcher may have been a no-op (no XP gained). False ⇒ skipped.
// Patches the daycare-side accumulated XP into the .sav file at savPath
// (an SD-relative or "/sd/..." absolute path). Caller is responsible for
// the cart-loaded gate. Returns true if dc.checkOut() actually wrote
// changes to the SRAM image.
static bool patchSdSavPathWithDaycareXp(const char *sdRel, PokemonDaycare &dc);

static bool patchSavOnSdWithDaycareXp(const char *romPath, PokemonDaycare &dc)
{
    if (!romPath || !romPath[0]) return false;

    // Convert "/sd/foo.gb" → "/foo.sav" the same way the loader does.
    char savPath[256] = {};
    strncpy(savPath, romPath, sizeof(savPath) - 1);
    char *dot = strrchr(savPath, '.');
    if (!dot) return false;
    if (sizeof(savPath) - (dot - savPath) < 5) return false;
    strcpy(dot, ".sav");
    const char *sdRel = savPath;
    if (strncmp(sdRel, "/sd", 3) == 0) sdRel += 3;
    return patchSdSavPathWithDaycareXp(sdRel, dc);
}

static bool patchSdSavPathWithDaycareXp(const char *sdRel, PokemonDaycare &dc)
{
    if (!sdRel || !sdRel[0]) return false;
    SD.end();
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (!SD.begin(SDCARD_CS, SPI)) {
        LOG_WARN("[MonsterMesh] D6 SAV write: SD.begin failed\n");
        return false;
    }

    // Standard Gen 1 SAV is 32KB.
    constexpr size_t SAV_SIZE = 32 * 1024;
    uint8_t *sram = (uint8_t *)heap_caps_malloc(SAV_SIZE, MALLOC_CAP_8BIT);
    if (!sram) {
        LOG_WARN("[MonsterMesh] D6 SAV write: heap alloc %u failed\n",
                 (unsigned)SAV_SIZE);
        return false;
    }

    File f = SD.open(sdRel, FILE_READ);
    if (!f) {
        LOG_WARN("[MonsterMesh] D6 SAV write: SD.open('%s') for read failed\n", sdRel);
        free(sram);
        return false;
    }
    int n = f.read(sram, SAV_SIZE);
    f.close();
    if (n < (int)SAV_SIZE) {
        LOG_WARN("[MonsterMesh] D6 SAV write: short read (%d/%u)\n",
                 n, (unsigned)SAV_SIZE);
        free(sram);
        return false;
    }

    // checkOut() patches in-place (additive only) and zeroes totalXpGained
    // so the next checkout doesn't double-count. Only the EXP / level / 5
    // stat fields per party slot + the checksum byte are touched.
    dc.checkOut(sram);

    // Truncate-on-write so we don't leave stale bytes if the file shrunk.
    File w = SD.open(sdRel, FILE_WRITE);
    if (!w) {
        LOG_WARN("[MonsterMesh] D6 SAV write: SD.open('%s') for write failed\n", sdRel);
        free(sram);
        return false;
    }
    size_t written = w.write(sram, SAV_SIZE);
    w.close();
    free(sram);
    if (written != SAV_SIZE) {
        LOG_WARN("[MonsterMesh] D6 SAV write: short write (%u/%u)\n",
                 (unsigned)written, (unsigned)SAV_SIZE);
        return false;
    }
    LOG_INFO("[MonsterMesh] D6 SAV write: patched '%s' (%u bytes)\n",
             sdRel, (unsigned)written);
    return true;
}

// Scan SD root for any .sav file and copy its SD-relative path into `out`.
// Returns true on hit. Used when no ROM was launched in this boot session, so
// the terminal can still load "the last save used" across reboots.
static bool findFirstSavOnSd(char *out, size_t outLen)
{
    SD.end();
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (!SD.begin(SDCARD_CS, SPI)) {
        LOG_WARN("[MonsterMesh] sav scan: SD.begin failed\n");
        return false;
    }
    File root = SD.open("/");
    if (!root) {
        LOG_WARN("[MonsterMesh] sav scan: SD.open('/') failed\n");
        return false;
    }
    bool found = false;
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        const char *name = entry.name();
        size_t nlen = name ? strlen(name) : 0;
        if (!entry.isDirectory() && nlen >= 4 &&
            strcasecmp(name + nlen - 4, ".sav") == 0) {
            // SD library returns names with leading "/" already on this build.
            if (name[0] == '/') {
                strncpy(out, name, outLen - 1);
            } else {
                if (outLen >= 2) { out[0] = '/'; strncpy(out + 1, name, outLen - 2); }
            }
            out[outLen - 1] = '\0';
            found = true;
            entry.close();
            break;
        }
        entry.close();
    }
    root.close();
    return found;
}

// Load party directly from the .sav file on SD that matches the last-launched
// ROM. This works even when the emulator isn't currently running — gives the
// terminal a usable party from "the last save used" rather than only when
// emulator memory is live. If `romPath` is empty, scans SD for any .sav file.
static bool loadPartyFromSavOnSd(const char *romPath, Gen1Party &out,
                                 char *resolvedSavOut, size_t resolvedSavLen,
                                 char *trainerNameOut, size_t trainerNameLen)
{
    char savPath[256] = {};
    const char *sdRel = nullptr;

    if (romPath && romPath[0]) {
        // SAV file lives next to the ROM with .sav extension. romPath is the VFS
        // path like "/sd/pokemon.gb" — convert to "/sd/pokemon.sav".
        strncpy(savPath, romPath, sizeof(savPath) - 1);
        char *dot = strrchr(savPath, '.');
        if (!dot) return false;
        if (sizeof(savPath) - (dot - savPath) < 5) return false;
        strcpy(dot, ".sav");
        sdRel = savPath;
        if (strncmp(sdRel, "/sd", 3) == 0) sdRel += 3;
    }

    // Same reinit dance the emulator does — SD library state is "ended" after
    // every SD operation in this codebase, so SD.open silently returns null
    // unless we re-begin first.
    SD.end();
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (!SD.begin(SDCARD_CS, SPI)) {
        LOG_WARN("[MonsterMesh] terminal SAV load: SD.begin failed\n");
        return false;
    }

    // No ROM path → fall back to scanning SD for any .sav file.
    if (!sdRel) {
        if (!findFirstSavOnSd(savPath, sizeof(savPath))) {
            LOG_WARN("[MonsterMesh] terminal SAV load: no ROM path, no .sav on SD\n");
            return false;
        }
        sdRel = savPath;
        // findFirstSavOnSd already calls SD.end()+SD.begin() — reopen state ok.
    }

    LOG_INFO("[MonsterMesh] terminal SAV load: trying '%s'\n", sdRel);
    File f = SD.open(sdRel, FILE_READ);
    if (!f) {
        LOG_WARN("[MonsterMesh] terminal SAV load: SD.open('%s') failed\n", sdRel);
        return false;
    }

    // Gen 1 SAV is 32KB. We only need the party block.
    static constexpr uint16_t SAV_TRAINER_NAME = 0x2598;
    static constexpr uint8_t  SAV_TRAINER_LEN  = 7;
    static constexpr uint16_t SAV_PARTY_COUNT  = 0x2F2C;
    static constexpr uint16_t SAV_SPECIES_LIST = 0x2F2D;
    static constexpr uint16_t SAV_POKEMON_DATA = 0x2F34;
    static constexpr uint16_t SAV_NICKNAMES    = 0x307E;
    static constexpr uint8_t  SAV_NAME_SIZE    = 11;
    static constexpr uint8_t  SAV_TERMINATOR   = 0x50;
    static constexpr uint16_t SAV_PARTY_END    = 0x307E + 11 * 6;  // ~12.5KB

    // Read the party region into a small buffer.
    uint8_t *buf = (uint8_t *)heap_caps_malloc(SAV_PARTY_END, MALLOC_CAP_8BIT);
    if (!buf) { f.close(); return false; }
    f.seek(0);
    int n = f.read(buf, SAV_PARTY_END);
    f.close();
    if (n < SAV_PARTY_END) { free(buf); return false; }

    memset(&out, 0, sizeof(out));
    uint8_t count = buf[SAV_PARTY_COUNT];
    if (count > 6) count = 6;
    out.count = count;
    memcpy(out.species,   &buf[SAV_SPECIES_LIST],  7);
    memcpy((uint8_t *)out.mons, &buf[SAV_POKEMON_DATA], (size_t)count * 44);
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *nick = &buf[SAV_NICKNAMES + i * SAV_NAME_SIZE];
        for (int j = 0; j < SAV_NAME_SIZE; ++j) {
            out.nicknames[i][j] = nick[j];
            if (nick[j] == SAV_TERMINATOR) break;
        }
    }
    if (trainerNameOut && trainerNameLen) {
        // Decode 7-char Gen 1 trainer name at SAV+0x2598. Stops at the 0x50
        // terminator. trainerNameLen must be >= 8 for a clean copy.
        size_t maxOut = trainerNameLen - 1;
        size_t w = 0;
        for (uint8_t j = 0; j < SAV_TRAINER_LEN && w < maxOut; ++j) {
            uint8_t c = buf[SAV_TRAINER_NAME + j];
            if (c == SAV_TERMINATOR) break;
            char a = gen1CharToAscii(c);
            if (a == '\0' || a == '?') break;
            trainerNameOut[w++] = a;
        }
        trainerNameOut[w] = '\0';
    }
    free(buf);
    if (resolvedSavOut && resolvedSavLen) {
        strncpy(resolvedSavOut, sdRel, resolvedSavLen - 1);
        resolvedSavOut[resolvedSavLen - 1] = '\0';
    }
    return true;
}

// Write the player's party block back to the on-SD .sav file at the same
// offsets loadPartyFromSavOnSd reads from. Only the party region is touched
// (0x2F2C..0x30E3 inclusive, plus a fresh checksum at 0x3523) — trainer
// name, badges, items, and everything else stays untouched. Returns true
// on success. Caller must hold spiLock.
static bool writePartyToSavOnSd(const char *savPath, const Gen1Party &party)
{
    if (!savPath || !savPath[0]) return false;

    SD.end();
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (!SD.begin(SDCARD_CS, SPI)) return false;

    // SAV mirror block layout (gen 1, party half):
    static constexpr uint16_t SAV_PARTY_COUNT  = 0x2F2C;
    static constexpr uint16_t SAV_SPECIES_LIST = 0x2F2D;
    static constexpr uint16_t SAV_POKEMON_DATA = 0x2F34;
    static constexpr uint16_t SAV_OT_NAMES     = 0x303C;  // 6 × 11 bytes
    static constexpr uint16_t SAV_NICKNAMES    = 0x307E;  // 6 × 11 bytes
    static constexpr uint16_t SAV_NAME_BLOCK_END = 0x30E4;
    static constexpr uint16_t SAV_CHECKSUM_OFFSET = 0x3523;
    static constexpr uint16_t SAV_CHECKSUM_START  = 0x2598;
    static constexpr uint16_t SAV_CHECKSUM_END    = 0x3522;
    static constexpr uint8_t  SAV_NAME_SIZE    = 11;
    static constexpr size_t   SAV_FILE_SIZE    = 32 * 1024;  // 32 KB

    File f = SD.open(savPath, FILE_READ);
    if (!f) { LOG_WARN("[MonsterMesh] sav writeback: open '%s' (read) failed\n", savPath); return false; }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(SAV_FILE_SIZE, MALLOC_CAP_8BIT);
    if (!buf) { f.close(); return false; }
    f.seek(0);
    int n = f.read(buf, SAV_FILE_SIZE);
    f.close();
    if (n < (int)SAV_FILE_SIZE) {
        LOG_WARN("[MonsterMesh] sav writeback: short read %d\n", n);
        free(buf); return false;
    }

    // Patch the party block in-place.
    uint8_t count = party.count > 6 ? 6 : party.count;
    buf[SAV_PARTY_COUNT] = count;
    memcpy(&buf[SAV_SPECIES_LIST], party.species, 7);
    memcpy(&buf[SAV_POKEMON_DATA], (const uint8_t *)party.mons, (size_t)count * 44);
    for (uint8_t i = 0; i < count; ++i) {
        memcpy(&buf[SAV_OT_NAMES   + i * SAV_NAME_SIZE],
               party.otNames[i],   SAV_NAME_SIZE);
        memcpy(&buf[SAV_NICKNAMES + i * SAV_NAME_SIZE],
               party.nicknames[i], SAV_NAME_SIZE);
    }
    (void)SAV_NAME_BLOCK_END;

    // Recompute the SAV checksum.
    uint8_t sum = 0;
    for (uint32_t i = SAV_CHECKSUM_START; i <= SAV_CHECKSUM_END; ++i) sum += buf[i];
    buf[SAV_CHECKSUM_OFFSET] = (uint8_t)~sum;

    File w = SD.open(savPath, FILE_WRITE);
    if (!w) { LOG_WARN("[MonsterMesh] sav writeback: open '%s' (write) failed\n", savPath); free(buf); return false; }
    w.seek(0);
    size_t wrote = w.write(buf, SAV_FILE_SIZE);
    w.close();
    free(buf);
    if (wrote != SAV_FILE_SIZE) {
        LOG_WARN("[MonsterMesh] sav writeback: short write %u\n", (unsigned)wrote);
        return false;
    }
    LOG_INFO("[MonsterMesh] sav writeback: wrote party + checksum to '%s'\n", savPath);
    return true;
}

void MonsterMeshModule::tryConsumeStagedParty()
{
    // Battle-end LVGL cleanup deferred from the LoRa thread. Run BEFORE
    // the staged-party check so the terminal is fully repainted before we
    // mutate widgets again.
    if (pendingBattleEndCleanup_) {
        pendingBattleEndCleanup_ = false;
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && savedFlushCb_) {
            lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
            savedFlushCb_ = nullptr;
        }
        if (disp) {
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(disp);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(disp);
        }
        if (terminalActive_) terminal_.refocus();
#endif
    }
    // Per-faint XP draining — flush whatever the engine accumulated since
    // last tick into the saved party. Runs ahead of the battle-end
    // callback so XP from the killing blow lands before the news entry.
    if (pendingXpAwardCb_) {
        pendingXpAwardCb_ = false;
        uint32_t xp[6];
        bool any = false;
        for (uint8_t i = 0; i < 6; ++i) {
            xp[i] = stagedXp_[i];
            if (xp[i]) any = true;
            stagedXp_[i] = 0;
        }
        if (any) terminal_.creditBattleXpPerSlot(xp);
    }
    // Battle-result callback (terminal_.on*BattleEnded) — runs after the
    // LVGL cleanup so the panel is repainted before we add news lines.
    if (pendingBattleEndedCb_) {
        pendingBattleEndedCb_ = false;
        switch (stagedEndKind_) {
            case StagedEndKind::GYM:
                terminal_.onGymBattleEnded(stagedEndA_, stagedEndB_, stagedEndWon_);
                break;
            case StagedEndKind::EXPLORE:
                terminal_.onExploreBattleEnded(stagedEndA_, stagedEndWon_, stagedEndB_);
                break;
            case StagedEndKind::E4:
                terminal_.onE4BattleEnded(stagedEndA_, stagedEndWon_);
                break;
            default: break;
        }
        stagedEndKind_ = StagedEndKind::NONE;
    }
    if (!terminalPartyStaged_) return;
    if (!terminalActive_) {
        // Terminal isn't visible — drop the staged party rather than mutating
        // a hidden panel. Next open will trigger a fresh load.
        terminalPartyStaged_ = false;
        return;
    }
    terminal_.setParty(terminalStagedParty_);
    terminal_.refreshParty();
    terminalPartyStaged_ = false;
}

void MonsterMeshModule::toggleTerminal()
{
#if HAS_TFT
    // The map button (>_ icon) always SHOWS the terminal — no toggle. The
    // terminal stays alive when the user navigates to other panels (Nodes,
    // Settings, etc.); coming back to this nav button must re-show it, not
    // close it. The user closes by navigating away or by ALT'ing into the
    // emulator.
    {
        lv_obj_t *parent = g_terminalParent ? g_terminalParent : lv_screen_active();
        if (!parent) return;
        terminal_.open(parent);
        terminalActive_ = true;
        // Only request a SAV load + party render the first time. After the
        // initial load + check-in, leave the scrollback alone so re-entries
        // don't reprint the party every visit.
        if (!terminal_.hasParty()) {
            terminalNeedsParty_ = true;
        }
    }
#endif
}

void MonsterMeshModule::clearCart()
{
    // [Eject Cart] entry inside the browser: actually unload the ROM. Stay
    // in browser afterward so the user can pick another cart.
    LOG_INFO("[MonsterMesh] Eject cart — unloading ROM\n");
    if (emu_.isRunning()) pendingSave_ = true;
    emuInitialized_ = false;
    browser_.markDirty();  // redraw without [Eject Cart] row
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
        // Idle while emulator is not the foreground mode. Without this, runFrame
        // keeps generating audio and rendering on Core 1 after the user ALTs back
        // to Meshtastic — the screen would appear frozen on the emulator and the
        // music would keep playing.
        if (!emulatorActive_) {
            vTaskDelay(pdMS_TO_TICKS(50));
            lastWake = xTaskGetTickCount();
            continue;
        }
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

        // Yield briefly every few frames so other Core-1 tasks (MonsterMesh
        // runOnce, PacketAPI, RadioLib worker) aren't starved. Without ANY
        // yield they wedge after ~30-60s. A 60fps cap stutters audio because
        // runFrame() was naturally running >60fps; vTaskDelay(1) is just a
        // 1ms tick yield that doesn't pace the framerate.
        static uint8_t s_yieldCount = 0;
        if (++s_yieldCount >= 4) {
            s_yieldCount = 0;
            vTaskDelay(1);
        }

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
    // Text battle steals all keys while it's foreground. ESC (0x1B) ends the
    // battle and returns to the terminal scrollback that was preserved in the
    // background.
    if (textBattleActive_) {
        textBattle_.handleKey(ascii);
        powerFSM.trigger(EVENT_INPUT);
        return;
    }

    // Drop keys (except ALT, which is the universal escape into the ROM
    // browser) while the LVGL battle-end cleanup is still pending. The
    // cleanup runs on the LVGL thread via tryConsumeStagedParty; until it
    // drains, the terminal panel's flush_cb is still the no-op installed at
    // battle start, so any lv_textarea_set_text we'd do here paints into a
    // black hole. ALT escapes regardless so the user is never stuck.
    if (pendingBattleEndCleanup_ && ascii != 0x05 /*ALT*/) {
        return;
    }

    // 'G' — dungeon overlay toggle. Must be checked before terminal routing so
    // pressing G while the terminal is in the background still works after sleep.
    // GATED on the dungeon module being compiled in. When excluded via
    // MESHTASTIC_EXCLUDE_MONSTERMESH_DUNGEON the stubs render nothing, so
    // toggling the overlay just hijacks LVGL's flush_cb and the screen
    // appears frozen — typing 'g' in the terminal would freeze it.
#ifndef MESHTASTIC_EXCLUDE_MONSTERMESH_DUNGEON
    if ((ascii == 'g' || ascii == 'G') && !emulatorActive_ && !browserActive_ && !textBattleActive_) {
        dungeonActive_ = !dungeonActive_;
        if (dungeonActive_) {
            dungeonOverlay_.open();
#if HAS_TFT
            // Suppress LVGL using a separate saved-cb so we never clobber the
            // emulator/browser/textbattle savedFlushCb_ variable.
            lv_display_t *disp = lv_display_get_default();
            if (disp && !dungeonFlushCb_) {
                dungeonFlushCb_ = (void *)disp->flush_cb;
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                    lv_display_flush_ready(d);
                });
                if (g_deviceUiLgfx) g_deviceUiLgfx->clearClipRect();
            }
#endif
        } else {
            dungeonOverlay_.close();
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            if (disp && dungeonFlushCb_) {
                lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)dungeonFlushCb_);
                dungeonFlushCb_ = nullptr;
                lv_obj_invalidate(lv_screen_active());
            }
#endif
        }
        powerFSM.trigger(EVENT_INPUT);
        return;
    }
#endif // !MESHTASTIC_EXCLUDE_MONSTERMESH_DUNGEON

    // Terminal: route ASCII keys to it ONLY when terminal is the foreground
    // (i.e. emulator and browser are both inactive). When the user ALT's into
    // ROM browser or emulator, terminalActive_ stays true so the terminal
    // remains "running in background" with its session preserved, but we let
    // browser/emu own the keyboard. ALT itself always falls through.
    if (terminalActive_ && !emulatorActive_ && !browserActive_ && ascii != 0x05) {
        terminal_.onKey(ascii);
        powerFSM.trigger(EVENT_INPUT);
        return;
    }

    // Keep screen awake on any keypress while emulator or browser is active
    if (emulatorActive_ || browserActive_) {
        powerFSM.trigger(EVENT_INPUT);
    }

    // ── ALT / mic button: toggle display modes ─────────────────────────
    // ALT semantics:
    //   browser/emulator active → exit to Meshtastic (existing behavior)
    //   battle station (terminal) visible → close it → Meshtastic
    //   Meshtastic + battle station "running" (terminal exists, has party)
    //     → show battle station
    //   Meshtastic + battle station NOT running → fall back to ROM loader
    //     so the user can load a SAV. Once loaded, future ALT presses go
    //     straight to battle station.
    // Emulator toggle (when initialized) lives on SYM+E now, not ALT.
    if (ascii == 0x05) {
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

        if (emulatorActive_) {
            // ── Exit emulator → Meshtastic UI ─────────────────────────────
            emulatorActive_ = false;
            exitEmulatorMode();
            if (emu_.isRunning()) pendingSave_ = true;
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            if (disp) {
                lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
                if (gfx) gfx->clearClipRect();
                if (savedFlushCb_) {
                    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                    savedFlushCb_ = nullptr;
                }
                lv_obj_invalidate(lv_screen_active());
            }
#endif
            return;
        }

        // ── ALT in chat or terminal → ROM loader ─────────────────────────
        // textBattle / dungeon consume ALT via their own handleKey path
        // (they're full-screen and block ALT). The terminal panel is
        // intentionally NOT closed here — when ROM loader exits we want
        // to return to the terminal slide, not the Meshtastic map.
        if (!emuInitialized_ && !setupDone_) {
            // Setup hasn't finished (SD not mounted yet)
            LOG_WARN("[MonsterMesh] not ready — status: %s\n", setupStatus_);
            return;
        }

        // ALT always opens the ROM loader (file browser), regardless of
        // whether a cart was previously loaded. Emulator toggle is on
        // SYM+E. Park radios + suppress LVGL flush BEFORE flipping
        // browserActive_ so the bus is quiesced before browser render.
#if HAS_TFT
        {
            lv_display_t *disp = lv_display_get_default();
            if (disp && !savedFlushCb_) {
                savedFlushCb_ = (void *)disp->flush_cb;
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
                    lv_display_flush_ready(d);
                });
            }
        }
        // Black out the framebuffer immediately so the user doesn't see
        // the empty map_panel for a few ms while the SD scan runs.
        if (g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            g_deviceUiLgfx->clearClipRect();
            g_deviceUiLgfx->fillScreen(0x0000);
        }
#endif
        enterEmulatorMode();
        browserActive_ = true;
        browserNeedsScan_ = true;
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

    // Pull palette from active Themes::set() — but force Red/Blue cartridge
    // themes onto the GBC palette here (red/blue UI is hard to read for a
    // file list with .gb file rows).
    auto rgb565 = [](uint32_t aarrggbb) -> uint16_t {
        uint8_t r = (aarrggbb >> 16) & 0xFF;
        uint8_t g = (aarrggbb >> 8) & 0xFF;
        uint8_t b = aarrggbb & 0xFF;
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    };
    uint16_t cBg, cText, cDim, cHi, cAccent;
    if (Themes::get() == Themes::ePokemonRed || Themes::get() == Themes::ePokemonBlue) {
        // GBC base shades (matches Themes.cpp GBC_0..6).
        cBg     = rgb565(0xff081820);  // GBC_0 darkest
        cText   = rgb565(0xffE0F8D0);  // GBC_6 lightest
        cDim    = rgb565(0xff88C070);  // GBC_5 light
        cHi     = rgb565(0xff346856);  // GBC_3 dark
        cAccent = rgb565(0xff5E9464);  // GBC_4 accent
    } else {
        cBg     = rgb565(Themes::darkest());
        cText   = rgb565(Themes::lightest());
        cDim    = rgb565(Themes::light());
        cHi     = rgb565(Themes::dark());
        cAccent = rgb565(Themes::accent());
    }

    concurrency::LockGuard g(spiLock);
    gfx->startWrite();
    gfx->setClipRect(0, 0, 320, 240);
    gfx->fillScreen(cBg);
    gfx->setTextWrap(false);

    // Title
    gfx->setTextSize(2);
    gfx->setTextColor(cText);
    gfx->setCursor(4, 2);
    gfx->print("Select ROM");

    // Current directory (right-aligned, small, dim)
    gfx->setTextSize(1);
    gfx->setTextColor(cDim);
    gfx->setCursor(200, 8);
    gfx->print(browser_.currentDir());

    gfx->setTextSize(1);

    // [Eject Cart] virtual row above the list when a ROM is loaded
    int ejectRowY = -1;
    if (emuInitialized_) {
        ejectRowY = LIST_Y;
        if (ejectFocused_) gfx->fillRect(0, ejectRowY, 320, ROW_H, cHi);
        gfx->setTextSize(1);
        gfx->setTextColor(cAccent);
        gfx->setCursor(4, ejectRowY + 3);
        gfx->print("[Eject Cart]");
    }

    if (browser_.count() == 0) {
        gfx->setTextSize(2);
        gfx->setTextColor(cAccent);
        gfx->setCursor(4, LIST_Y + (emuInitialized_ ? ROW_H : 0) + 20);
        gfx->print("No files found");
        gfx->setTextSize(1);
        gfx->setTextColor(cText);
        gfx->setCursor(4, LIST_Y + (emuInitialized_ ? ROW_H : 0) + 44);
        gfx->print("Path: ");
        gfx->print(browser_.currentDir());
    } else {
        int scroll = browser_.scroll();
        int cursor = browser_.cursor();
        // When [Eject Cart] is shown above, push the file list down by one row.
        int yOffset = emuInitialized_ ? ROW_H : 0;
        int maxRows = emuInitialized_ ? MAX_ROWS - 1 : MAX_ROWS;
        for (int i = 0; i < maxRows && (scroll + i) < browser_.count(); i++) {
            int idx = scroll + i;
            int y = LIST_Y + yOffset + i * ROW_H;

            if (idx == cursor && !ejectFocused_) {
                gfx->fillRect(0, y, 320, ROW_H, cHi);
            }

            gfx->setCursor(4, y + 3);

            const auto &entry = browser_.entries()[idx];
            char dispName[MAX_CHARS + 1];

            if (entry.isDir) {
                gfx->setTextColor(cAccent);
                snprintf(dispName, sizeof(dispName), "[%s]", entry.name);
            } else {
                size_t nlen = strlen(entry.name);
                bool isRom = (nlen >= 3 && strcasecmp(entry.name + nlen - 3, ".gb") == 0) ||
                             (nlen >= 4 && strcasecmp(entry.name + nlen - 4, ".gbc") == 0);
                gfx->setTextColor(isRom ? cText : cDim);
                strncpy(dispName, entry.name, MAX_CHARS);
                dispName[MAX_CHARS] = '\0';
            }
            gfx->print(dispName);
        }
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

    // Small "Loading..." dialog in the GBC green palette so it matches the
    // ROM browser regardless of active theme.
#if HAS_TFT
    if (g_deviceUiLgfx) {
        concurrency::LockGuard g(spiLock);
        lgfx::LGFX_Device *gfx = g_deviceUiLgfx;
        gfx->startWrite();
        const int boxW = 96, boxH = 24;
        const int boxX = (gfx->width()  - boxW) / 2;
        const int boxY = (gfx->height() - boxH) / 2;
        auto rgb565 = [](uint32_t v) -> uint16_t {
            return ((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x001F);
        };
        uint16_t fill   = rgb565(0xff081820);  // GBC darkest
        uint16_t border = rgb565(0xff88C070);  // GBC light
        uint16_t text   = rgb565(0xffE0F8D0);  // GBC lightest
        gfx->fillRect(boxX, boxY, boxW, boxH, fill);
        gfx->drawRect(boxX, boxY, boxW, boxH, border);
        gfx->setTextSize(1);
        gfx->setTextColor(text);
        gfx->setCursor(boxX + 18, boxY + 8);
        gfx->print("Loading...");
        gfx->endWrite();
    }
#endif

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

// Atomic guard for mode-switch transitions. The two ALT-detection paths
// (runOnce poll + LVGL KEY-mode peek) can fire on the same physical press
// up to ~1s apart. Without an atomic claim the slow enterEmulatorMode body
// (~1s of WiFi teardown + radio sleep) runs twice, racing on deinitWifi
// and SPI access — which freezes the device.
static portMUX_TYPE s_mmModeMux = portMUX_INITIALIZER_UNLOCKED;

void MonsterMeshModule::enterEmulatorMode()
{
    portENTER_CRITICAL(&s_mmModeMux);
    bool already = radioParked_;
    if (!already) radioParked_ = true;
    portEXIT_CRITICAL(&s_mmModeMux);
    if (already) {
        LOG_INFO("MonsterMesh: enterEmulatorMode already in progress — skip\n");
        return;
    }

    LOG_INFO("MonsterMesh: entering emulator mode — suspending Meshtastic\n");
    // Daycare yields the SD bus to the emulator. Defer the actual checkOut()
    // call to runOnce on the LoRa thread — SD/SPI work on the LVGL thread
    // would race with the LGFX flush we just suppressed.
    pendingAutoCheckOut_ = true;
    // Snappy path: gate flags + radio sleep ONLY (~few ms). WiFi deinit (~50ms+)
    // is deferred to runOnce on the LoRa thread so the user sees the browser
    // come up instantly after pressing ALT.
    wifiSuppressed = true;
    g_meshSuspended = true;
    // Yield so any in-flight LoRa SPI tx completes and spiLock is released
    // before we take it for the chip sleep command. Skipping this caused
    // SX126x sleep() to hang waiting for the bus.
    vTaskDelay(pdMS_TO_TICKS(20));
    LOG_INFO("MonsterMesh: parking LoRa radio (IRQ disable)\n");
    if (RadioLibInterface::instance) {
        // Soft park: just disable the DIO1 IRQ. The full chip sleep() path
        // (setStandby → checkNotification → standby command → SetSleep) was
        // hanging mid-call on the LVGL thread. Disabling IRQ is one SPI op
        // (clearDio1Action) and matches what the older firmware did.
        // RX packets won't be processed, and TX is already gated by
        // g_meshSuspended in MeshService::handleToRadio.
        RadioLibInterface::instance->disableInterrupt();
    }
    LOG_INFO("MonsterMesh: radios parked\n");
}

void MonsterMeshModule::exitEmulatorMode()
{
    portENTER_CRITICAL(&s_mmModeMux);
    bool wasParked = radioParked_;
    if (wasParked) radioParked_ = false;
    portEXIT_CRITICAL(&s_mmModeMux);
    if (!wasParked) {
        LOG_INFO("MonsterMesh: exitEmulatorMode already in progress — skip\n");
        return;
    }

    LOG_INFO("MonsterMesh: exiting emulator mode — bringing radios back\n");
    // LVGL thread: only the cheap atomic flag flips. The actual radio
    // startReceive() goes through setStandby() → checkNotification() which
    // can hang on the LVGL thread (same hang we saw with sleep()). The
    // initWifi cert work is also slow (~4s). Both run in runOnce() on the
    // LoRa thread, which sees !radioParked_ and reconciles state.
    g_meshSuspended = false;
    wifiSuppressed = false;
    needReconnect = true;
    radioNeedsRx_ = true;
    // Re-arm daycare. runOnce will reload the party from the (possibly
    // updated) SAV and call checkIn → forceBeacon, picking up any XP/level
    // changes the user earned in-game during this session.
    pendingAutoCheckin_ = true;
}

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH
