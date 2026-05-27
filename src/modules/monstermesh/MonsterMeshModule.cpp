#include "MonsterMeshModule.h"

#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH

#include "MeshService.h"
#include "Router.h"
#include "NodeDB.h"
#include "mesh/Default.h"
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
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif
#include "modules/NodeInfoModule.h"
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
#include "modules/NodeInfoModule.h"  // for periodic NodeInfo on MM channel
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

// Forward decl — direct emu resume in runOnce switches kb back to RAW mode.
static void kbSetMode(bool raw);

// Build a full Gen1Party from a daycare neighbor's beacon-broadcast summary.
// The beacon carries only (dex, level, nickname, moves[4]) per mon; this
// helper inflates that to a Gen1Pokemon with deterministic stats so a peer
// who received the beacon can run a battle against the broadcaster's team
// without needing a separate party-exchange protocol. Same conversion the
// BBS bulk-ladder receive path uses (MonsterMeshModule.cpp BBS_LADDER_PARTIES
// branch), kept here so MMB PvP can reuse it.
static void buildPartyFromNeighbor(const DaycareNeighborPokemon &n,
                                   Gen1Party &out)
{
    memset(&out, 0, sizeof(out));
    uint8_t mc = n.partyCount;
    if (mc > 6) mc = 6;
    out.count = mc;
    for (uint8_t i = 0; i < mc; ++i) {
        uint8_t dex   = n.party[i].species;
        uint8_t level = n.party[i].level;
        if (level == 0) level = 5;  // defensive: neighbor data was zeroed
        Gen1MinimalStats s = gen1MinimalStats(dex, level);
        uint8_t internal   = gen1DexToInternal(dex);
        Gen1Pokemon &pk    = out.mons[i];
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
        for (uint8_t mi = 0; mi < 4; ++mi) {
            pk.moves[mi] = n.party[i].moves[mi];
            const Gen1MoveData *mv = gen1Move(pk.moves[mi]);
            pk.pp[mi] = mv ? mv->pp : (pk.moves[mi] ? 25 : 0);
        }
        // Use the broadcaster's nickname so it shows up correctly in
        // battle log lines. Gen1Pokemon has no nickname field — nicknames
        // live on Gen1Party.nicknames[i] and the engine pulls from there.
        out.species[i]  = internal;
        size_t nlen = strnlen(n.party[i].nickname, 10);
        memcpy(out.nicknames[i], n.party[i].nickname, nlen);
        out.nicknames[i][nlen] = '\0';
    }
}
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

// Called from MeshService::handleToRadio for every TEXT_MESSAGE_APP unicast
// the phone hands us, BEFORE it's sent. This is the only place a phone-typed
// "MMB ON" DM can be observed — Router::sendLocal only re-dispatches broadcasts
// through the module pipeline, so unicast outbound DMs never reach
// MeshModule::callModules / MonsterMeshModule::handleReceived.
static bool mmContainsIgnoreCase(const char *hay, size_t hlen,
                                 const char *ndl, size_t nlen)
{
    if (nlen == 0 || hlen < nlen) return false;
    for (size_t i = 0; i + nlen <= hlen; ++i) {
        size_t j = 0;
        for (; j < nlen; ++j) {
            char a = hay[i + j];
            char b = ndl[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (j == nlen) return true;
    }
    return false;
}

void mmSniffPhoneOutboundDM(meshtastic_MeshPacket *p)
{
    if (monsterMeshModule && p) monsterMeshModule->sniffPhoneOutboundDM(p);
}

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

    // b445: wake the screen on EVERY keyboard event MM sees, including the
    // ones we don't intercept (which then fall through to LVGL → terminal
    // textarea). b444's fix in handleKeyPress only fired for events MM
    // actually consumed (ALT, emu/browser, text-battle); typing in the
    // MonsterMesh terminal goes straight to LVGL, so handleKeyPress is
    // never called and the screen blanked while the user was typing.
    if (event->kbchar != 0 || event->inputEvent == INPUT_BROKER_ANYKEY) {
        powerFSM.trigger(EVENT_INPUT);
    }

    // Always process Ctrl+E regardless of emulator state.
    // Share the global ALT-fire debounce window with the I2C poll and the
    // KEY-mode peek so a single press doesn't fire the handler twice
    // (which would enter ROM loader and immediately exit it back to chat).
    if (event->kbchar == 0x05) {  // Ctrl+E
        uint32_t now = millis();
        if (now - g_lastAltFireMs > 250) {
            g_lastAltFireMs = now;
            handleKeyPress(0x05);
        }
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

    // Trackball events → viewport scroll (emulator only — the file browser
    // is meant to be driven by W/S per user preference, not trackball).
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
    // Auto-provision a "MonsterMesh" channel with a baked-in AES128 PSK +
    // MQTT bridge so every node flashed with this firmware can talk to
    // every other one without any phone-app setup. Privacy from accidental
    // third parties only — anyone with the firmware binary can derive the
    // PSK.
    //
    // Strategy:
    //   1. If a channel named "MonsterMesh" already exists at any index,
    //      use it as-is (user may have customized PSK / settings).
    //   2. Else find the first DISABLED slot starting at index 1 and add
    //      MonsterMesh there. We never overwrite a user-configured slot.
    //   3. As a last resort if all 8 slots are taken, log a warning and
    //      bail; user has to free a slot or merge manually.
    // 16-byte AES128 PSK = ASCII "MonsterMesh!2024" — matches the PSK
    // already configured on user's existing T-Decks so new flashes
    // interop with the existing fleet without any phone-app setup.
    static const uint8_t MM_PSK[16] = {
        'M', 'o', 'n', 's', 't', 'e', 'r', 'M',
        'e', 's', 'h', '!', '2', '0', '2', '4'
    };
    // Bump channels_count to 8 if needed so the auto-provision slot is a
    // real channelFile entry, not the malloc'd stub channels::getByIndex
    // returns for out-of-range indices. Without this, fresh devices whose
    // NVS only has 1 channel (LongFast) get their auto-provision silently
    // dropped, leaving anyMqttEnabled() false and MQTT never connecting.
    if (channelFile.channels_count < 8) {
        for (uint32_t i = channelFile.channels_count; i < 8; ++i) {
            memset(&channelFile.channels[i], 0, sizeof(channelFile.channels[i]));
            channelFile.channels[i].index = i;
            channelFile.channels[i].role = meshtastic_Channel_Role_DISABLED;
        }
        channelFile.channels_count = 8;
        LOG_INFO("[MonsterMesh] extended channelFile.channels_count to 8\n");
    }
    int existingIdx = -1;
    int freeIdx = -1;
    for (int i = 0; i < 8; ++i) {
        auto &c = channels.getByIndex(i);
        if (c.role != meshtastic_Channel_Role_DISABLED &&
            strcmp(c.settings.name, "MonsterMesh") == 0) {
            existingIdx = i;
            break;
        }
        if (freeIdx < 0 && i >= 1 &&
            (c.role == meshtastic_Channel_Role_DISABLED ||
             strlen(c.settings.name) == 0)) {
            freeIdx = i;
        }
    }
    if (existingIdx >= 0) {
        mmChannel_ = (uint8_t)existingIdx;
        Serial.printf("[MonsterMesh] reusing existing MonsterMesh channel at index %d\n",
                      existingIdx);
    } else if (freeIdx < 0) {
        Serial.println("[MonsterMesh] no free channel slot — MonsterMesh channel not provisioned");
    } else {
        // Modify channelFile.channels[freeIdx] directly. setChannel() copies
        // a meshtastic_Channel into channelFile.channels[c.index] — so c.index
        // MUST equal freeIdx, otherwise setChannel writes into the wrong slot
        // and the auto-provision is silently lost.
        auto &ch = channels.getByIndex(freeIdx);
        ch.index = freeIdx;
        ch.role = meshtastic_Channel_Role_SECONDARY;
        ch.has_settings = true;  // anyMqttEnabled() ignores channels without this
        snprintf(ch.settings.name, sizeof(ch.settings.name), "MonsterMesh");
        ch.settings.psk.size = sizeof(MM_PSK);
        memcpy(ch.settings.psk.bytes, MM_PSK, sizeof(MM_PSK));
        ch.settings.uplink_enabled   = true;
        ch.settings.downlink_enabled = true;
        channels.setChannel(ch);
        mmChannel_ = (uint8_t)freeIdx;
        // Persist channelFile so anyMqttEnabled() is consistent across reboots
        // and the MQTT module sees the bridge as enabled on first runOnce.
        nodeDB->saveToDisk(SEGMENT_CHANNELS);
        Serial.printf("[MonsterMesh] auto-provisioned MonsterMesh channel at index %d "
                      "(+ PSK + MQTT bridge)\n", freeIdx);
    }

    // Canonicalize moduleConfig.mqtt — force the MonsterMesh private EMQX
    // broker on every boot so devices that came from earlier firmware with
    // mqtt.meshtastic.org credentials auto-migrate without the user touching
    // the phone app. Anything stuck in NVS gets healed in one cycle.
    bool mqttDirty = false;
    const char *desiredAddr     = default_mqtt_address;
    const char *desiredUser     = default_mqtt_username;
    const char *desiredPass     = default_mqtt_password;
    const char *desiredRoot     = "kanto";
    const bool  desiredTls      = true;
    // MQTT must be enabled on the device-side (not phone-proxy) so the deck
    // connects to the EMQX broker directly via WiFi. The phone app's "Send
    // via Phone" toggle silently flips proxy_to_client_enabled=true, after
    // which the deck no longer attempts a direct broker connect and we see
    // "MQTT not connected, queue packet" with zero [MQTT] logs. Heal both.
    if (!moduleConfig.mqtt.enabled) {
        Serial.printf("[MonsterMesh] mqtt.enabled canonicalized: 0 -> 1\n");
        moduleConfig.mqtt.enabled = true;
        mqttDirty = true;
    }
    if (moduleConfig.mqtt.proxy_to_client_enabled) {
        Serial.printf("[MonsterMesh] mqtt.proxy_to_client_enabled canonicalized: 1 -> 0\n");
        moduleConfig.mqtt.proxy_to_client_enabled = false;
        mqttDirty = true;
    }
    if (!moduleConfig.mqtt.encryption_enabled) {
        Serial.printf("[MonsterMesh] mqtt.encryption_enabled canonicalized: 0 -> 1\n");
        moduleConfig.mqtt.encryption_enabled = true;
        mqttDirty = true;
    }
    if (strcmp(moduleConfig.mqtt.address, desiredAddr) != 0) {
        Serial.printf("[MonsterMesh] mqtt.address canonicalized: '%s' -> '%s'\n",
                      moduleConfig.mqtt.address, desiredAddr);
        strncpy(moduleConfig.mqtt.address, desiredAddr,
                sizeof(moduleConfig.mqtt.address) - 1);
        moduleConfig.mqtt.address[sizeof(moduleConfig.mqtt.address) - 1] = '\0';
        mqttDirty = true;
    }
    if (strcmp(moduleConfig.mqtt.username, desiredUser) != 0) {
        strncpy(moduleConfig.mqtt.username, desiredUser,
                sizeof(moduleConfig.mqtt.username) - 1);
        moduleConfig.mqtt.username[sizeof(moduleConfig.mqtt.username) - 1] = '\0';
        mqttDirty = true;
    }
    if (strcmp(moduleConfig.mqtt.password, desiredPass) != 0) {
        strncpy(moduleConfig.mqtt.password, desiredPass,
                sizeof(moduleConfig.mqtt.password) - 1);
        moduleConfig.mqtt.password[sizeof(moduleConfig.mqtt.password) - 1] = '\0';
        mqttDirty = true;
    }
    if (moduleConfig.mqtt.tls_enabled != desiredTls) {
        Serial.printf("[MonsterMesh] mqtt.tls_enabled canonicalized: %d -> %d\n",
                      (int)moduleConfig.mqtt.tls_enabled, (int)desiredTls);
        moduleConfig.mqtt.tls_enabled = desiredTls;
        mqttDirty = true;
    }
    if (strcmp(moduleConfig.mqtt.root, desiredRoot) != 0) {
        Serial.printf("[MonsterMesh] mqtt.root canonicalized: '%s' -> '%s'\n",
                      moduleConfig.mqtt.root, desiredRoot);
        strncpy(moduleConfig.mqtt.root, desiredRoot, sizeof(moduleConfig.mqtt.root) - 1);
        moduleConfig.mqtt.root[sizeof(moduleConfig.mqtt.root) - 1] = '\0';
        mqttDirty = true;
    }
    if (mqttDirty) {
        nodeDB->saveToDisk(SEGMENT_MODULECONFIG);
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
    // T4: peek at every TEXT_MESSAGE_APP DM addressed to us OR sent BY us.
    //   - inbound DM to us: parse Y/N reply to our outstanding mmt challenge,
    //     or detect an "MMB ON" / "battle in MonsterMesh" challenge phrase
    //     and arm the mmtChallengerPeer_ window so a matching
    //     TEXT_BATTLE_START can auto-launch the receiver-side battle.
    //   - outbound DM from us: if the user types "MMB ON" in their phone
    //     to a peer, arm mmtAwaitingReplyFrom_ so the peer's Y reply
    //     actually kicks off our side of the PvP. Without this hook, an
    //     "MMB ON" challenge sent from the phone (instead of the terminal
    //     `mmt @peer` command) silently went nowhere on the sender side.
    // handleReceived always returns CONTINUE so the standard text
    // pipeline still delivers the DM to the user's phone client.
    if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
        nodeDB && !isBroadcast(p->to)) {
        // Inbound DM to us.
        if (p->to == nodeDB->getNodeNum()) return true;
        // Outbound DM from us. Phone-originated packets arrive at
        // handleReceived with from=0 (not yet stamped to our node ID); by
        // the time they reach the router rebroadcast they have from=self.
        // Accept both so the "MMB ON" hook can see phone-typed DMs.
        if (p->from == 0 || p->from == nodeDB->getNodeNum()) return true;
    }
    // (BBS_REPLY arrives via PRIVATE_APP, already accepted above.)
    return false;
}

// Forward decls for the minimal-party PvP wire helpers (definitions below).
static void packPartyMin(const Gen1Party &src, uint8_t out[109]);
static void unpackPartyMin(const uint8_t in[109], Gen1Party &dst);

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
            //
            // Matches b237 behavior: any TEXT_BATTLE_START with the
            // correct size from another node auto-launches the receiver
            // side. Other-agent gauntlet/dungeon code that could emit
            // stray BATTLE_STARTs is compiled out of t-deck-tft via
            // MESHTASTIC_EXCLUDE_GAUNTLET / EXCLUDE_MONSTERMESH_DUNGEON,
            // so we don't need the mmtChallengerPeer_ window any more.
            if ((PktType)bp->type == PktType::TEXT_BATTLE_START &&
                mp.decoded.payload.size == BATTLELINK_HDR_SIZE + 14 &&
                mp.from != nodeDB->getNodeNum()) {
                LOG_INFO("[MonsterMesh] PvP: TEXT_BATTLE_START RX from 0x%08X "
                         "(tb=%d recv=%d init=%d) — gate check\n",
                         (unsigned)mp.from, (int)textBattleActive_,
                         (int)pendingMmtBattleAsReceiver_,
                         (int)pendingMmtBattleAsInitiator_);
                // Already-armed receiver path (e.g. self-armed via Y press
                // with fallback seed=1): UPDATE the seed/session from the
                // real START so both engines run identical RNG. Without
                // this the engines desync on the first turn.
                if (pendingMmtBattleAsReceiver_ &&
                    mp.from == mmtBattlePeer_ && !textBattleActive_) {
                    uint32_t newSeed = ((uint32_t)bp->payload[0] << 24) |
                                       ((uint32_t)bp->payload[1] << 16) |
                                       ((uint32_t)bp->payload[2] <<  8) |
                                                 bp->payload[3];
                    if (newSeed != mmtBattleSeed_) {
                        LOG_INFO("[MonsterMesh] PvP: receiver already armed; "
                                 "updating seed 0x%08X->0x%08X session->0x%04X\n",
                                 (unsigned)mmtBattleSeed_, (unsigned)newSeed,
                                 (unsigned)bp->sessionId());
                        mmtBattleSeed_    = newSeed;
                        mmtBattleSession_ = bp->sessionId();
                    }
                    return ProcessMessage::CONTINUE;
                }
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
                    // Arm direct party exchange — we owe sender our party,
                    // and we expect theirs. Reset reassembly state.
                    mmbPartyTxTarget_  = mp.from;
                    mmbPartyRxFrom_    = mp.from;
                    mmbOppPartyReady_  = false;
                    mmbPartyChunkMask_ = 0;
                    mmbPartyTotal_     = 0;
                    mmbPartyTxStartMs_ = millis();
                    mmbPartyTxLastMs_  = 0;
                    mmbPartyTxAttempts_ = 0;
                    LOG_INFO("[MonsterMesh] PvP: TEXT_BATTLE_START rx from 0x%08X seed=0x%08X (party exchange armed)\n",
                             (unsigned)mp.from, (unsigned)seed);
                } else {
                    // Stale receiver state from a prior failed attempt can wedge
                    // us into permanently ignoring new STARTs. If we have a
                    // pending receiver flag older than 45s and no chunks have
                    // arrived, reset and re-arm with this new START.
                    bool staleReceiver = pendingMmtBattleAsReceiver_ &&
                        mmtBattleReceivePendingMs_ != 0 &&
                        (millis() - mmtBattleReceivePendingMs_) > 45000 &&
                        !mmbOppPartyReady_;
                    if (staleReceiver) {
                        LOG_WARN("[MonsterMesh] PvP: clearing stale receiver "
                                 "state (%ums old), re-arming from new START\n",
                                 (unsigned)(millis() - mmtBattleReceivePendingMs_));
                        pendingMmtBattleAsReceiver_  = false;
                        pendingMmtBattleAsInitiator_ = false;
                        mmbPartyTxTarget_  = 0;
                        mmbPartyRxFrom_    = 0;
                        mmbOppPartyReady_  = false;
                        mmbPartyChunkMask_ = 0;
                        mmbPartyTotal_     = 0;
                        // Re-fire the arm path with this packet's seed/session.
                        uint32_t seed2 = ((uint32_t)bp->payload[0] << 24) |
                                         ((uint32_t)bp->payload[1] << 16) |
                                         ((uint32_t)bp->payload[2] <<  8) |
                                                   bp->payload[3];
                        mmtBattlePeer_    = mp.from;
                        mmtBattleSeed_    = seed2;
                        mmtBattleSession_ = bp->sessionId();
                        pendingMmtBattleAsReceiver_  = true;
                        mmtBattleReceivePendingMs_   = millis();
                        mmbPartyTxTarget_  = mp.from;
                        mmbPartyRxFrom_    = mp.from;
                        mmbPartyTxStartMs_ = millis();
                        mmbPartyTxLastMs_  = 0;
                        mmbPartyTxAttempts_ = 0;
                        LOG_INFO("[MonsterMesh] PvP: re-armed from new START "
                                 "seed=0x%08X\n", (unsigned)seed2);
                    } else {
                        LOG_WARN("[MonsterMesh] PvP: START gate BLOCKED "
                                 "tb=%d recv=%d init=%d — dropping\n",
                                 (int)textBattleActive_,
                                 (int)pendingMmtBattleAsReceiver_,
                                 (int)pendingMmtBattleAsInitiator_);
                    }
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

            // Server-authoritative PvP packets (0x66..0x6B). CHALLENGE may
            // arrive when no battle is active (it triggers the CLIENT
            // overlay); the other types must hit an active textBattle_
            // session. Self-echo filter prevents MQTT loopback (b402 fix).
            if (((PktType)bp->type == PktType::TEXT_BATTLE_UPDATE       ||
                 (PktType)bp->type == PktType::TEXT_BATTLE_ACTION_V2    ||
                 (PktType)bp->type == PktType::TEXT_BATTLE_CHALLENGE    ||
                 (PktType)bp->type == PktType::TEXT_BATTLE_ACCEPT       ||
                 (PktType)bp->type == PktType::TEXT_BATTLE_STATE_REQUEST||
                 (PktType)bp->type == PktType::TEXT_BATTLE_FULL_STATE)  &&
                mp.from != nodeDB->getNodeNum()) {
                textBattle_.handlePacket(mp.from,
                                          mp.decoded.payload.bytes,
                                          mp.decoded.payload.size);
                // If the CHALLENGE just transitioned us into the CLIENT
                // overlay, mark the module's battle-active gate so other
                // module-side guards (sleep, browser focus, etc.) treat
                // this like an in-progress battle.
                if (textBattle_.isActive()) textBattleActive_ = true;
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

            // MMB PvP party-exchange — TEXT_BATTLE_PARTY chunks of the peer's
            // current Gen1Party, sent point-to-point right after the handshake.
            // Engine launch is gated on mmbOppPartyReady_; we don't trust
            // daycare-beacon party data for the engine because identical SAVs
            // produce a self-fight and stale data can mismatch the current
            // party. Reassembled into mmbOppParty_.
            //
            // Self-arm path: TEXT_BATTLE_START is sent via channel encryption
            // on MonsterMesh (broadcast) and can be lost by the MQTT broker at
            // QoS 0. Chunks are PKI-DMs sent 5x and retransmitted, so they
            // survive much better. If a chunk arrives from a peer that DM'd us
            // a challenge in the last 10 minutes (mmtChallengerPeer_ used to
            // be 60s, but real users take longer to reply Y), treat that as
            // evidence the START was lost and AUTO-ARM the receiver path
            // right now. Without this, a lost START silently drops the entire
            // PvP attempt — the user sees nothing happen.
            if ((PktType)bp->type == PktType::TEXT_BATTLE_PARTY &&
                mmbPartyRxFrom_ == 0 && mmtChallengerPeer_ == mp.from &&
                !textBattleActive_ && !pendingMmtBattleAsReceiver_ &&
                !pendingMmtBattleAsInitiator_) {
                LOG_WARN("[MonsterMesh] PvP: chunk from challenger 0x%08X with "
                         "no armed session — assuming START was lost, "
                         "auto-arming receiver\n", (unsigned)mp.from);
                mmtBattlePeer_    = mp.from;
                mmtBattleSeed_    = 1;  // fallback seed; engine will desync if
                                        // real seed wasn't preserved, but
                                        // launching is better than silence
                mmtBattleSession_ = 0;
                pendingMmtBattleAsReceiver_  = true;
                mmtBattleReceivePendingMs_   = millis();
                mmbPartyTxTarget_  = mp.from;
                mmbPartyRxFrom_    = mp.from;
                mmbOppPartyReady_  = false;
                mmbPartyChunkMask_ = 0;
                mmbPartyTotal_     = 0;
                mmbPartyTxStartMs_ = millis();
                mmbPartyTxLastMs_  = 0;
                mmbPartyTxAttempts_ = 0;
            }
            // Mirror self-arm path: if we sent a challenge to this peer
            // (mmtAwaitingReplyFrom_ matches) and now they're sending us
            // chunks WITHOUT us ever receiving their Y, treat the chunks
            // themselves as implicit acceptance. Y reply over PKI/MQTT is
            // QoS 0 so it can be lost in transit — we observed exactly this
            // on 2026-05-20 (Blue's Y was never ACKed but Blue still
            // self-armed and started sending chunks). Symmetric to the
            // receiver self-arm above.
            if ((PktType)bp->type == PktType::TEXT_BATTLE_PARTY &&
                mmbPartyRxFrom_ == 0 && mmtAwaitingReplyFrom_ == mp.from &&
                !textBattleActive_ && !pendingMmtBattleAsReceiver_ &&
                !pendingMmtBattleAsInitiator_ && terminal_.hasParty()) {
                LOG_WARN("[MonsterMesh] PvP: chunk from awaited peer 0x%08X "
                         "but Y reply was never received — assuming Y was "
                         "lost, auto-arming initiator\n", (unsigned)mp.from);
                pendingMmtAccepted_   = true;
                pendingMmtAcceptedTx_ = true;
                mmtAcceptedTxTarget_  = mp.from;
                mmtAwaitingReplyFrom_ = 0;
                mmbPartyTxTarget_   = mp.from;
                mmbPartyRxFrom_     = mp.from;
                mmbOppPartyReady_   = false;
                mmbPartyChunkMask_  = 0;
                mmbPartyTotal_      = 0;
                mmbPartyTxStartMs_  = millis();
                mmbPartyTxLastMs_   = 0;
                mmbPartyTxAttempts_ = 0;
            }
            // Single-packet minimal party (PvP path). Same self-arm
            // gates as the chunked path: if we get one of these from an
            // unarmed challenger/awaited peer, treat it as implicit
            // session arming (START or Y was lost).
            if ((PktType)bp->type == PktType::TEXT_BATTLE_PARTY_MIN &&
                mmbPartyRxFrom_ == 0 && mmtChallengerPeer_ == mp.from &&
                !textBattleActive_ && !pendingMmtBattleAsReceiver_ &&
                !pendingMmtBattleAsInitiator_) {
                LOG_WARN("[MonsterMesh] PvP: PARTY_MIN from challenger 0x%08X "
                         "with no armed session — auto-arming receiver\n",
                         (unsigned)mp.from);
                mmtBattlePeer_    = mp.from;
                mmtBattleSeed_    = 1;
                mmtBattleSession_ = 0;
                pendingMmtBattleAsReceiver_  = true;
                mmtBattleReceivePendingMs_   = millis();
                mmbPartyTxTarget_  = mp.from;
                mmbPartyRxFrom_    = mp.from;
                mmbOppPartyReady_  = false;
                mmbPartyTxStartMs_ = millis();
                mmbPartyTxLastMs_  = 0;
                mmbPartyTxAttempts_ = 0;
            }
            if ((PktType)bp->type == PktType::TEXT_BATTLE_PARTY_MIN &&
                mmbPartyRxFrom_ == 0 && mmtAwaitingReplyFrom_ == mp.from &&
                !textBattleActive_ && !pendingMmtBattleAsReceiver_ &&
                !pendingMmtBattleAsInitiator_ && terminal_.hasParty()) {
                LOG_WARN("[MonsterMesh] PvP: PARTY_MIN from awaited peer "
                         "0x%08X — auto-arming initiator\n",
                         (unsigned)mp.from);
                pendingMmtAccepted_   = true;
                pendingMmtAcceptedTx_ = true;
                mmtAcceptedTxTarget_  = mp.from;
                mmtAwaitingReplyFrom_ = 0;
                mmbPartyTxTarget_   = mp.from;
                mmbPartyRxFrom_     = mp.from;
                mmbOppPartyReady_   = false;
                mmbPartyTxStartMs_  = millis();
                mmbPartyTxLastMs_   = 0;
                mmbPartyTxAttempts_ = 0;
            }
            if ((PktType)bp->type == PktType::TEXT_BATTLE_PARTY_MIN &&
                ((mmbPartyRxFrom_ != 0 && mp.from == mmbPartyRxFrom_) ||
                 (mmtChallengerPeer_ != 0 && mp.from == mmtChallengerPeer_))) {
                if (mp.decoded.payload.size < BATTLELINK_HDR_SIZE + 109) {
                    LOG_WARN("[MonsterMesh] PARTY_MIN too short: %u B\n",
                             (unsigned)mp.decoded.payload.size);
                    return ProcessMessage::CONTINUE;
                }
                // Session recovery: if our START was lost and we self-armed
                // (mmtBattleSession_=0), adopt the sender's session_id from
                // this packet so our ACTION/HASH packets pass the sender's
                // session filter. Without this, post-launch the engines
                // deadlock at "Waiting for opponent..." even though both
                // battle stations opened fine.
                uint16_t pktSession = bp->sessionId();
                if (mmtBattleSession_ == 0 && pktSession != 0) {
                    LOG_INFO("[MonsterMesh] PvP: adopting session 0x%04X "
                             "from PARTY_MIN (START was lost)\n",
                             (unsigned)pktSession);
                    mmtBattleSession_ = pktSession;
                }
                unpackPartyMin(bp->payload, mmbOppParty_);
                mmbOppPartyReady_  = true;
                mmbPartyRxFrom_    = 0;
                mmbPartyChunkMask_ = 0;
                mmbPartyTotal_     = 0;
                LOG_INFO("[MonsterMesh] MMB PARTY_MIN RX complete from "
                         "0x%08X count=%u session=0x%04X (1 pkt)\n",
                         (unsigned)mp.from, (unsigned)mmbOppParty_.count,
                         (unsigned)mmtBattleSession_);
                return ProcessMessage::CONTINUE;
            }

            if ((PktType)bp->type == PktType::TEXT_BATTLE_PARTY &&
                ((mmbPartyRxFrom_ != 0 && mp.from == mmbPartyRxFrom_) ||
                 (mmtChallengerPeer_ != 0 && mp.from == mmtChallengerPeer_))) {
                if (mp.decoded.payload.size < BATTLELINK_HDR_SIZE + 2)
                    return ProcessMessage::CONTINUE;
                // Lazy-allocate the chunk-assembly buffer in PSRAM the first
                // time we need it. Keeps it out of heap so emu task stack
                // alloc can succeed even after long runtime fragmentation.
                if (!mmbPartyChunks_) {
                    mmbPartyChunks_ = (uint8_t *)ps_malloc(MMB_PARTY_CHUNKS_BYTES);
                    if (!mmbPartyChunks_) {
                        LOG_WARN("[MonsterMesh] mmbPartyChunks PSRAM alloc failed\n");
                        return ProcessMessage::CONTINUE;
                    }
                    memset(mmbPartyChunks_, 0, MMB_PARTY_CHUNKS_BYTES);
                }
                uint8_t partIdx   = bp->payload[0];
                uint8_t partTotal = bp->payload[1];
                size_t  dataLen   = mp.decoded.payload.size -
                                    BATTLELINK_HDR_SIZE - 2;
                // Must match the sender's CHUNK in sendMmbPartyChunks().
                // 100-byte chunks keep each LoRa packet well under the
                // 237-byte encrypted limit so bidirectional exchange is
                // reliable on busy channels.
                const size_t CHUNK = 100;
                if (partTotal == 0 || partTotal > 8) return ProcessMessage::CONTINUE;
                if (partIdx >= partTotal)            return ProcessMessage::CONTINUE;
                size_t off = (size_t)partIdx * CHUNK;
                if (off + dataLen > MMB_PARTY_CHUNKS_BYTES)
                    return ProcessMessage::CONTINUE;
                memcpy(mmbPartyChunks_ + off, bp->payload + 2, dataLen);
                mmbPartyTotal_     = partTotal;
                uint8_t prevMask = mmbPartyChunkMask_;
                mmbPartyChunkMask_ |= (uint8_t)(1u << partIdx);

                uint8_t fullMask = (uint8_t)((1u << partTotal) - 1u);
                LOG_INFO("[MonsterMesh] MMB chunk RX from 0x%08X "
                         "idx=%u/%u len=%u mask=0x%02X->0x%02X full=0x%02X\n",
                         (unsigned)mp.from, (unsigned)partIdx,
                         (unsigned)partTotal, (unsigned)dataLen,
                         (unsigned)prevMask, (unsigned)mmbPartyChunkMask_,
                         (unsigned)fullMask);
                if ((mmbPartyChunkMask_ & fullMask) == fullMask) {
                    // All chunks in — parse into Gen1Party. Sender wrote
                    // it as raw memcpy(buf, &party, sizeof(Gen1Party)).
                    if (sizeof(mmbOppParty_) <= MMB_PARTY_CHUNKS_BYTES) {
                        memcpy(&mmbOppParty_, mmbPartyChunks_,
                               sizeof(mmbOppParty_));
                        mmbOppPartyReady_ = true;
                        LOG_INFO("[MonsterMesh] MMB party RX complete from 0x%08X "
                                 "count=%u (%u chunks)\n",
                                 (unsigned)mp.from, (unsigned)mmbOppParty_.count,
                                 (unsigned)partTotal);
                    }
                    mmbPartyRxFrom_ = 0;  // done collecting
                    // Reset the chunk-assembly buffer so the NEXT session starts
                    // clean. Without this, mmbPartyChunkMask_ stays at fullMask
                    // and the first chunk of the next battle short-circuits the
                    // gate, memcpy'ing 4/5ths of the prior opponent's party
                    // into mmbOppParty_ — symptom: PvP fights launch with
                    // "wrong" or "missing" pokemon from the previous fight.
                    mmbPartyChunkMask_ = 0;
                    mmbPartyTotal_     = 0;
                    if (mmbPartyChunks_)
                        memset(mmbPartyChunks_, 0, MMB_PARTY_CHUNKS_BYTES);
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
                    // Reciprocal beacon: ask runOnce to broadcast our own
                    // beacon so the new peer learns about us within seconds
                    // instead of waiting up to 15 min for our next periodic
                    // BEACON_INTERVAL_MS broadcast.
                    pendingReplyBeacon_ = true;
                }
                // MQTT-only response: if this is a user-triggered beacon
                // (boot/manual), echo a beacon + NodeInfo back via MQTT
                // only so the requester populates their neighbor list
                // without every deck slamming LoRa.
                if (beacon->requestResponse) {
                    pendingMqttResponseTo_ = beacon->nodeId;
                    LOG_INFO("[MonsterMesh] beacon requestResponse=1 → queueing MQTT-only reply to 0x%08X\n",
                             (unsigned)beacon->nodeId);
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

    // T4: detect an INCOMING MMT challenge DM. Two phrases accepted:
    //   1. "battle in MonsterMesh"  — sent by the terminal `mmt @peer` command
    //      via sendTextDM ("Do you want to battle in MonsterMesh? Reply Y or N.")
    //   2. "MMB ON"  (case-insensitive) — user-friendly shorthand typed
    //      directly into the phone DM chat. Lets you initiate a fight
    //      without opening the on-deck terminal.
    // Either phrase arms a 10-minute window for the matching TEXT_BATTLE_START
    // to auto-launch the receiver-side battle. Without this gate we'd take
    // any 0x60 packet of the right size as a battle invite (and other-agent
    // gauntlet/dungeon code emits stray ones).
    auto containsIgnoreCase = [](const char *hay, size_t hlen,
                                 const char *ndl, size_t nlen) -> bool {
        if (nlen == 0 || hlen < nlen) return false;
        for (size_t i = 0; i + nlen <= hlen; ++i) {
            size_t j = 0;
            for (; j < nlen; ++j) {
                char a = hay[i + j];
                char b = ndl[j];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) break;
            }
            if (j == nlen) return true;
        }
        return false;
    };
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
        mp.from != 0 && mp.from != nodeDB->getNodeNum()) {
        const char *txt = (const char *)mp.decoded.payload.bytes;
        size_t      len = mp.decoded.payload.size;
        bool isChallenge =
            containsIgnoreCase(txt, len, "battle in MonsterMesh", 21) ||
            containsIgnoreCase(txt, len, "MMB ON", 6);
        if (isChallenge) {
            mmtChallengerPeer_     = mp.from;
            // 10-minute window so user has reasonable time to type Y.
            // Originally 60s, but real chat reaction time + DeviceUI
            // input lag routinely exceeded that and the resulting
            // chunks were silently dropped (observed b358, 4+min Y).
            mmtChallengerExpireMs_ = millis() + 600000;
            LOG_INFO("[MonsterMesh] mmt: challenge DM from 0x%08X — armed 10min\n",
                     (unsigned)mp.from);
        }
    }
    // (Outbound "MMB ON" sniff lives in sniffPhoneOutboundDM, called from
    //  MeshService::handleToRadio. Unicast outbound DMs from the phone never
    //  reach this function — Router::sendLocal only re-dispatches broadcasts
    //  through the module pipeline. See feedback_mm_unicast_tx_module_blind.md.)

    // T4: parse the peer's Y/N reply to our outstanding challenge.
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        // Diagnostic: log EVERY text DM the module sees, with the awaiting
        // state. This is how we tell whether a Y reply reached us at all
        // vs. silently dropped on the wire / filtered by wantPacket.
        const char *txt = (const char *)mp.decoded.payload.bytes;
        size_t      len = mp.decoded.payload.size;
        char preview[24] = {};
        size_t cp = (len < sizeof(preview) - 1) ? len : sizeof(preview) - 1;
        memcpy(preview, txt, cp);
        for (size_t k = 0; k < cp; ++k) {
            if ((uint8_t)preview[k] < 0x20 || (uint8_t)preview[k] > 0x7E)
                preview[k] = '.';
        }
        LOG_INFO("[MonsterMesh] mmt RX text DM: fr=0x%08X len=%u await=0x%08X '%s'\n",
                 (unsigned)mp.from, (unsigned)len,
                 (unsigned)mmtAwaitingReplyFrom_, preview);

        if (mmtAwaitingReplyFrom_ != 0 && mp.from == mmtAwaitingReplyFrom_) {
            // Skip ANY non-printable / non-letter prefix bytes. Meshtastic's
            // DeviceUI chat panel sometimes prepends 0xFF (or other control
            // bytes — possibly a broken emoji shim) before the user's text,
            // which made our previous "Y" parse pick up the 0xFF instead of
            // the actual letter. Walk forward to the first ASCII letter and
            // use that as the verdict char.
            size_t i = 0;
            while (i < len) {
                char c = txt[i];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) break;
                ++i;
            }
            char first = (i < len) ? txt[i] : '\0';
            LOG_INFO("[MonsterMesh] mmt: reply first char='%c' (0x%02X)\n",
                     first ? first : '?', (unsigned)first);
            if (first == 'Y' || first == 'y') {
                pendingMmtAccepted_   = true;
                pendingMmtAcceptedTx_ = true;
                mmtAcceptedTxTarget_  = mp.from;
                mmtAwaitingReplyFrom_ = 0;
                // Arm direct party exchange — we owe peer our party, and we
                // expect their party in chunks. Reset reassembly state.
                mmbPartyTxTarget_   = mp.from;
                mmbPartyRxFrom_     = mp.from;
                mmbOppPartyReady_   = false;
                mmbPartyChunkMask_  = 0;
                mmbPartyTotal_      = 0;
                mmbPartyTxStartMs_  = millis();
                mmbPartyTxLastMs_   = 0;
                mmbPartyTxAttempts_ = 0;
                LOG_INFO("[MonsterMesh] mmt: ACCEPT staged from 0x%08X (party exchange armed)\n",
                         (unsigned)mp.from);
            } else if (first == 'N' || first == 'n') {
                pendingMmtDeclined_   = true;
                mmtAwaitingReplyFrom_ = 0;
            }
        }
        // Always CONTINUE so the standard chat pipeline still delivers
        // the reply DM to the user's phone app.
        return ProcessMessage::CONTINUE;
    }
    // Fall-through default: CONTINUE so any packet that reached us (e.g.
    // a normal text DM addressed to us, which wantPacket now accepts so
    // we can sniff for an incoming MMT challenge phrase) still flows to
    // the standard chat / DeviceUI pipeline. STOP here would silently
    // drop incoming DMs and the user would never see them in chat.
    return ProcessMessage::CONTINUE;
}

void MonsterMeshModule::onLocalYReply()
{
    // Only react if a challenger window is armed AND we're not already in a
    // PvP session. Either the user actually replied Y, or they typed a stray
    // Y outside the challenge window (in which case mmtChallengerPeer_ is 0
    // and we drop out silently).
    if (mmtChallengerPeer_ == 0) return;
    if (mmtChallengerExpireMs_ != 0 &&
        (int32_t)(millis() - mmtChallengerExpireMs_) >= 0) {
        LOG_INFO("[MonsterMesh] onLocalYReply: challenger window expired\n");
        return;
    }
    if (textBattleActive_ || pendingMmtBattleAsReceiver_ ||
        pendingMmtBattleAsInitiator_) {
        return;  // already armed somehow
    }
    LOG_INFO("[MonsterMesh] onLocalYReply: arming as RECEIVER vs 0x%08X "
             "(local Y detected before START arrives)\n",
             (unsigned)mmtChallengerPeer_);
    mmtBattlePeer_    = mmtChallengerPeer_;
    mmtBattleSeed_    = 1;          // fallback seed; overwritten when real
                                    // TEXT_BATTLE_START packet arrives
    mmtBattleSession_ = 0;
    pendingMmtBattleAsReceiver_  = true;
    mmtBattleReceivePendingMs_   = millis();
    mmbPartyTxTarget_  = mmtChallengerPeer_;
    mmbPartyRxFrom_    = mmtChallengerPeer_;
    mmbOppPartyReady_  = false;
    mmbPartyChunkMask_ = 0;
    mmbPartyTotal_     = 0;
    mmbPartyTxStartMs_ = millis();
    mmbPartyTxLastMs_  = 0;
    mmbPartyTxAttempts_ = 0;
}

// publishMqttOnlyBroadcast — publish a broadcast packet to the MQTT broker
// ONLY. Does NOT touch the LoRa interface.
//
// CRITICAL — DO NOT REPLACE THIS WITH service->sendToMesh OR router->send.
// Both of those go through Router::send → iface->send and fan the packet out
// over LoRa airtime as well. The whole point of the beacon-response feature
// is that N decks can quietly ack a remote beacon via the broker without
// every one of them slamming the LoRa airwaves. Routing the response
// through the normal mesh send path defeats the design and turns one
// requester beacon into N×LoRa TX bursts — exactly what we're avoiding.
//
// Hot rules:
//   - Anything that flows from a `requestResponse=1` daycare beacon MUST
//     land here, never on service->sendToMesh / router->send.
//   - If you need to send PvP or DM traffic that does require LoRa
//     delivery, use the normal send path. That's a different design with
//     different airtime semantics.
//   - Keep the throttle at the call-site (currently 30 s/peer) so even a
//     misbehaving peer flooding request beacons can't spam the broker.
void MonsterMeshModule::publishMqttOnlyBroadcast(meshtastic_MeshPacket *p)
{
#if !MESHTASTIC_EXCLUDE_MQTT
    if (!p) return;
    if (!mqtt || !moduleConfig.mqtt.enabled) {
        LOG_WARN("[MonsterMesh] mqtt-only TX: MQTT disabled — dropping pkt portnum=%d\n",
                 (int)p->decoded.portnum);
        packetPool.release(p);
        return;
    }
    if (p->from == 0) p->from = nodeDB->getNodeNum();
    ChannelIndex chIndex = p->channel;
    // perhapsEncode mutates p (decoded → encrypted), so snapshot a decoded
    // copy first for mqtt->onSend which wants both.
    meshtastic_MeshPacket *pDecoded = packetPool.allocCopy(*p);
    auto encResult = perhapsEncode(p);
    if (encResult != meshtastic_Routing_Error_NONE) {
        LOG_WARN("[MonsterMesh] mqtt-only encode failed: %d\n", (int)encResult);
        packetPool.release(pDecoded);
        packetPool.release(p);
        return;
    }
    // mqtt->onSend publishes to the broker only. We deliberately do NOT call
    // iface->send / router->send here — see the function-level note above.
    mqtt->onSend(*p, *pDecoded, chIndex);
    LOG_INFO("[MonsterMesh] mqtt-only TX: portnum=%d ch=%u from=0x%08X to=0x%08X\n",
             (int)pDecoded->decoded.portnum, (unsigned)chIndex,
             (unsigned)p->from, (unsigned)p->to);
    packetPool.release(pDecoded);
    packetPool.release(p);
#else
    if (p) packetPool.release(p);
#endif
}

void MonsterMeshModule::sniffPhoneOutboundDM(meshtastic_MeshPacket *p)
{
    if (!p || !nodeDB) return;
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) return;
    if (p->decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) return;
    if (isBroadcast(p->to) || p->to == 0 || p->to == nodeDB->getNodeNum()) return;
    const char *txt = (const char *)p->decoded.payload.bytes;
    size_t      len = p->decoded.payload.size;
    if (!mmContainsIgnoreCase(txt, len, "MMB ON", 6)) return;
    if (mmtAwaitingReplyFrom_ == p->to) return; // already armed for this peer
    mmtAwaitingReplyFrom_ = p->to;
    mmtOnTxTarget_        = p->to;
    pendingMmtOnTx_       = true;
    LOG_INFO("[MonsterMesh] mmt: phone OUT MMB ON -> 0x%08X — armed + queuing challenge DM\n",
             (unsigned)p->to);
}

void MonsterMeshModule::challengePeerByShortName(const char *peerShort)
{
    if (!nodeDB || !peerShort || !peerShort[0]) {
        terminal_.printLine("mmb: empty target");
        return;
    }
    // Nuke any stuck PvP state from a prior failed handshake so a fresh
    // `mmb <peer>` always restarts cleanly. Without this, a stale
    // pendingMmtBattleAsInitiator_ from a previous attempt blocks the new
    // session and the user has no way to recover short of a reboot.
    if (pendingMmtBattleAsInitiator_ || pendingMmtBattleAsReceiver_ ||
        mmbPartyTxTarget_ || mmbPartyRxFrom_) {
        LOG_INFO("[MonsterMesh] mmb: clearing prior PvP state before new challenge "
                 "(init=%d recv=%d txTgt=0x%08X)\n",
                 (int)pendingMmtBattleAsInitiator_,
                 (int)pendingMmtBattleAsReceiver_,
                 (unsigned)mmbPartyTxTarget_);
        pendingMmtBattleAsInitiator_ = false;
        pendingMmtBattleAsReceiver_  = false;
        mmbPartyTxTarget_   = 0;
        mmbPartyRxFrom_     = 0;
        mmbOppPartyReady_   = false;
        mmbPartyChunkMask_  = 0;
        mmbPartyTotal_      = 0;
        mmbPartyTxStartMs_  = 0;
        mmbPartyTxLastMs_   = 0;
        mmbPartyTxAttempts_ = 0;
        mmtBattleReceivePendingMs_ = 0;
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
                 "mmb: no node '%s' in NodeDB. Try after they NodeInfo.",
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
    // Force a beacon broadcast right now so the peer's daycare neighbor
    // table has our latest party summary by the time they reply Y and the
    // TEXT_BATTLE_START arrives. Without this, the peer may run a mirror
    // match because their `buildPartyFromNeighbor` lookup found no entry
    // for our node ID. Beacons are cheap (~150 bytes broadcast) and the
    // BEACON_INTERVAL_MS is 15 min — too slow for ad-hoc challenges.
    if (daycare_.isActive()) {
        daycare_.forceBeacon();
        LOG_INFO("[MonsterMesh] mmb: forced beacon so peer has our party data\n");
    }
}

// Server-authoritative challenge — terminal command `mmb2 <short>`.
// Resolves the peer + stages the request; the actual engine start +
// LVGL flush_cb swap + screen clear happen later in runOnce on the LoRa
// thread. Running them inline from this context (terminal input → LVGL
// thread) deadlocks against the LVGL render lock and trips the t-deck
// watchdog (observed: deck freezes mid-print right after the first
// "[MMB] server-auth CHALLENGE" log line).
void MonsterMeshModule::challengePeerByShortNameV2(const char *peerShort)
{
    if (!terminal_.hasParty()) {
        terminal_.printLine("mmb2: no party loaded — load a SAV first");
        return;
    }
    if (textBattleActive_) {
        terminal_.printLine("mmb2: already in a battle");
        return;
    }
    if (pendingMmb2Initiator_) {
        terminal_.printLine("mmb2: challenge already pending");
        return;
    }
    size_t total = nodeDB->getNumMeshNodes();
    uint32_t resolved = 0;
    char matchedShort[12] = {};
    for (size_t i = 0; i < total; ++i) {
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || !n->has_user) continue;
        if (n->num == nodeDB->getNodeNum()) continue;
        if (strcasecmp(n->user.short_name, peerShort) == 0) {
            resolved = n->num;
            strncpy(matchedShort, n->user.short_name, sizeof(matchedShort) - 1);
            break;
        }
    }
    if (resolved == 0) {
        char buf[80];
        snprintf(buf, sizeof(buf),
                 "mmb2: no node '%s' in NodeDB. Try after they NodeInfo.",
                 peerShort);
        terminal_.printLine(buf);
        return;
    }
    pendingMmb2Target_ = resolved;
    strncpy(pendingMmb2PeerShort_, matchedShort,
            sizeof(pendingMmb2PeerShort_) - 1);
    pendingMmb2PeerShort_[sizeof(pendingMmb2PeerShort_) - 1] = '\0';
    pendingMmb2Initiator_ = true;
    char buf[80];
    snprintf(buf, sizeof(buf), "mmb2: queued challenge → %s (server-auth)",
             matchedShort);
    terminal_.printLine(buf);
    LOG_INFO("[MonsterMesh] mmb2: queued v2-challenge for %s = 0x%08X\n",
             matchedShort, (unsigned)resolved);
}

// Send TEXT_BATTLE_START directly from the module (not via textBattle's
// internal sendStart) so the receiver can arm its party-RX state machine
// before our chunks arrive. The seed lives in mmtBattleSeed_ on both
// sides so the actual engine.start (deferred until party exchange
// completes) uses the same value.
void MonsterMeshModule::sendMmbBattleStart(uint32_t seed)
{
    if (!router || !service || !mmtBattlePeer_) return;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_WARN("[MonsterMesh] mmt: allocForSending(START) failed\n");
        return;
    }
    p->to = NODENUM_BROADCAST;  // BATTLE_START is broadcast on MM channel
    p->channel = mmChannel_;  // MonsterMesh channel — MQTT-bridged (b345)
    p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    BattlePacket *bp = (BattlePacket *)p->decoded.payload.bytes;
    memset(bp, 0, BATTLELINK_HDR_SIZE + 14);
    bp->type = (uint8_t)PktType::TEXT_BATTLE_START;
    // Record the session_id we use for THIS battle so the engine can use
    // the same value when we later call startNetworkedAsInitiator —
    // otherwise sender's outgoing ACTION packets (with engine session_)
    // mismatch what the receiver captured from this START packet and get
    // dropped on receipt. Same value flows: module session var → START
    // packet → receiver captures from packet → both engines pin to it.
    mmtBattleSession_ = (uint16_t)(millis() & 0xFFFF);
    bp->setSessionId(mmtBattleSession_);
    bp->seq = 0;
    bp->payload[0] = (seed >> 24) & 0xFF;
    bp->payload[1] = (seed >> 16) & 0xFF;
    bp->payload[2] = (seed >> 8)  & 0xFF;
    bp->payload[3] =  seed        & 0xFF;
    bp->payload[4] = 1;  // gen
    bp->payload[5] = 0;  // party count placeholder; real party comes via chunks
    p->decoded.payload.size = BATTLELINK_HDR_SIZE + 14;
    service->sendToMesh(p);
    LOG_INFO("[MonsterMesh] mmt: sent TEXT_BATTLE_START seed=0x%08X session=0x%04X\n",
             (unsigned)seed, (unsigned)mmtBattleSession_);
}

// Pack a Gen1Party into the 109-byte minimal PvP wire format. Strips fields
// the engine can derive at fresh-battle start: OT names, nicknames (battle
// UI falls back to species name), current HP / PP / status (all reset to
// healed-and-zero), and the redundant species[] header (already in mons[].
// species). Per-mon layout = species(1) | level(1) | dvs[2] | hpExp[2] |
// atkExp[2] | defExp[2] | spdExp[2] | spcExp[2] | moves[4] = 18 bytes.
// Total = 1 (count) + 6*18 = 109 B, fits in one PRIVATE_APP packet.
static void packPartyMin(const Gen1Party &src, uint8_t out[109])
{
    out[0] = src.count;
    for (uint8_t i = 0; i < 6; ++i) {
        uint8_t *p = out + 1 + (size_t)i * 18;
        const Gen1Pokemon &m = src.mons[i];
        p[0] = m.species;
        p[1] = m.level ? m.level : m.boxLevel;
        p[2] = m.dvs[0];
        p[3] = m.dvs[1];
        memcpy(p +  4, m.hpExp,  2);
        memcpy(p +  6, m.atkExp, 2);
        memcpy(p +  8, m.defExp, 2);
        memcpy(p + 10, m.spdExp, 2);
        memcpy(p + 12, m.spcExp, 2);
        memcpy(p + 14, m.moves,  4);
    }
}

// Inverse of packPartyMin. Reconstructs a Gen1Party with zeroed names,
// current HP=0 (initBattlePokeFromSave falls through to maxHp), status=0,
// and PP filled from the canonical move table so initBattlePokeFromSave's
// memcpy(dst.pp, src.pp, 4) copies the right values. species[] header is
// rebuilt from mons[].species for any consumer that still inspects it.
static void unpackPartyMin(const uint8_t in[109], Gen1Party &dst)
{
    memset(&dst, 0, sizeof(dst));
    dst.count = in[0];
    if (dst.count > 6) dst.count = 6;
    for (uint8_t i = 0; i < 6; ++i) {
        const uint8_t *p = in + 1 + (size_t)i * 18;
        Gen1Pokemon &m = dst.mons[i];
        m.species  = p[0];
        m.level    = p[1];
        m.boxLevel = p[1];
        m.dvs[0]   = p[2];
        m.dvs[1]   = p[3];
        memcpy(m.hpExp,  p +  4, 2);
        memcpy(m.atkExp, p +  6, 2);
        memcpy(m.defExp, p +  8, 2);
        memcpy(m.spdExp, p + 10, 2);
        memcpy(m.spcExp, p + 12, 2);
        memcpy(m.moves,  p + 14, 4);
        for (uint8_t k = 0; k < 4; ++k) {
            const Gen1MoveData *md = gen1Move(m.moves[k]);
            m.pp[k] = md ? md->pp : 0;
        }
    }
    for (uint8_t i = 0; i < 7; ++i) {
        dst.species[i] = (i < dst.count) ? dst.mons[i].species : 0xFF;
    }
}

// Send our current party to a peer as a single TEXT_BATTLE_PARTY_MIN packet
// (point-to-point on the MonsterMesh channel). Replaces the older 5×100B /
// 3×200B chunked TEXT_BATTLE_PARTY exchange for PvP — drops payload from
// ~404 B (multi-packet) to a single 113 B PRIVATE_APP frame. Receiver path
// in handleReceived unpacks directly into mmbOppParty_ and sets
// mmbOppPartyReady_ in one shot. SAFE to call from runOnce/LoRa thread.
void MonsterMeshModule::sendMmbPartyChunks(uint32_t to, const Gen1Party &party)
{
    if (!to || !router || !service) return;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_WARN("[MonsterMesh] MMB party TX: allocForSending failed\n");
        return;
    }
    p->to = to;
    p->channel = mmChannel_;
    p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    BattlePacket *bp = (BattlePacket *)p->decoded.payload.bytes;
    memset(bp, 0, BATTLELINK_HDR_SIZE);
    bp->type = (uint8_t)PktType::TEXT_BATTLE_PARTY_MIN;
    // Stamp the live battle session into the packet. If the START packet
    // was lost over MQTT, the receiver self-arms with session=0 and would
    // otherwise emit ACTION/HASH packets with a session that mismatches
    // ours — sender drops them as stale, never sets peerReady_, blocks at
    // "Waiting for opponent..." forever. Carrying the session in every
    // PARTY_MIN burst lets the receiver recover it on the first chunk
    // that lands (PARTY_MIN is retransmitted on a timer until oppReady).
    bp->setSessionId(mmtBattleSession_);
    bp->seq = 0;
    packPartyMin(party, bp->payload);
    p->decoded.payload.size = BATTLELINK_HDR_SIZE + 109;
    LOG_INFO("[MonsterMesh] MMB party TX → 0x%08X count=%u session=0x%04X "
             "(min, 1 pkt, %u B payload)\n",
             (unsigned)to, (unsigned)party.count,
             (unsigned)mmtBattleSession_,
             (unsigned)p->decoded.payload.size);
    service->sendToMesh(p);
}

void MonsterMeshModule::sendTextDM(uint32_t to, const char *text)
{
    if (!text) return;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) return;
    p->to = to;
    p->channel = mmChannel_;  // MonsterMesh channel — MQTT-bridged (b345)
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

        // Pre-allocate emu + render task stacks NOW while the heap is fresh.
        // If we wait until the first ROM launch (a few minutes in), heap
        // fragmentation makes the 16KB emu stack alloc fail silently (we've
        // seen free=28KB but largest=13KB) and the ROM "loads" with no task
        // to blit frames. Tasks idle on emulatorActive_=false until needed.
        if (!emuTaskHandle_) {
            BaseType_t r = xTaskCreatePinnedToCore(
                emuTaskEntry, "monstermesh_emu",
                16384, this, 5, &emuTaskHandle_, 1
            );
            if (r != pdPASS || !emuTaskHandle_) {
                LOG_ERROR("[MonsterMesh] boot emu task spawn FAILED (r=%d) "
                          "free=%u largest=%u\n",
                          (int)r,
                          (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getMaxAllocHeap());
            } else {
                LOG_INFO("[MonsterMesh] boot emu task spawned (handle=%p) "
                         "free=%u largest=%u\n",
                         (void *)emuTaskHandle_,
                         (unsigned)ESP.getFreeHeap(),
                         (unsigned)ESP.getMaxAllocHeap());
            }
        }
        if (!renderTaskHandle_) {
            BaseType_t r = xTaskCreatePinnedToCore(
                renderTaskEntry, "monstermesh_render",
                4096, this, 2, &renderTaskHandle_, 0
            );
            if (r == pdPASS && renderTaskHandle_) {
                LOG_INFO("[MonsterMesh] boot render task spawned (handle=%p)\n",
                         (void *)renderTaskHandle_);
            }
        }

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
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                // Manual beacon → ask peers to respond MQTT-only.
                self->nextBeaconRequestsResponse_ = true;
                self->daycare_.forceBeacon();
                // Also re-emit NodeInfo on the MM channel so peers across
                // MQTT pick up our pubkey + short_name without waiting
                // for the 15-min periodic refresh. User-triggered
                // beacon → user expects immediate visibility.
                if (nodeInfoModule) {
                    nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST,
                                                     false,
                                                     self->mmChannel_);
                    LOG_INFO("[MonsterMesh] beacon cmd: also sent NodeInfo on MM ch %u\n",
                             (unsigned)self->mmChannel_);
                }
            }, this);
        terminal_.setMmtListFn(
            [](void *ctx, char *buf, size_t n) {
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                if (!buf || n == 0) return;
                size_t off = 0;
                #define MMT_APPEND(...) do { \
                    if (off < n - 1) { \
                        int _w = snprintf(buf + off, n - off, __VA_ARGS__); \
                        if (_w > 0) off += (size_t)_w; \
                        if (off >= n) off = n - 1; \
                    } \
                } while (0)
                uint8_t nc = self->daycare_.getNeighborCount();
                if (nc == 0) {
                    MMT_APPEND("No peers in range. Have them open MM and beacon.\n");
                } else {
                    MMT_APPEND("Online peers (last beacon):\n");
                    const auto *neigh = self->daycare_.getNeighbors();
                    uint8_t shown = 0;
                    for (uint8_t i = 0; i < nc && i < 6; ++i) {
                        // Fall back to NodeDB's short_name if the daycare
                        // beacon arrived before the peer had its own owner
                        // info loaded (beacon shortName would be empty
                        // → "?"). Skip the entry entirely if NEITHER source
                        // has a name — that means we haven't received the
                        // peer's nodeinfo yet either, so any HB row we'd
                        // print would be "?/?" garbage.
                        const char *sn = neigh[i].shortName[0] ? neigh[i].shortName : nullptr;
                        if (!sn && nodeDB) {
                            auto *node = nodeDB->getMeshNode(neigh[i].nodeId);
                            if (node && node->has_user && node->user.short_name[0]) {
                                sn = node->user.short_name;
                            }
                        }
                        if (!sn) continue;  // wait for both beacon + nodeinfo
                        // Peers without a loaded party can't battle, so skip
                        // them rather than render as "SN/?" — keeps the HB
                        // list to actionable opponents only.
                        if (neigh[i].partyCount == 0) continue;
                        const char *gn = neigh[i].gameName[0] ? neigh[i].gameName : "?";
                        if (neigh[i].ngPlusTier > 0) {
                            MMT_APPEND("  %s/%s NG+%u\n",
                                       sn, gn, (unsigned)neigh[i].ngPlusTier);
                        } else {
                            MMT_APPEND("  %s/%s\n", sn, gn);
                        }
                        shown++;
                    }
                    if (shown == 0) {
                        MMT_APPEND("(waiting for peer nodeinfo...)\n");
                    }
                    MMT_APPEND("\nUsage: mmb <short_name>\n");
                }
                buf[off] = '\0';
                #undef MMT_APPEND
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
        terminal_.setMmb2ChallengeFn(
            [](void *ctx, const char *peerShort) {
                static_cast<MonsterMeshModule *>(ctx)
                    ->challengePeerByShortNameV2(peerShort);
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
            DaycareBeacon beacon = beaconIn;
            beacon.nodeId     = nodeDB->getNodeNum();
            beacon.ngPlusTier = lordCurrentNgPlusTier();
            // Short-name fallback: if owner.short_name isn't customized
            // (factory-fresh deck), derive a name from the NodeNum so the
            // receiver still has a human-readable handle to use in the
            // neighbor list. Format "T-XX" where XX = last 2 hex digits of
            // NodeNum, leaves room for the null terminator in [5].
            if (beacon.shortName[0] == '\0') {
                snprintf(beacon.shortName, sizeof(beacon.shortName),
                         "T-%02X", (unsigned)(beacon.nodeId & 0xFFu));
            }
            // User-triggered beacons (boot, manual `beacon`/`bc`) ask peers
            // to reply MQTT-only. Cleared after one beacon goes out so the
            // next periodic auto-beacon doesn't carry the request flag.
            MonsterMeshModule *mod = static_cast<MonsterMeshModule *>(ctx);
            beacon.requestResponse = (mod && mod->nextBeaconRequestsResponse_) ? 1 : 0;
            if (mod) mod->nextBeaconRequestsResponse_ = false;
            meshtastic_MeshPacket *p = router->allocForSending();
            if (!p) {
                LOG_WARN("[MonsterMesh] daycare beacon: packet alloc failed\n");
                return;
            }
            p->to = NODENUM_BROADCAST;
            // Beacons used to go on the primary channel (LongFast etc.)
            // for visibility on the public mesh. That made them invisible
            // between two decks whose primaries diverge. Send on the
            // MonsterMesh channel instead — every node flashed with this
            // firmware has it (b343 auto-provisions), so the daycare
            // neighbor table actually populates.
            MonsterMeshModule *self = static_cast<MonsterMeshModule *>(ctx);
            p->channel = self ? self->mmChannel_ : MONSTERMESH_CHANNEL;
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
                if (altNow && !altWas && altSeenLow && (now - g_lastAltFireMs > 250)) {
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
        // Terminal re-skin happens on next terminal_.open() (LVGL thread).
        // Calling applyTheme() here would touch LVGL widgets from the LoRa
        // thread and race the LVGL render thread → chat panel deadlock.
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
        // One-shot channel provision now that channels.* is fully ready.
        // Adds "MonsterMesh" channel (PSK "MonsterMesh!2024", MQTT bridge
        // on) to the first free slot if no channel by that name exists.
        // Never overwrites a user-configured slot.
        ensureMonsterMeshChannel();
    }

    // Deferred SAV save FIRST, before we touch the radio. Saving uses SD
    // (shared SPI bus) and we want it complete + spiLock released cleanly
    // before startReceive starts driving the SX1262. Trying to re-arm the
    // radio while a 32KB SD write is mid-flight was freezing the device.
    if (pendingSave_) {
        pendingSave_ = false;
        uint32_t saveStart = millis();
        LOG_INFO("[MonsterMesh] deferred SAV save: start\n");
        emu_.save();
        LOG_INFO("[MonsterMesh] deferred SAV save: done (%ums)\n",
                 (unsigned)(millis() - saveStart));
    }

    // Radio + WiFi state sync on the LoRa thread. LVGL thread only flips
    // radioParked_/radioNeedsRx_; we reconcile here so LVGL stays snappy.
    //
    // Two-step re-arm: first force the chip into a known state via
    // setStandby() (which calls checkNotification + lora.standby +
    // disableInterrupt), wait briefly for the chip to settle, then call
    // startReceive(). Diag in b330 showed calling startReceive directly
    // after emu exit hung the device — chip was likely in a state where
    // setStandby inside startReceive couldn't make progress. Doing it as
    // a separate step + delay between gives the chip time to flush.
    if (radioNeedsRx_) {
        radioNeedsRx_ = false;
        // No-op: enterEmulatorMode no longer calls disableInterrupt, so
        // the radio is still in RX continuously during emu. g_meshSuspended
        // drops packets at the receivePacket level for the duration. On
        // exit we just clear g_meshSuspended (in exitEmulatorMode) and
        // packets flow normally again — no startReceive needed.
        LOG_INFO("[MonsterMesh] sync: radio already RX'ing (no-op re-arm)\n");
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

    // ── Deferred ALT-to-browser activation ────────────────────────────────
    // LVGL thread set pendingBrowserActivate_; we do all the heavy stuff
    // here on the LoRa thread (spiLock-guarded fillScreen, flush_cb swap,
    // radio park) so the LVGL thread doesn't deadlock against in-progress
    // DeviceUI render work (250-node restore, chat-history flood).
    // Direct emu resume — ALT in Meshtastic with a ROM already loaded.
    // Skip the file browser entirely; just swap flush_cb to no-op,
    // wipe screen, park radios via enterEmulatorMode, flip emulatorActive_
    // so the pre-allocated emu task wakes from its idle loop.
    if (pendingEmuResume_ && !emulatorActive_ && !browserActive_ &&
        !textBattleActive_ && setupDone_ && emuInitialized_) {
        pendingEmuResume_ = false;
        LOG_INFO("[MonsterMesh] direct emu resume (ROM already loaded)\n");
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && !savedFlushCb_) {
            savedFlushCb_ = (void *)disp->flush_cb;
            lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                lv_display_flush_ready(d);
            });
        }
        if (g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            g_deviceUiLgfx->clearClipRect();
            g_deviceUiLgfx->fillScreen(0x0000);
        }
#endif
        enterEmulatorMode();
        emulatorActive_ = true;
        kbSetMode(true);
        setupStatus_ = "Playing!";
    }

    if (pendingBrowserActivate_ && !browserActive_ && !emulatorActive_ &&
        !textBattleActive_ && setupDone_) {
        pendingBrowserActivate_ = false;
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && !savedFlushCb_) {
            savedFlushCb_ = (void *)disp->flush_cb;
            lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                lv_display_flush_ready(d);
            });
        }
        if (g_deviceUiLgfx) {
            concurrency::LockGuard g(spiLock);
            g_deviceUiLgfx->clearClipRect();
            g_deviceUiLgfx->fillScreen(0x0000);
        }
#endif
        enterEmulatorMode();
        browserActive_ = true;
        browserNeedsScan_ = true;
    }

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
                    return 10;
                }
                if (ejectFocused_) {
                    if (key == 's' || key == 'S') {
                        ejectFocused_ = false;
                        browser_.markDirty();
                        renderBrowser();
                        return 10;
                    }
                    if (key == 'k' || key == 'K' || key == '\r' || key == '\n') {
                        ejectFocused_ = false;
                        clearCart();
                        renderBrowser();
                        return 10;
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

    // (Deferred SAV save moved earlier in runOnce so it completes before
    // the LoRa RX re-arm — bus contention between SD write and
    // SX1262 startReceive was freezing the device on ALT-exit.)

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
            //
            // NOTE: DMs don't bridge over the Meshtastic public MQTT
            // broker (only broadcasts on uplink-enabled channels do).
            // So MMB across LoRa range needs a different design — see
            // future PRIVATE_APP challenge protocol. For now MMB works
            // within LoRa range only.
            sendTextDM(mmtOnTxTarget_,
                       "Do you want to battle in MonsterMesh? Reply Y or N.");
            LOG_INFO("[MonsterMesh] mmt challenge DM → 0x%08X\n",
                     (unsigned)mmtOnTxTarget_);
        }
    }
    // T4 reply drain: peer's Y/N to our outstanding challenge.
    if (pendingMmtAccepted_) {
        pendingMmtAccepted_ = false;
        // NOTE: do NOT call terminal_.printLine() here. runOnce is on the
        // LoRa task; printLine allocates LVGL labels on that thread and can
        // deadlock LVGL when label-create contends with the LVGL render
        // task. The battle screen takes over the display in a few ticks
        // anyway, so the user wouldn't see the line either way.
        LOG_INFO("[MonsterMesh] mmt accept from %s — kicking off PvP\n",
                 mmtPeerShort_[0] ? mmtPeerShort_ : "(?)");
        LOG_INFO("[MonsterMesh] mmt accept gate: target=0x%08X hasParty=%d "
                 "emu=%d br=%d tb=%d\n",
                 (unsigned)mmtAcceptedTxTarget_, (int)terminal_.hasParty(),
                 (int)emulatorActive_, (int)browserActive_, (int)textBattleActive_);
        if (mmtAcceptedTxTarget_ && terminal_.hasParty()) {
            mmtBattlePeer_ = mmtAcceptedTxTarget_;
            // Pre-compute seed and send TEXT_BATTLE_START right now so the
            // receiver arms its party-RX state machine before our chunks
            // arrive. Without this the chunks were ignored on the receiver
            // side (mmbPartyRxFrom_=0) and the protocol deadlocked.
            mmtBattleSeed_ = (uint32_t)(esp_random() ^ mmtBattlePeer_ ^ millis());
            sendMmbBattleStart(mmtBattleSeed_);
            pendingMmtBattleAsInitiator_ = true;
            LOG_INFO("[MonsterMesh] mmt: pendingMmtBattleAsInitiator_ SET, "
                     "peer=0x%08X seed=0x%08X\n",
                     (unsigned)mmtBattlePeer_, (unsigned)mmtBattleSeed_);
        } else {
            LOG_WARN("[MonsterMesh] mmt: launch SKIPPED — target=%u hasParty=%d\n",
                     (unsigned)(mmtAcceptedTxTarget_ ? 1 : 0),
                     (int)terminal_.hasParty());
        }
    }
    if (pendingMmtDeclined_) {
        pendingMmtDeclined_ = false;
        // Same LVGL-from-LoRa-thread hazard as the accept path above — skip
        // the printLine and just log to serial.
        LOG_INFO("[MonsterMesh] mmt decline from %s\n",
                 mmtPeerShort_[0] ? mmtPeerShort_ : "(?)");
    }
    if (pendingMmtAcceptedTx_) {
        pendingMmtAcceptedTx_ = false;
        // Suppressed: the "Open the MonsterMesh terminal." DM is redundant.
        // Receiver's local Y press already runs onLocalYReply() which arms
        // pendingMmtBattleAsReceiver_, and runOnce auto-launches the battle
        // station as soon as party exchange completes — no manual terminal
        // open required.
        (void)mmtAcceptedTxTarget_;
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
        nextBeaconRequestsResponse_ = true; // boot beacon asks for MQTT replies
        daycare_.forceBeacon();
        LOG_INFO("[MonsterMesh] first daycare beacon fired (30s gate, requestResponse=1)\n");
    }

    // NodeInfo on the MonsterMesh channel: once on MQTT connect, then
    // every 15 minutes thereafter while WiFi stays up.
    //
    // Why: PKI-encrypted DMs only decrypt on the recipient if they have
    // the sender's pubkey cached. The pubkey rides in NodeInfo packets.
    // If we only emit NodeInfo on the primary (LongFast) channel, peers
    // that only subscribe to MM never get it. Re-broadcast on mmChannel_
    // so any peer that comes online within the next 15 min picks us up.
    {
        static bool wifiWasConnected = false;
        static uint32_t wifiConnectAt = 0;
        static bool announceFired = false;
        static uint32_t lastPeriodicNodeInfoMs = 0;
        static constexpr uint32_t PERIODIC_NODEINFO_MS = 15UL * 60UL * 1000UL;
#if HAS_WIFI
        bool wifiNow = (WiFi.status() == WL_CONNECTED);
#else
        bool wifiNow = false;
#endif
        if (wifiNow && !wifiWasConnected) {
            wifiConnectAt = millis();
            announceFired = false;
            LOG_INFO("[MonsterMesh] WiFi connected — NodeInfo on MM channel in 3s\n");
        }
        if (!wifiNow) announceFired = false;
        wifiWasConnected = wifiNow;
        bool baseGate = wifiNow && setupDone_ &&
                        !emulatorActive_ && !browserActive_;
        if (baseGate && !announceFired && wifiConnectAt &&
            (millis() - wifiConnectAt) > 3000) {
            announceFired = true;
            lastPeriodicNodeInfoMs = millis();
            if (nodeInfoModule) {
                nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, false, mmChannel_);
                LOG_INFO("[MonsterMesh] MQTT-connect: sent NodeInfo on MM channel %u\n",
                         (unsigned)mmChannel_);
            }
        }
        if (baseGate && announceFired && lastPeriodicNodeInfoMs &&
            (millis() - lastPeriodicNodeInfoMs) > PERIODIC_NODEINFO_MS) {
            lastPeriodicNodeInfoMs = millis();
            if (nodeInfoModule) {
                nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, false, mmChannel_);
                LOG_INFO("[MonsterMesh] periodic NodeInfo on MM channel %u (15min refresh)\n",
                         (unsigned)mmChannel_);
            }
        }
    }

    // Stuck PvP state janitor — if either pendingMmt* flag has been set for
    // longer than MMT_PENDING_TIMEOUT_MS without progress (oppParty never
    // assembled, textBattle never launched), the prior session is dead and
    // is blocking new challenges. Observed 2026-05-20: Blue stayed init=1
    // for 1100+ seconds after a partial handshake, refusing every fresh
    // mmb challenge. Reset all the related state so a new round can land.
    {
        // Must exceed MMB_PARTY_RETRY_TIMEOUT_MS so the janitor doesn't wipe
        // state mid-assembly when chunks are still arriving stochastically.
        static constexpr uint32_t MMT_PENDING_TIMEOUT_MS = 120000;
        uint32_t now = millis();
        bool stale = false;
        if (pendingMmtBattleAsReceiver_ && mmtBattleReceivePendingMs_ != 0 &&
            (now - mmtBattleReceivePendingMs_) > MMT_PENDING_TIMEOUT_MS &&
            !mmbOppPartyReady_ && !textBattleActive_) {
            stale = true;
        }
        if (pendingMmtBattleAsInitiator_ && mmbPartyTxStartMs_ != 0 &&
            (now - mmbPartyTxStartMs_) > MMT_PENDING_TIMEOUT_MS &&
            !mmbOppPartyReady_ && !textBattleActive_) {
            stale = true;
        }
        // Also catch the case where an initiator flag is set but no timer
        // ever started (corruption from prior firmware revs).
        if ((pendingMmtBattleAsInitiator_ || pendingMmtBattleAsReceiver_) &&
            mmtBattleReceivePendingMs_ == 0 && mmbPartyTxStartMs_ == 0 &&
            !mmbOppPartyReady_ && !textBattleActive_) {
            // Stamp the receiver timer so we time out one cycle from now.
            mmtBattleReceivePendingMs_ = now;
        }
        if (stale) {
            LOG_WARN("[MonsterMesh] PvP janitor: clearing stuck pending state "
                     "(init=%d recv=%d age=%ums)\n",
                     (int)pendingMmtBattleAsInitiator_,
                     (int)pendingMmtBattleAsReceiver_,
                     (unsigned)(now -
                        (mmtBattleReceivePendingMs_ ? mmtBattleReceivePendingMs_
                                                    : mmbPartyTxStartMs_)));
            pendingMmtBattleAsInitiator_ = false;
            pendingMmtBattleAsReceiver_  = false;
            mmbPartyTxTarget_   = 0;
            mmbPartyRxFrom_     = 0;
            mmbOppPartyReady_   = false;
            mmbPartyChunkMask_  = 0;
            mmbPartyTotal_      = 0;
            mmbPartyTxStartMs_  = 0;
            mmbPartyTxLastMs_   = 0;
            mmbPartyTxAttempts_ = 0;
            mmtBattleReceivePendingMs_ = 0;
        }
    }

    // MQTT-only beacon response: handleReceived set pendingMqttResponseTo_
    // when a peer sent us a beacon with requestResponse=1. Build a beacon
    // + NodeInfo packet on the MM channel and publish via mqtt->onSend
    // directly, skipping LoRa iface->send so we don't fan out the response
    // over the airwaves. Throttle to 30 s/peer so a rapid burst of request
    // beacons can't spam the broker.
    //
    // CRITICAL: the beacon below MUST go via publishMqttOnlyBroadcast.
    // Do NOT switch this to service->sendToMesh / router->send — that path
    // also LoRa-broadcasts, which would turn N decks all responding to one
    // request beacon into N×LoRa TX bursts (exactly the airtime storm the
    // MQTT-only design avoids). See the function header comment on
    // publishMqttOnlyBroadcast for the full reasoning + invariants.
    if (pendingMqttResponseTo_ != 0 && setupDone_ &&
        !emulatorActive_ && !browserActive_) {
        uint32_t now = millis();
        if (lastMqttResponseMs_ == 0 || now - lastMqttResponseMs_ > 30000) {
            uint32_t target = pendingMqttResponseTo_;
            pendingMqttResponseTo_ = 0;
            lastMqttResponseMs_ = now;
            // 1) Beacon — build the same packet the daycare's setSendBeacon
            //    callback emits, but go straight to MQTT.
            DaycareBeacon b;
            memset(&b, 0, sizeof(b));
            b.type = 0x60;
            b.nodeId = nodeDB->getNodeNum();
            const char *sn = owner.short_name;
            if (sn) {
                size_t n = strnlen(sn, sizeof(b.shortName) - 1);
                memcpy(b.shortName, sn, n);
            }
            b.partyCount = 0; // server-side beacon, no party state to leak
            b.ngPlusTier = lordCurrentNgPlusTier();
            b.requestResponse = 0; // never request response on a response (no ping-pong)
            meshtastic_MeshPacket *pb = router->allocForSending();
            if (pb) {
                pb->to = NODENUM_BROADCAST;
                pb->channel = mmChannel_;
                pb->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
                size_t sz = sizeof(b);
                if (sz > sizeof(pb->decoded.payload.bytes))
                    sz = sizeof(pb->decoded.payload.bytes);
                memcpy(pb->decoded.payload.bytes, &b, sz);
                pb->decoded.payload.size = sz;
                LOG_INFO("[MonsterMesh] MQTT-only beacon response → req 0x%08X\n",
                         (unsigned)target);
                publishMqttOnlyBroadcast(pb);
            }
            // 2) NodeInfo — sendOurNodeInfo also lands on LoRa, but it has
            //    a 5-min cooldown so flooding is bounded. Calling it here
            //    is the simplest way to broadcast our pubkey + short_name
            //    via the broker. A pure-MQTT NodeInfo path could come later.
            if (nodeInfoModule) {
                nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, false, mmChannel_);
            }
        }
    }

    // Reciprocal beacon: handleReceived set pendingReplyBeacon_ when a new
    // neighbor was added. Fire a single forceBeacon() back so the peer
    // learns about us promptly. Throttle to one per 60 s so a flurry of new
    // neighbors doesn't spam the airwaves.
    if (setupDone_ && pendingReplyBeacon_ && daycare_.isActive() &&
        !emulatorActive_ && !browserActive_ && firstBeaconDone_) {
        uint32_t now = millis();
        if (lastReplyBeaconMs_ == 0 || now - lastReplyBeaconMs_ > 60000) {
            pendingReplyBeacon_ = false;
            lastReplyBeaconMs_  = now;
            daycare_.forceBeacon();
            LOG_INFO("[MonsterMesh] reciprocal daycare beacon fired (new neighbor)\n");
        } else {
            // Hold the flag until the throttle window clears.
        }
    }

    // Daycare tick — only run while in the Meshtastic UI. The radio is asleep
    // in emu/browser mode, so beacons would just queue up uselessly.
    if (setupDone_ && !emulatorActive_ && !browserActive_ && daycare_.isActive()) {
        daycare_.tick(millis());
    }

    // ── T4 phase 3: live PvP battle launch ────────────────────────────────
    // Both initiator and receiver land here on the main loop. Receiver-side
    // auto-launch is now gated by the mmtChallengerPeer_ window (set when
    // the sender's challenge DM lands), so stray TEXT_BATTLE_START packets
    // from other-agent code can't bounce us into spurious PvP.
    // Drain pending MMB party TX (point-to-point). Sender sets this on Y
    // receipt; receiver sets it on TEXT_BATTLE_START receipt. We re-publish
    // the chunk burst every MMB_PARTY_RETRY_INTERVAL_MS until either the
    // engine launches (clears target below) or MMB_PARTY_RETRY_TIMEOUT_MS
    // elapses — needed because the MQTT bridge is QoS 0 and a peer's WiFi
    // outage drops chunks silently. mmbOppPartyReady_ is the *receive*
    // signal; the opposite direction has no ACK, so we just keep firing.
    if (mmbPartyTxTarget_ != 0 && terminal_.hasParty()) {
        uint32_t now = millis();
        if (mmbPartyTxStartMs_ != 0 &&
            (now - mmbPartyTxStartMs_) >= MMB_PARTY_RETRY_TIMEOUT_MS) {
            LOG_WARN("[MonsterMesh] MMB party TX retries timed out after %ums "
                     "(attempts=%u oppReady=%d); abandoning\n",
                     (unsigned)(now - mmbPartyTxStartMs_),
                     (unsigned)mmbPartyTxAttempts_,
                     (int)mmbOppPartyReady_);
            mmbPartyTxTarget_   = 0;
            mmbPartyTxLastMs_   = 0;
            mmbPartyTxStartMs_  = 0;
            mmbPartyTxAttempts_ = 0;
        } else {
            bool firstSend  = (mmbPartyTxLastMs_ == 0);
            bool timeToRetry = !firstSend &&
                (now - mmbPartyTxLastMs_) >= MMB_PARTY_RETRY_INTERVAL_MS;
            if (firstSend || timeToRetry) {
                mmbPartyTxLastMs_ = now;
                mmbPartyTxAttempts_++;
                LOG_INFO("[MonsterMesh] MMB party TX attempt %u → 0x%08X "
                         "(oppReady=%d)\n",
                         (unsigned)mmbPartyTxAttempts_,
                         (unsigned)mmbPartyTxTarget_,
                         (int)mmbOppPartyReady_);
                sendMmbPartyChunks(mmbPartyTxTarget_, terminal_.getParty());
            }
        }
    }

    // Log every tick where launch flag is set but a gate blocks. Throttled
    // to once a second so we don't spam the serial.
    if ((pendingMmtBattleAsInitiator_ || pendingMmtBattleAsReceiver_) &&
        (textBattleActive_ || !setupDone_ || emulatorActive_ ||
         browserActive_ || !terminal_.hasParty() || !mmbOppPartyReady_)) {
        static uint32_t lastBlockLogMs = 0;
        uint32_t now = millis();
        if (now - lastBlockLogMs > 1000) {
            lastBlockLogMs = now;
            LOG_WARN("[MonsterMesh] mmt launch BLOCKED: init=%d recv=%d tb=%d "
                     "setupDone=%d emu=%d br=%d hasParty=%d oppReady=%d\n",
                     (int)pendingMmtBattleAsInitiator_,
                     (int)pendingMmtBattleAsReceiver_,
                     (int)textBattleActive_, (int)setupDone_,
                     (int)emulatorActive_, (int)browserActive_,
                     (int)terminal_.hasParty(),
                     (int)mmbOppPartyReady_);
        }
    }

    // ── Server-auth CLIENT side: textBattle_ flipped to CLIENT mode when
    // a CHALLENGE arrived. We need to swap LVGL flush_cb + clear screen
    // (just like the SERVER launch below) so Meshtastic UI repaints don't
    // overdraw the battle overlay.
    if (textBattle_.isActive() && !textBattleActive_ && setupDone_) {
        textBattleActive_ = true;
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
        LOG_INFO("[MonsterMesh] textBattle activated externally (CLIENT overlay) — flush_cb parked\n");
    }

    // ── Server-authoritative PvP launch (mmb2) ─────────────────────────
    // Deferred from challengePeerByShortNameV2 so the heavy LVGL work
    // doesn't run on the terminal/LVGL thread. Single CHALLENGE packet
    // carries our party; no party-exchange round-trip required.
    if (pendingMmb2Initiator_ && !textBattleActive_ && setupDone_ &&
        !emulatorActive_ && !browserActive_ && terminal_.hasParty()) {
        pendingMmb2Initiator_ = false;
        uint32_t target = pendingMmb2Target_;
        char     peerShort[12];
        memcpy(peerShort, pendingMmb2PeerShort_, sizeof(peerShort));
        peerShort[sizeof(peerShort) - 1] = '\0';

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

        const char *ourShort = (owner.short_name[0] != '\0')
                                 ? owner.short_name : "MM";
        textBattle_.setMyTbParty(terminal_.getParty(), ourShort);
        textBattle_.startServerAuthAsInitiator(target,
                                                terminal_.getParty(),
                                                ourShort);
        LOG_INFO("[MonsterMesh] mmb2: launched server-auth challenge → %s "
                 "(0x%08X)\n", peerShort, (unsigned)target);
    }

    if ((pendingMmtBattleAsInitiator_ || pendingMmtBattleAsReceiver_) &&
        !textBattleActive_ && setupDone_ && !emulatorActive_ &&
        !browserActive_ && terminal_.hasParty() && mmbOppPartyReady_) {
        bool asInitiator = pendingMmtBattleAsInitiator_;
        pendingMmtBattleAsInitiator_ = false;
        pendingMmtBattleAsReceiver_  = false;
        // Consume the just-arrived opponent party for the launch below; clear
        // the ready flag so a fresh exchange is required for the next fight.
        mmbOppPartyReady_ = false;
        // Stop retransmitting our party — both engines are about to run.
        mmbPartyTxTarget_   = 0;
        mmbPartyTxLastMs_   = 0;
        mmbPartyTxStartMs_  = 0;
        mmbPartyTxAttempts_ = 0;
        // Belt + suspenders: clear chunk-assembly state at launch too so a
        // post-engine straggler chunk can't be ORed into a stale mask and
        // cause the NEXT session to launch with the previous opponent's
        // pokemon partially overwritten.
        mmbPartyChunkMask_ = 0;
        mmbPartyTotal_     = 0;
        if (mmbPartyChunks_)
            memset(mmbPartyChunks_, 0, MMB_PARTY_CHUNKS_BYTES);
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

        // Use the directly-exchanged opponent party (assembled from
        // TEXT_BATTLE_PARTY chunks). The launch gate above requires
        // mmbOppPartyReady_ so this is guaranteed populated.
        Gen1Party oppParty = mmbOppParty_;
        LOG_INFO("[MonsterMesh] PvP: launching with direct-exchange opp "
                 "party (count=%u)\n", (unsigned)oppParty.count);

        if (asInitiator) {
            // Pass the pre-computed seed AND session — TEXT_BATTLE_START
            // was already broadcast by sendMmbBattleStart() with that
            // session_id. Engine's session_ must match so outgoing
            // ACTION/HASH/FORFEIT packets pass the receiver's filter.
            textBattle_.startNetworkedAsInitiator(mmtBattlePeer_,
                                                   terminal_.getParty(),
                                                   oppParty,
                                                   mmtBattleSeed_,
                                                   mmtBattleSession_);
            char hdr[40];
            snprintf(hdr, sizeof(hdr), "MMB vs %.4s",
                     mmtPeerShort_[0] ? mmtPeerShort_ : "Peer");
            textBattle_.setHeader(hdr);
            LOG_INFO("[MonsterMesh] PvP: started as initiator vs 0x%08X opp=%u session=0x%04X\n",
                     (unsigned)mmtBattlePeer_, (unsigned)oppParty.count,
                     (unsigned)mmtBattleSession_);
        } else {
            // Pass the session_id captured from the incoming START packet
            // (handleReceived stored it in mmtBattleSession_) so our
            // outgoing ACTION packets match the initiator's session
            // filter.
            textBattle_.startNetworkedAsReceiver(mmtBattlePeer_,
                                                  terminal_.getParty(),
                                                  mmtBattleSeed_,
                                                  oppParty,
                                                  mmtBattleSession_);
            char hdr[40];
            snprintf(hdr, sizeof(hdr), "MMB incoming");
            textBattle_.setHeader(hdr);
            LOG_INFO("[MonsterMesh] PvP: started as receiver from 0x%08X seed=0x%08X opp=%u\n",
                     (unsigned)mmtBattlePeer_, (unsigned)mmtBattleSeed_,
                     (unsigned)oppParty.count);
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
                // Clear ALL PvP-handshake state so a leftover chunk
                // retransmit from the peer (their mmbPartyTxTarget_ keeps
                // firing for the full 90s retry window) can't auto-arm a
                // new receiver session and re-launch the battle the
                // moment the user lands back in the terminal. Without
                // this, the launch gate (pendingMmt* && mmbOppPartyReady_
                // && !textBattleActive_) trips on the next chunk burst.
                pendingMmtBattleAsInitiator_ = false;
                pendingMmtBattleAsReceiver_  = false;
                mmbOppPartyReady_            = false;
                mmbPartyChunkMask_           = 0;
                mmbPartyTotal_               = 0;
                mmbPartyRxFrom_              = 0;
                mmbPartyTxTarget_            = 0;
                mmbPartyTxStartMs_           = 0;
                mmbPartyTxLastMs_            = 0;
                mmbPartyTxAttempts_          = 0;
                mmtChallengerPeer_           = 0;
                mmtChallengerExpireMs_       = 0;
                mmtAwaitingReplyFrom_        = 0;
                mmtBattlePeer_               = 0;
                mmtBattleSeed_               = 0;
                mmtBattleSession_            = 0;
                mmtBattleReceivePendingMs_   = 0;
                if (mmbPartyChunks_)
                    memset(mmbPartyChunks_, 0, MMB_PARTY_CHUNKS_BYTES);
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
    // While the browser is open, run runOnce more often so the buffered
    // key drain (pendingBrowserKey_ → browser_.handleKey) feels snappy
    // instead of stacking up at the default 50 ms cadence.
    if (browserActive_) return 10;
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
        // MMB engine traffic (TEXT_BATTLE_ACTION/HASH/FORFEIT) on the
        // MonsterMesh channel so it MQTT-bridges across LoRa range.
        p->channel = mmChannel_;
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

    // Keep device-ui backlight alive on EVERY keyboard read — used to gate
    // on emu/browser/textbattle but that left the terminal blanking while
    // the user was actively typing: typing keystrokes don't show up as LVGL
    // input until LVGL routes them, so the inactivity monitor saw nothing.
    // Poking activity here keeps both the terminal scrollback and the lgfx-
    // direct text-battle screen lit while the user is at the keyboard.
    lv_display_trigger_activity(NULL);

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
            if (now - g_lastAltFireMs > 250) {
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

        // ALT button in RAW mode — toggle screens (exit emulator).
        // CRITICAL: this is one of THREE ALT detectors (the others are the
        // KEY-mode peek above and the runOnce I2C poll). They must share the
        // same g_lastAltFireMs debounce, otherwise a single physical press
        // fires here AND in the runOnce poll, double-toggling — emulator
        // exit + immediate browser activate — and freezing the device.
        bool altHeld = (b[0] & 0x10) != 0;
        static bool g_altWasHeldRaw = false;
        static bool g_altSeenLowRaw = false;
        if (!altHeld) g_altSeenLowRaw = true;
        if (altHeld && !g_altWasHeldRaw && g_altSeenLowRaw) {
            uint32_t now = millis();
            if (now - g_lastAltFireMs > 250) {
                g_lastAltFireMs = now;
                g_micLastToggleMs = now;  // also gate mic so it doesn't bounce
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

    // Preemptive receiver arm — when an MMT challenger is armed and the user
    // types 'y'/'Y' followed by Enter, assume the user is replying Y to the
    // challenge and arm the receiver path immediately. This makes the
    // challengee deterministically the "slave" (receiver), so even if Red's
    // TEXT_BATTLE_START packet is lost over MQTT, Blue is already in the
    // right state when chunks arrive.
    if (monsterMeshModule) {
        static uint32_t yPressedAtMs = 0;
        if (key == 'y' || key == 'Y') {
            yPressedAtMs = millis();
        } else if (key == 0x0D && yPressedAtMs != 0 &&
                   (millis() - yPressedAtMs) < 5000) {
            // 'y' then Enter within 5s → user is sending Y reply.
            yPressedAtMs = 0;
            monsterMeshModule->onLocalYReply();
        } else if (key != 0 && key != 'y' && key != 'Y' && key != 0x0D) {
            // any other character cancels the pending Y (user typed
            // something other than a single Y before Enter).
            yPressedAtMs = 0;
        }
    }

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
    //
    // Terminal special-case: isTerminalActive() stays true even when the user
    // navigates to chat / nodes / settings (the panel is preserved in the
    // background). Only swallow the key if the terminal's input is actually
    // the foregrounded LVGL widget — otherwise chat replies (and any other
    // panel's textarea) never receive keystrokes.
    if (monsterMeshModule) {
        bool ownsScreen = monsterMeshModule->isEmulatorActive() ||
                          monsterMeshModule->isBrowserActive()  ||
                          monsterMeshModule->isTextBattleActive() ||
                          monsterMeshModule->isDungeonActive() ||
                          (monsterMeshModule->isTerminalActive() &&
                           monsterMeshModule->isTerminalForeground());
        if (ownsScreen) {
            monsterMeshModule->handleKeyFromLVGL(key);
            return; // consume — don't pass to LVGL
        }
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
    // Heal the in-memory party for on-deck play. HP → maxHp, clear status,
    // PP → canonical max for each move. The SAV's original values stay
    // untouched on disk: writePartyToSavOnSd snapshots HP/status/PP from
    // the existing SAV bytes before writing and restores them.
    for (uint8_t i = 0; i < count; ++i) {
        Gen1Pokemon &p = out.mons[i];
        p.hp[0] = p.maxHp[0];
        p.hp[1] = p.maxHp[1];
        p.status = 0;
        for (uint8_t s = 0; s < 4; ++s) {
            const Gen1MoveData *mv = gen1Move(p.moves[s]);
            p.pp[s] = mv ? mv->pp : (p.moves[s] ? 25 : 0);
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

    // Snapshot per-mon HP / status / PP from the existing SAV before
    // overwriting the block. We DON'T want our in-deck "fully healed"
    // values written back — the SAV should reflect whatever HP/status/PP
    // state the player left the game in. Level / EXP / stat changes from
    // battles still propagate (those fields come from party.mons[i]).
    // Gen1Pokemon layout (44 bytes): hp at 1-2, status at 4, pp at 29-32.
    struct PreservedMonFields { uint8_t hpHi, hpLo, status, pp0, pp1, pp2, pp3; };
    PreservedMonFields snap[6] = {};
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *m = &buf[SAV_POKEMON_DATA + i * 44];
        snap[i].hpHi   = m[1];
        snap[i].hpLo   = m[2];
        snap[i].status = m[4];
        snap[i].pp0    = m[29];
        snap[i].pp1    = m[30];
        snap[i].pp2    = m[31];
        snap[i].pp3    = m[32];
    }
    memcpy(&buf[SAV_POKEMON_DATA], (const uint8_t *)party.mons, (size_t)count * 44);
    // Restore the snapshot — HP / status / PP stay as the SAV had them.
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t *m = &buf[SAV_POKEMON_DATA + i * 44];
        m[1]  = snap[i].hpHi;
        m[2]  = snap[i].hpLo;
        m[4]  = snap[i].status;
        m[29] = snap[i].pp0;
        m[30] = snap[i].pp1;
        m[31] = snap[i].pp2;
        m[32] = snap[i].pp3;
    }
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
    // Always commit the staged party into the terminal's data model so
    // hasParty() flips true and downstream features (PvP launch gate,
    // daycare check-in, gym fights) work even before the user has
    // opened the terminal panel. The visible UI refresh is gated on
    // terminalActive_ so we don't touch LVGL labels on a hidden panel.
    terminal_.setParty(terminalStagedParty_);
    if (terminalActive_) {
        terminal_.refreshParty();
    }
    // Also stage into textBattle_ so the server-auth CLIENT path can
    // respond to an inbound CHALLENGE without an additional round-trip
    // — ACCEPT carries our party straight from this staged copy.
    {
        const char *ourShort = (owner.short_name[0] != '\0')
                                 ? owner.short_name : "MM";
        textBattle_.setMyTbParty(terminalStagedParty_, ourShort);
    }
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
            emuTaskIdle_ = true;  // ALT-exit polls this to wait for emu quiescence
            vTaskDelay(pdMS_TO_TICKS(50));
            lastWake = xTaskGetTickCount();
            continue;
        }
        emuTaskIdle_ = false;  // we're about to runFrame — not idle
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
    // b444: unconditionally wake the screen on ANY keystroke. The per-branch
    // EVENT_INPUT triggers below missed the path where the terminal panel is
    // up but doesn't currently hold LVGL focus (e.g. user pressed a key but
    // focus was on a hidden chat widget), so the screen blanked while the
    // user was actively typing in the MonsterMesh terminal.
    powerFSM.trigger(EVENT_INPUT);

    // Text battle steals all keys while it's foreground. ESC (0x1B) ends the
    // battle and returns to the terminal scrollback that was preserved in the
    // background.
    if (textBattleActive_) {
        textBattle_.handleKey(ascii);
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

    // Terminal: route ASCII keys to it ONLY when its input field is the
    // LVGL-focused widget. That means the user is *currently* looking at
    // the MM Terminal panel — not at chat, nodes, or any other panel they
    // may have navigated to while leaving the terminal alive in the
    // background. Without this focus gate the intercept swallowed every
    // keystroke so chat replies (e.g. typing Y to an MMB challenge) never
    // reached the chat textarea.
    if (terminalActive_ && !emulatorActive_ && !browserActive_ &&
        ascii != 0x05 && terminal_.hasInputFocus()) {
        terminal_.onKey(ascii);
        return;
    }
    // b444: per-branch EVENT_INPUT removed — now triggered unconditionally
    // at the top of handleKeyPress.

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
            // Same lessons as ALT-exit-emu (b339+): no fillScreen (would
            // race with SD reads still in flight for the browser scan),
            // walk the screen's children explicitly when invalidating so
            // Meshtastic's panels actually repaint instead of filling in
            // one widget at a time as the user taps.
            LOG_INFO("[MonsterMesh] ALT-exit browser: returning to Meshtastic\n");
            browserActive_ = false;
            exitEmulatorMode();  // emulatorActive_ already false
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            if (disp) {
                if (savedFlushCb_) {
                    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                    savedFlushCb_ = nullptr;
                }
                lv_obj_t *scr = lv_screen_active();
                if (scr) {
                    lv_obj_invalidate(scr);
                    uint32_t n = lv_obj_get_child_count(scr);
                    for (uint32_t i = 0; i < n; ++i) {
                        lv_obj_t *child = lv_obj_get_child(scr, i);
                        if (child) lv_obj_invalidate(child);
                    }
                }
            }
#endif
            return;
        }

        if (emulatorActive_) {
            // ── Exit emulator → Meshtastic UI ─────────────────────────────
            // Strict order (per user spec): SAV save → screen → radio.
            // Running them sequentially on the LVGL thread avoids the
            // emu-task / LVGL-thread / LoRa-thread spiLock collision that
            // froze the device. We block LVGL for ~100-200ms total during
            // this transition, which is acceptable for a mode change.
            LOG_INFO("[MonsterMesh] ALT-exit: stage A — park emu task\n");
            emulatorActive_ = false;
            // Poll the emu task's idle signal so we know its in-progress
            // runFrame (and any auto-save inside it) is fully done before
            // we touch the screen or SD. Snaps out the moment the emu
            // task quiesces; cap at 250ms to guard against a stuck task.
            {
                uint32_t startWait = millis();
                while (!emuTaskIdle_ && (millis() - startWait) < 250) {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
                LOG_INFO("[MonsterMesh] ALT-exit: emu idle after %ums (idle=%d)\n",
                         (unsigned)(millis() - startWait), (int)emuTaskIdle_);
            }

#if HAS_TFT
            // Stage 1 — screen: restore LVGL flush_cb + invalidate every
            // child of the active screen. lv_obj_invalidate(screen) only
            // marks the screen object's own area as dirty — Meshtastic's
            // panels are children that LVGL skips unless explicitly
            // invalidated, so only widgets the user pokes get repainted.
            // Walk the tree and invalidate each so the entire UI
            // repaints with proper Meshtastic background pixels.
            LOG_INFO("[MonsterMesh] ALT-exit: stage 1 — screen restore\n");
            lv_display_t *disp = lv_display_get_default();
            if (disp) {
                if (savedFlushCb_) {
                    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                    savedFlushCb_ = nullptr;
                }
                lv_obj_t *scr = lv_screen_active();
                if (scr) {
                    lv_obj_invalidate(scr);
                    // Force every descendant to repaint too — LVGL won't
                    // redraw children just because the parent invalidates.
                    uint32_t n = lv_obj_get_child_count(scr);
                    for (uint32_t i = 0; i < n; ++i) {
                        lv_obj_t *child = lv_obj_get_child(scr, i);
                        if (child) lv_obj_invalidate(child);
                    }
                }
            }
            LOG_INFO("[MonsterMesh] ALT-exit: stage 1 done\n");
#endif

            // Stage 2 — radio: re-arm LoRa + WiFi. exitEmulatorMode flips
            // atomic flags; the actual SPI work runs in runOnce under
            // spiLock so it serializes with the deferred SAV save below.
            LOG_INFO("[MonsterMesh] ALT-exit: stage 2 — re-arm radios\n");
            exitEmulatorMode();

            // Stage 3 — defer the SAV save to runOnce so the LVGL thread
            // is freed immediately. SD write happens on the LoRa thread
            // alongside normal radio operation; spiLock serializes them.
            if (emu_.isRunning()) {
                LOG_INFO("[MonsterMesh] ALT-exit: stage 3 — staged SAV save\n");
                pendingSave_ = true;
            }
            LOG_INFO("[MonsterMesh] ALT-exit: all stages done\n");
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

        // ALT in Meshtastic: if a ROM is already loaded (cart inserted),
        // jump straight back into the emulator. Otherwise open the ROM
        // loader. Lets the user toggle emu ↔ Meshtastic without going
        // through the file browser every time.
        //
        // CRITICAL: ALT can fire from the LVGL thread (KEY-mode peek,
        // input broker) AND the LoRa thread (runOnce I2C poll). LVGL
        // ops + spiLock-guarded LGFX fillScreen on the LVGL thread can
        // deadlock with in-progress DeviceUI rendering (e.g. during the
        // 250-node restore). Stage a flag and let runOnce handle the
        // expensive parts on the LoRa thread.
        //
        // Yield terminal focus before we leave Meshtastic. We don't
        // close the panel (b325 lost scrollback + dropped user back at
        // map); we just remove input_ from the LVGL focus group so
        // chat/DM textareas can claim focus when the user ALT-exits the
        // emulator. Terminal scrollback is preserved.
        if (terminalActive_) terminal_.yieldFocus();
        if (emuInitialized_) {
            LOG_INFO("[MonsterMesh] ALT in Meshtastic: cart loaded — resuming emulator\n");
            pendingEmuResume_ = true;
        } else {
            pendingBrowserActivate_ = true;
        }
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

    // Create emulator FreeRTOS task on Core 1 (high priority — never stalls).
    // Heap fragmentation has been observed killing this silently — if the
    // task fails to spawn, ROM "loads" but the screen stays on the
    // Loading... overlay because no frame ever blits. Log explicitly.
    if (!emuTaskHandle_) {
        BaseType_t r = xTaskCreatePinnedToCore(
            emuTaskEntry, "monstermesh_emu",
            16384, this, 5, &emuTaskHandle_, 1
        );
        if (r != pdPASS || !emuTaskHandle_) {
            LOG_ERROR("[MonsterMesh] emu task spawn FAILED (r=%d handle=%p) "
                      "free=%u largest=%u — heap too fragmented\n",
                      (int)r, (void *)emuTaskHandle_,
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMaxAllocHeap());
        } else {
            LOG_INFO("[MonsterMesh] emu task spawned (handle=%p)\n",
                     (void *)emuTaskHandle_);
        }
    }

    // Create render task on Core 0 (lower priority — blits framebuffer to TFT
    // without blocking the emulator task, so audio stays smooth)
    if (!renderTaskHandle_) {
        BaseType_t r = xTaskCreatePinnedToCore(
            renderTaskEntry, "monstermesh_render",
            4096, this, 2, &renderTaskHandle_, 0
        );
        if (r != pdPASS || !renderTaskHandle_) {
            LOG_ERROR("[MonsterMesh] render task spawn FAILED (r=%d handle=%p) "
                      "free=%u largest=%u\n",
                      (int)r, (void *)renderTaskHandle_,
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMaxAllocHeap());
        } else {
            LOG_INFO("[MonsterMesh] render task spawned (handle=%p)\n",
                     (void *)renderTaskHandle_);
        }
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
    // No-op park on the radio chip: previously we called disableInterrupt()
    // here to silence DIO1 callbacks. That left the chip in a state that
    // startReceive() on exit could not recover from — every exit hung at
    // setStandby/checkNotification inside startReceive. Instead, let the
    // chip keep RX'ing autonomously; g_meshSuspended already drops the
    // packets at the receivePacket level and gates TX at three other layers
    // (per feedback_mm_tx_gate_layered.md), so emu mode stays quiet.
    LOG_INFO("MonsterMesh: radios parked (soft — IRQ left enabled to avoid resume hang)\n");
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
