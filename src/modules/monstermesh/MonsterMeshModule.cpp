#include "MonsterMeshModule.h"

#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH

#include "gps/RTC.h"
#include "main.h"
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
#include "mesh/RadioLibInterface.h"
#include "MonsterMeshAudio.h"
#include "PokemonData.h"
#include "Gen1Species.h"
// DaycareSavPatcher.h included via PokemonDaycare.h (in MonsterMeshModule.h)

// LovyanGFX is available on T-Deck in both t-deck and t-deck-tft builds
#include <LovyanGFX.hpp>
#if HAS_TFT
#include <lvgl.h>
#include "display/lv_display_private.h"
#include "indev/lv_indev_private.h"
#include "generated/ui_320x240/screens.h"

// UNSCII 16px pixel font — declared manually because lv_conf.h disables it
LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_unscii_8;
#endif

MonsterMeshModule *monsterMeshModule = nullptr;

// Global flag read by patched device-ui LGFXDriver to suppress powersave
// transitions while the GB emulator is running. Otherwise LVGL's inactivity
// timer expires (because LVGL flushes are paused during emu), triggering
// lgfx->sleep() + lgfx->powerSaveOn() which reconfigures the shared SPI bus
// and stalls the emulator task on Core 1. Symptom was "[DeviceUI] enter
// powersave" immediately before the mid-play freeze.
extern "C" volatile bool g_mmEmulatorActive = false;

// Set by the patched TFTView_320x240::notifyMessagesRestored — blocks
// MonsterMesh from opening the ROM browser until Meshtastic's phone-sync
// message history replay completes. Opening earlier races the LVGL state
// machine and triggers the mid-play crash ~3 min in.
extern "C" volatile bool g_mmMessagesRestored = false;

// Weak default — device-ui provides the real implementation when present
extern "C" __attribute__((weak)) void monstermesh_set_toggle_cb(void (*cb)(void)) { (void)cb; }

// Global LGFX pointer — set by device-ui if present, otherwise falls back to getLovyanGfx()
static lgfx::LGFX_Device *g_deviceUiLgfx = nullptr;
extern "C" void monstermesh_set_lgfx(void *ptr)
{
    g_deviceUiLgfx = static_cast<lgfx::LGFX_Device *>(ptr);
}

// getLovyanGfx() is defined in TFTDisplay.cpp — returns the board's LGFX instance
extern lgfx::LGFX_Device *getLovyanGfx();

// Use device-ui LGFX if set, otherwise fall back to the firmware's own TFT driver
static lgfx::LGFX_Device *getGfx()
{
    if (g_deviceUiLgfx) return g_deviceUiLgfx;
    return getLovyanGfx();
}

// Forward declarations for static helpers defined later in this file
static void kbSetMode(bool raw);
static void lvgl_hide_browser();

// Called from device-ui tools menu button via function pointer
static void mmToggle()
{
    if (monsterMeshModule) {
        monsterMeshModule->handleKeyFromLVGL(0x05); // toggle emulator on/off
    }
}

// Bridge: called from device-ui when Enter is pressed in the terminal textarea
extern "C" void monsterMeshTerminalSubmit()
{
    if (monsterMeshModule && monsterMeshModule->terminal().ready())
        monsterMeshModule->terminal().submitCommand();
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
      concurrency::OSThread("MonsterMesh"),
      shim_(transport_),
      lobby_(transport_, emu_)
{
    // We want to see all PRIVATE_APP packets on any channel, not just "our" channel,
    // because wantPacket filters by channel anyway.
    isPromiscuous = false;
    loopbackOk = false;

    // Tools menu removed — ALT key is the only entry point (avoids keyboard
    // mode desync when toggling from an LVGL button callback).
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

    // Always process Ctrl+E (ALT+E on T-Deck) regardless of emulator state
    if (event->kbchar == 0x05) {  // Ctrl+E
        handleKeyPress(0x05);
        return 0;
    }

    // Note: trackball long-press toggle is polled via GPIO 0 in runOnce() — not via InputBroker.
    // The device-ui encoder driver consumes the trackball press through LVGL directly.

    if (!emulatorActive_ && !browserActive_) return 0;  // let Meshtastic handle keys

    // ANYKEY with kbchar = raw character
    if (event->inputEvent == INPUT_BROKER_ANYKEY && event->kbchar != 0) {
        handleKeyPress(event->kbchar);
        lastKeyMs_ = millis();
        return 0;
    }

    // Trackball events → viewport scroll (up/down), palette cycle (left/right)
    if (event->inputEvent == INPUT_BROKER_UP) {
        viewportDelta_--;
        return 0;
    }
    if (event->inputEvent == INPUT_BROKER_DOWN) {
        viewportDelta_++;
        return 0;
    }
    if (emulatorActive_ && event->inputEvent == INPUT_BROKER_LEFT) {
        g_emuPaletteIdx = (g_emuPaletteIdx + EMU_PALETTE_COUNT - 1) % EMU_PALETTE_COUNT;
        return 0;
    }
    if (emulatorActive_ && event->inputEvent == INPUT_BROKER_RIGHT) {
        g_emuPaletteIdx = (g_emuPaletteIdx + 1) % EMU_PALETTE_COUNT;
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
        // Set up channel 1 as "MonsterMesh"
        ch.role = meshtastic_Channel_Role_SECONDARY;
        snprintf(ch.settings.name, sizeof(ch.settings.name), "MonsterMesh");
        // 16-byte PSK — shared across all MonsterMesh nodes
        // "MonsterMesh!2024" as a simple deterministic key
        static const uint8_t psk[] = {
            'M','o','n','s','t','e','r','M',
            'e','s','h','!','2','0','2','4'
        };
        ch.settings.psk.size = 16;
        memcpy(ch.settings.psk.bytes, psk, 16);
        channels.setChannel(ch);
        nodeDB->saveToDisk();
        LOG_INFO("[MonsterMesh] channel 1 configured as 'MonsterMesh'\n");
    }
}

// ── wantPacket() — filter incoming packets ──────────────────────────────────

bool MonsterMeshModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // Accept PRIVATE_APP packets (binary serial data) — on MonsterMesh channel,
    // primary channel (daycare beacons), or DMs addressed to us
    if (p->decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        if (p->channel == MONSTERMESH_CHANNEL ||
            p->channel == channels.getPrimaryIndex() ||
            p->to == nodeDB->getNodeNum()) {
            return true;
        }
    }
    // Accept TEXT_MESSAGE DMs TO us
    if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
        p->to == nodeDB->getNodeNum() &&
        p->decoded.payload.size > 0) {
        const char *txt = (const char *)p->decoded.payload.bytes;
        size_t sz = p->decoded.payload.size;
        // "MM..." or "mm..." prefix — internal protocol messages
        if (sz >= 2 &&
            (txt[0] == 'M' || txt[0] == 'm') &&
            (txt[1] == 'M' || txt[1] == 'm')) {
            return true;
        }
        // "MonsterMesh..." prefix
        if (sz >= 11 &&
            (txt[0] == 'M' || txt[0] == 'm') &&
            (txt[1] == 'o' || txt[1] == 'O')) {
            return true;
        }
        // Single Y/N reply when we are the initiator waiting for a response
        // (cable club legacy path OR MMT text-battle)
        if (sz == 1 &&
            (waitingForAcceptFrom_ != 0 || mmtWaitingForAcceptFrom_ != 0) &&
            (txt[0] == 'Y' || txt[0] == 'y' || txt[0] == 'N' || txt[0] == 'n')) {
            return true;
        }
        // "fled!" from partner
        if (sz >= 5 && txt[0] == 'f' && txt[1] == 'l' && txt[2] == 'e') {
            return true;
        }
    }
    return false;
}

// ── handleReceived() — incoming mesh packet ─────────────────────────────────

ProcessMessage MonsterMeshModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // ── Text DM commands ──────────────────────────────────────────────────
    //
    // User commands (trigger action + send ack to other side):
    //   "MM cable on"  → pair locally, send "MM link on" ack
    //   "MM cable off" → disconnect locally, send "MM link off" ack
    //
    // Internal acks (trigger action, NO echo — stops the loop):
    //   "MM link on"   → pair locally, no reply
    //   "MM link off"  → disconnect locally, no reply
    //
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        char txt[64] = {};
        size_t len = mp.decoded.payload.size;
        if (len >= sizeof(txt)) len = sizeof(txt) - 1;
        memcpy(txt, mp.decoded.payload.bytes, len);
        txt[len] = '\0';

        // Lowercase for all comparisons
        char low[64] = {};
        memcpy(low, txt, len);
        for (size_t i = 0; i < len; i++)
            if (low[i] >= 'A' && low[i] <= 'Z') low[i] += 32;

        // ── Daycare commands ─────────────────────────────────────────────
        // Self-DMs (from our own node) are treated as local commands — response
        // goes to self without extra mesh traffic.
        uint32_t replyTo = (mp.from == nodeDB->getNodeNum()) ? nodeDB->getNodeNum() : mp.from;

        if (strstr(low, "mmd on") || strstr(low, "mmd checkin") || strstr(low, "mmd in")) {
            daycareCheckIn();
            sendTextDM(replyTo, daycare_.isActive() ? "Daycare: Pokemon checked in!" : "Daycare: No ROM loaded");
            return ProcessMessage::CONTINUE;
        }
        if (strstr(low, "mmd off") || strstr(low, "mmd checkout") || strstr(low, "mmd out")) {
            daycareCheckOut();
            sendTextDM(replyTo, "Daycare: Pokemon checked out! XP applied.");
            return ProcessMessage::CONTINUE;
        }
        // `mmd status` deliberately NOT handled as a DM — daycare status is
        // shown in the local Terminal (type "mmd" or "daycare"). Keeps the
        // mesh free of status chatter.
        if (strstr(low, "mmd test")) {
            if (!daycare_.isActive()) {
                // Auto check-in if not active
                daycareCheckIn();
            }
            if (daycare_.isActive()) {
                daycare_.forceEvent();
                const auto &evt = daycare_.getLastEvent();
                sendTextDM(mp.from, evt.message);
            } else {
                sendTextDM(mp.from, "Daycare: No ROM loaded");
            }
            return ProcessMessage::CONTINUE;
        }

        // ── Cable Club DM handlers disabled ───────────────────────────────
        // Bisect identified cable-club paths as the cause of mid-play freeze.
        // Short-circuit all cable/link/mmc/mml strings so shim state machines
        // never get triggered.
        if (strstr(low, "cable") || strstr(low, "mmc") || strstr(low, "mm link") ||
            strstr(low, "mml on") || strstr(low, "mmlon") || strstr(low, "mm waiting")) {
            return ProcessMessage::CONTINUE;
        }

        // ── PERSON 2: receives "mmc on" from Person 1 ────────────────────
        // Store pending challenge, send "MM waiting" ack so Person 1 knows
        // we received it and can then send us the formatted challenge message.
        if (strstr(low, "mmc on") || strstr(low, "cable on")) {
            if (cableOffMs_ && (millis() - cableOffMs_ < 10000))
                return ProcessMessage::CONTINUE;
            pendingChallengerFrom_ = mp.from;
            pendingChallengeMs_    = millis();
            sendTextDM(mp.from, "MM waiting");
            snprintf(setupStatusBuf_, sizeof(setupStatusBuf_),
                     "[%s] wants to link! Reply Y or N", getShortName(mp.from));
            setupStatus_ = setupStatusBuf_;
            LOG_INFO("[MonsterMesh] 'mmc on' from 0x%08X — sent MM waiting\n", (unsigned)mp.from);
            return ProcessMessage::CONTINUE;
        }

        // ── PERSON 1: receives "MM waiting" from Person 2 ────────────────
        // Person 2 got our "mmc on". Now send the human-readable challenge
        // to Person 2 so it appears in their chat.
        if (strstr(low, "mm waiting")) {
            if (cableOffMs_ && (millis() - cableOffMs_ < 10000))
                return ProcessMessage::CONTINUE;
            waitingForAcceptFrom_ = mp.from;
            char msg[64];
            snprintf(msg, sizeof(msg), "[%s] wants to link! Reply Y or N",
                     getShortName(nodeDB->getNodeNum()));
            sendTextDM(mp.from, msg);
            snprintf(setupStatusBuf_, sizeof(setupStatusBuf_),
                     "MM: Waiting for [%s]...", getShortName(mp.from));
            setupStatus_ = setupStatusBuf_;
            LOG_INFO("[MonsterMesh] MM waiting from 0x%08X — challenge sent\n", (unsigned)mp.from);
            return ProcessMessage::CONTINUE;
        }

        // ── PERSON 2: receives "mmc off" — show fled, send disconnected ack ──
        if (strstr(low, "mmc off") || strstr(low, "cable off")) {
            shim_.cancel();
            cableOffMs_ = millis();
            waitingForAcceptFrom_ = 0;
            pendingChallengerFrom_ = 0;
            snprintf(setupStatusBuf_, sizeof(setupStatusBuf_),
                     "[%s] fled!", getShortName(mp.from));
            setupStatus_ = setupStatusBuf_;
            sendTextDM(mp.from, "MM disconnected");
            LOG_INFO("[MonsterMesh] 'mmc off' from 0x%08X — sent MM disconnected\n", (unsigned)mp.from);
            return ProcessMessage::CONTINUE;
        }

        // ── "MM disconnected" — Person 1 gets confirmation, sends fled to Person 2 ──
        if (strstr(low, "mm disconnected")) {
            LOG_INFO("[MonsterMesh] MM disconnected from 0x%08X\n", (unsigned)mp.from);
            shim_.cancel();
            cableOffMs_ = millis();
            waitingForAcceptFrom_ = 0;
            pendingChallengerFrom_ = 0;
            setupStatus_ = "MM disconnected";
            // Send "[Name] fled!" to Person 2 so they see it in chat
            char msg[32];
            snprintf(msg, sizeof(msg), "[%s] fled!", getShortName(nodeDB->getNodeNum()));
            sendTextDM(mp.from, msg);
            return ProcessMessage::CONTINUE;
        }

        // ── Y/N reply: Person 2 sent Y or N DM to Person 1 ───────────────
        if (len == 1 && (txt[0] == 'Y' || txt[0] == 'y' || txt[0] == 'N' || txt[0] == 'n')) {
            bool accepted = (txt[0] == 'Y' || txt[0] == 'y');
            // MMT path — P2 (challenged) replied Y/N to P1's MMT:ON DM.
            // P1 (us) is the challenger; we're waiting for them.
            if (mmtWaitingForAcceptFrom_ != 0 && mp.from == mmtWaitingForAcceptFrom_) {
                uint32_t partner = mmtWaitingForAcceptFrom_;
                mmtWaitingForAcceptFrom_ = 0;
                if (accepted) {
                    LOG_INFO("[MonsterMesh] MMT Y from 0x%08X — deferring battle start\n",
                             (unsigned)partner);
                    // Defer terminal LVGL work to runOnce; sending DMs here is fine.
                    uint32_t seed = (uint32_t)millis();
                    char buf[32];
                    snprintf(buf, sizeof(buf), "MMT:ACCEPT:%08X", (unsigned)seed);
                    sendTextDM(partner, buf);
                    pendingMmtStartPartner_ = partner;
                    pendingMmtStartSeed_    = seed;
                } else {
                    LOG_INFO("[MonsterMesh] MMT N from 0x%08X — declined\n", (unsigned)partner);
                    pendingMmtReject_ = partner;  // terminal work happens in runOnce
                }
                return ProcessMessage::CONTINUE;
            }
            if (waitingForAcceptFrom_ != 0 && mp.from == waitingForAcceptFrom_) {
                uint32_t partner = waitingForAcceptFrom_;
                waitingForAcceptFrom_ = 0;
                if (accepted) {
                    LOG_INFO("[MonsterMesh] Y from 0x%08X — pairing\n", (unsigned)partner);
                    shim_.pairWith(partner);
                    snprintf(setupStatusBuf_, sizeof(setupStatusBuf_),
                             "MonsterMesh linked!");
                    setupStatus_ = setupStatusBuf_;
                    sendTextDM(partner, "MonsterMesh linked!");
                } else {
                    LOG_INFO("[MonsterMesh] N from 0x%08X — declined\n", (unsigned)partner);
                    snprintf(setupStatusBuf_, sizeof(setupStatusBuf_),
                             "[%s] fled!", getShortName(partner));
                    setupStatus_ = setupStatusBuf_;
                    // Ask Person 2 to send "[Name] fled!" so it appears in Person 1's chat
                    sendTextDM(partner, "MM rejected");
                }
            }
            return ProcessMessage::CONTINUE;
        }

        // ── "MM rejected" — Person 2 sends "[Name] fled!" to Person 1's chat ──
        if (strstr(low, "mm rejected")) {
            LOG_INFO("[MonsterMesh] MM rejected from 0x%08X — sending fled\n", (unsigned)mp.from);
            pendingChallengerFrom_ = 0;
            pendingChallengeMs_    = 0;
            char msg[32];
            snprintf(msg, sizeof(msg), "[%s] fled!", getShortName(nodeDB->getNodeNum()));
            sendTextDM(mp.from, msg);
            return ProcessMessage::CONTINUE;
        }

        // ── "MonsterMesh linked!" — Person 2 pairs; if already paired, just ack ──
        if (strstr(low, "monstermesh linked")) {
            LOG_INFO("[MonsterMesh] MonsterMesh linked from 0x%08X\n", (unsigned)mp.from);
            if (pendingChallengerFrom_ == mp.from) {
                // We are Person 2 — pair and send back confirmation
                pendingChallengerFrom_ = 0;
                pendingChallengeMs_    = 0;
                shim_.pairWith(mp.from);
                sendTextDM(mp.from, "MonsterMesh linked!");
            }
            setupStatus_ = "MonsterMesh linked!";
            return ProcessMessage::CONTINUE;
        }

        // ── "fled!" — partner disconnected ───────────────────────────────
        if (strstr(low, "fled!")) {
            LOG_INFO("[MonsterMesh] partner disconnected\n");
            shim_.cancel();
            cableOffMs_ = millis();
            waitingForAcceptFrom_ = 0;
            pendingChallengerFrom_ = 0;
            snprintf(setupStatusBuf_, sizeof(setupStatusBuf_),
                     "[%s] fled!", getShortName(mp.from));
            setupStatus_ = setupStatusBuf_;
            return ProcessMessage::CONTINUE;
        }

        // ── mml on — link cable alias (same as mmc on) ───────────────────
        if (strstr(low, "mml on") || strstr(low, "mmlon") || strstr(low, "mm link on")) {
            // Treat as mmc on — rewrite and fall through by re-entering same handler
            if (mp.from != nodeDB->getNodeNum()) {
                // From another node: treat as cable challenge
                if (cableOffMs_ && (millis() - cableOffMs_ < 10000))
                    return ProcessMessage::CONTINUE;
                pendingChallengerFrom_ = mp.from;
                pendingChallengeMs_    = millis();
                sendTextDM(mp.from, "MM waiting");
            }
            return ProcessMessage::CONTINUE;
        }

        // ── MMT:ON — text battle challenge (DM-native) ──────────────────
        // Do NOT touch the terminal state here — the challenged user handles
        // everything via phone DMs. Only the MMT:ACCEPT:<seed> response
        // brings the terminal into the battle.
        //
        // If the MMT:ON arrived as a bare signal (no accompanying prompt),
        // self-DM the user so their phone chat shows Y/N instructions. If
        // the DM from the challenger already carries human-readable text
        // (the terminal-sent challenge format), skip the self-DM since
        // the user already sees the prompt in their phone chat thread.
        if (strstr(low, "mmt:on") || strstr(low, "mmt on")) {
            if (mp.from == nodeDB->getNodeNum()) return ProcessMessage::CONTINUE;
            // If the incoming DM already contains a human-readable prompt
            // (terminal-sent challenges are long), don't add anything —
            // the user already sees the Y/N instructions in their chat.
            // If it's a bare "MMT:ON", send back a prompt to the challenger
            // so THEY tell the user what to do (avoids self-DM crash).
            if (len <= 15) {  // bare MMT:ON
                char prompt[96];
                snprintf(prompt, sizeof(prompt),
                         "Tell [%s] to DM you 'Y' or 'N' to accept/decline.",
                         getShortName(nodeDB->getNodeNum()));
                sendTextDM(mp.from, prompt);
            }
            return ProcessMessage::CONTINUE;
        }

        // (MMT Y/N DM reply handled in the single-letter Y/N handler below —
        //  see `mmtWaitingForAcceptFrom_`.)

        // ── MMT:ACCEPT:<seed> — challenge accepted, start battle ──────────
        if (strncmp(low, "mmt:accept:", 11) == 0 && mp.from != nodeDB->getNodeNum()) {
            uint32_t seed = (uint32_t)strtoul(low + 11, nullptr, 16);
            if (terminal_.ready()) {
                // Build opponent party from daycare neighbor list
                Gen1Party oppParty{};
                const DaycareNeighborPokemon *peers = daycare_.getNeighbors();
                uint8_t count = daycare_.getNeighborCount();
                for (uint8_t i = 0; i < count; i++) {
                    if (peers[i].nodeId == mp.from) {
                        terminal_.buildAsyncOpponent(peers[i], oppParty);
                        break;
                    }
                }
                terminal_.receiveNetAccept(mp.from, seed, oppParty);
            }
            return ProcessMessage::CONTINUE;
        }

        // ── MMT:REJECT — opponent declined our challenge ──────────────────
        if (strncmp(low, "mmt:reject", 10) == 0 && mp.from != nodeDB->getNodeNum()) {
            if (terminal_.ready()) {
                terminal_.receiveNetReject(mp.from);
            }
            return ProcessMessage::CONTINUE;
        }

        // ── MMT:ACT:<type>:<index> — opponent's live battle action ────────
        if (strncmp(low, "mmt:act:", 8) == 0 && mp.from != nodeDB->getNodeNum()) {
            uint8_t act = (uint8_t)strtoul(low + 8, nullptr, 10);
            const char *colon = strchr(low + 8, ':');
            uint8_t idx = colon ? (uint8_t)strtoul(colon + 1, nullptr, 10) : 0;
            if (terminal_.ready()) {
                terminal_.receiveNetAction(act, idx);
            }
            return ProcessMessage::CONTINUE;
        }

        return ProcessMessage::CONTINUE;
    }

    // ── Binary PRIVATE_APP packets ─────────────────────────────────────
    if (mp.decoded.payload.size > 0) {
        // Check if it's a daycare beacon (first byte matches beacon type)
        if (mp.decoded.payload.size >= sizeof(DaycareBeacon)) {
            const auto *beacon = reinterpret_cast<const DaycareBeacon *>(mp.decoded.payload.bytes);
            // Daycare beacons have type field = 0x60
            if (beacon->type == 0x60) {
                uint8_t prevNeighbors = daycare_.getNeighborCount();
                daycare_.handleBeacon(*beacon);
                // New neighbor appeared — queue notification for runOnce
                // (self-DM from handleReceived thread was crashing the device)
                if (daycare_.getNeighborCount() > prevNeighbors) {
                    LOG_INFO("[MonsterMesh] new neighbor: %s (%s)\n",
                             beacon->shortName, beacon->gameName);
                    snprintf(pendingNeighborMsg_, sizeof(pendingNeighborMsg_),
                             "Daycare: %s (%s) is nearby!",
                             beacon->shortName, beacon->gameName);
                    pendingNeighborMsgReady_ = true;

                    // Dog park arrival — Pokemon interact based on type affinity
                    if (daycare_.triggerArrivalEvent(*beacon)) {
                        const auto &evt = daycare_.getLastEvent();
                        LOG_INFO("[MonsterMesh] arrival event: %s\n", evt.message);
                        snprintf(pendingArrivalMsg_, sizeof(pendingArrivalMsg_),
                                 "%s", evt.message);
                        pendingArrivalTargetNode_ = evt.targetNodeId;
                        pendingArrivalMsgReady_ = true;
                    }
                }
                return ProcessMessage::STOP;
            }
        }
        // Otherwise it's battle shim serial data
        transport_.pushReceivedPacket(
            mp.decoded.payload.bytes,
            mp.decoded.payload.size,
            mp.rx_rssi
        );
    }
    return ProcessMessage::STOP;
}

// ── runOnce() — OSThread periodic drain of tx queue ─────────────────────────

int32_t MonsterMeshModule::runOnce()
{
    // Register InputBroker observer and install keyboard hook early (after 1s).
    // The hook passes through to the original Meshtastic driver when MM is inactive,
    // so keyboard/touch work normally. ALT button detection runs from both states.
    if (!kbObserverRegistered_ && millis() > 1000 && inputBroker) {
        kbObserverRegistered_ = true;
        installKeyboardHook();
        inputObserver_.observe(inputBroker);
    }

    // One-time fix: channel_num=50 left over from a bad SHORT_TURBO config puts
    // frequency out of the US band regardless of which preset is now selected.
    // Only clear channel_num — keep whatever preset the user has chosen.
    {
        static bool loraChecked = false;
        if (!loraChecked && millis() > 2000) {
            loraChecked = true;
            auto &lora = config.lora;
            if (lora.channel_num > 26) {
                LOG_INFO("[MonsterMesh] LoRa channel_num=%d out of range, resetting to 0\n",
                         (int)lora.channel_num);
                lora.channel_num = 0;
                lora.tx_enabled  = true;
                nodeDB->saveToDisk(SEGMENT_CONFIG);
                rebootAtMsec = millis() + 2000;
            }
        }
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
            // ── Transport ────────────────────────────────────────────────────
            transport_.begin();
            transport_.setNodeId(nodeDB->getNodeNum());

            // ── Shim + Lobby ─────────────────────────────────────────────────
            shim_.begin();
            shim_.setLobby(&lobby_);
            lobby_.setShim(&shim_);
            lobby_.loadStats();

            // ── Wire serial link ─────────────────────────────────────────────
            emu_.setSerialLink(&shim_);

            // ── Ensure MonsterMesh channel exists ────────────────────────────
            ensureMonsterMeshChannel();

            // Wire the terminal's daycare-status callback so typing `mmd`
            // or `daycare` in the terminal prints local status without
            // a mesh DM round-trip.
            terminal_.setDaycareStatusFn(
                [](void *ctx, char *buf, size_t n) {
                    static_cast<MonsterMeshModule *>(ctx)->daycareStatusString(buf, n);
                }, this);

            // ── Daycare ─────────────────────────────────────────────────────
            daycare_.init();
            daycare_.setSendDm([](uint32_t dest, const char *msg, void *ctx) {
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                self->sendTextDM(dest, msg);
            }, this);
            daycare_.setBroadcast([](const char *msg, void *ctx) {
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                // Broadcast on primary channel
                self->sendTextDM(NODENUM_BROADCAST, msg);
            }, this);
            daycare_.setSendBeacon([](const DaycareBeacon &beaconIn, void *ctx) {
                auto *self = static_cast<MonsterMeshModule *>(ctx);
                // Fill nodeId and send as binary PRIVATE_APP packet
                DaycareBeacon beacon = beaconIn;
                beacon.nodeId = nodeDB->getNodeNum();
                meshtastic_MeshPacket *p = router->allocForSending();
                p->to = NODENUM_BROADCAST;
                p->channel = channels.getPrimaryIndex();
                p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
                size_t sz = sizeof(DaycareBeacon);
                if (sz > sizeof(p->decoded.payload.bytes)) sz = sizeof(p->decoded.payload.bytes);
                memcpy(p->decoded.payload.bytes, &beacon, sz);
                p->decoded.payload.size = sz;
                service->sendToMesh(p);
            }, this);
        }


        // Mount SD — suppress LVGL flush first so SPI.begin() doesn't race
        // with LGFX DMA on the shared SPI bus.
        snprintf(setupStatusBuf_, sizeof(setupStatusBuf_), "SD attempt %d/%d...", setupRetries_ + 1, MAX_SETUP_RETRIES);
        setupStatus_ = setupStatusBuf_;
#if HAS_TFT
        lv_display_t *sdDisp = lv_display_get_default();
        void *sdSavedCb = sdDisp ? (void *)sdDisp->flush_cb : nullptr;
        if (sdDisp) {
            lv_display_set_flush_cb(sdDisp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                lv_display_flush_ready(d);
            });
            if (sdDisp->refr_timer) lv_timer_pause(sdDisp->refr_timer);
        }
#endif
        bool sdOk = false;
        {
            concurrency::LockGuard g(spiLock);
            SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
            sdOk = SD.begin(SDCARD_CS, SPI, 4000000U);
        }
#if HAS_TFT
        if (sdDisp && sdSavedCb)
            lv_display_set_flush_cb(sdDisp, (lv_display_flush_cb_t)sdSavedCb);
        if (sdDisp && sdDisp->refr_timer) lv_timer_resume(sdDisp->refr_timer);
        if (sdDisp) lv_obj_invalidate(lv_screen_active());
#endif
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
            return 2000;
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

        // SD ready — stay in Meshtastic UI. User opens MonsterMesh via Tools or mic button.
        setupStatus_ = "MonsterMesh ready";
        LOG_INFO("[MonsterMesh] SD ready — waiting for user to open MonsterMesh\n");

        // Defer auto-daycare to next runOnce() tick — doing 32KB SD read
        // inline here blocks spiLock too long and starves the radio task.
        pendingAutoCheckin_ = true;
    }

    // Deferred auto-daycare check-in. Each SD operation inside daycareAutoCheckIn()
    // is wrapped in spiLock, which prevents LGFX DMA from running concurrently.
    // We do NOT pause the LVGL flush timer here — keeping LVGL alive prevents the
    // task watchdog from firing if SD.begin() is slow on cold boot.
    if (pendingAutoCheckin_ && !emulatorActive_ && !browserActive_) {
        pendingAutoCheckin_ = false;
        daycareAutoCheckIn();
        daycareCheckinDone_ = true;
    }

    // Deferred terminal init — LVGL screen objects may not exist at setupDone_ time.
#if HAS_TFT
    if (!terminal_.ready() && objects.mm_terminal_output && objects.mm_terminal_input) {
        terminal_.init(objects.mm_terminal_output, objects.mm_terminal_input);
        LOG_INFO("[MonsterMesh] Terminal initialized\n");
    }
    if (terminal_.ready() && terminal_.needsPartyLoad() && terminalPartyReady_) {
        terminal_.loadParty(terminalParty_);
        LOG_INFO("[MonsterMesh] Terminal party loaded from cache\n");
    }
    if (terminal_.ready()) {
        terminal_.setMeshPeers(daycare_.getNeighbors(), daycare_.getNeighborCount());
        terminal_.setLocalShortName(getShortName(nodeDB->getNodeNum()));

        // Drain async battle result DM
        if (terminal_.hasPendingDM()) {
            uint32_t t = terminal_.dmTarget();
            if (t == 0) t = nodeDB->getNodeNum();  // 0 = self (mml on)
            sendTextDM(t, terminal_.dmText());
            terminal_.clearPendingDM();
        }

        // Drain live PvP action
        if (terminal_.hasNetAction() && terminal_.netPartnerNodeId() != 0) {
            char act[16];
            snprintf(act, sizeof(act), "MMT:ACT:%u:%u",
                     (unsigned)terminal_.netAction(),
                     (unsigned)terminal_.netIndex());
            sendTextDM(terminal_.netPartnerNodeId(), act);
            terminal_.clearNetAction();
        }

        // Drain net challenge — send targeted DM with human text + MMT:ON signal
        if (terminal_.hasPendingNetChallenge()) {
            uint32_t target = terminal_.netChallengeTarget();
            terminal_.clearPendingNetChallenge();
            if (target != 0) {
                char challengeDM[128];
                snprintf(challengeDM, sizeof(challengeDM),
                         "[%s] wants a text battle! Reply Y or N in DM. MMT:ON",
                         getShortName(nodeDB->getNodeNum()));
                sendTextDM(target, challengeDM);
                // Record pending — single-letter Y/N DM back from target resolves it
                mmtWaitingForAcceptFrom_ = target;
            } else {
                sendTextDM(NODENUM_BROADCAST, "MMT:ON");
            }
        }
    }
#endif

    // Keep keyboard hook installed while MonsterMesh is active
    if (emulatorActive_ || browserActive_) {
        installKeyboardHook();
    }

    // Mirror emulator/browser active state to the global flag the patched
    // device-ui LGFXDriver reads to suppress powersave during play.
    bool prevActive = g_mmEmulatorActive;
    g_mmEmulatorActive = (emulatorActive_ || browserActive_);

    // Mode switch: when emu/browser becomes active, disable LoRa IRQ so the
    // radio stops hitting the shared SPI bus during gameplay. When exiting,
    // delay startReceive by 2 seconds so the SAV write + LVGL re-init can
    // complete without SPI contention crashing the eject path.
    static uint32_t pendingRadioResumeMs = 0;
    if (prevActive != g_mmEmulatorActive && RadioLibInterface::instance) {
        if (g_mmEmulatorActive) {
            LOG_INFO("[MonsterMesh] mode→EMU: disableInterrupt\n");
            RadioLibInterface::instance->disableInterrupt();
            pendingRadioResumeMs = 0;
        } else {
            // Defer radio resume to avoid SPI contention with SAV save
            pendingRadioResumeMs = millis() + 2000;
            LOG_INFO("[MonsterMesh] mode→MESH: defer startReceive 2s\n");
        }
    }
    if (pendingRadioResumeMs != 0 && millis() >= pendingRadioResumeMs &&
        !emulatorActive_ && !browserActive_ && !pendingSave_ &&
        RadioLibInterface::instance) {
        LOG_INFO("[MonsterMesh] mode→MESH: startReceive now\n");
        RadioLibInterface::instance->startReceive();
        pendingRadioResumeMs = 0;
    }

    // NOTE: LVGL inactivity-timer reset moved to LGFXDriver::task_handler
    // because LVGL APIs are not thread-safe from arbitrary FreeRTOS tasks;
    // calling lv_display_trigger_activity from MonsterMesh's runOnce task
    // corrupted LVGL state and froze the ROM loader UI.

    // Keep PowerFSM awake while emulator or browser is active.
    // CRITICAL: throttle to once per minute. Firing EVENT_INPUT every
    // runOnce tick (~50Hz) causes PowerFSM::onEnter("ON") to re-execute
    // every tick, which calls setBluetoothEnable(true) + screen->setOn()
    // 50 times per second — that's the cross-core stall that has been
    // mis-diagnosed as an "emulator freeze" for 80+ commits.
    // PowerFSM's inactivity timeout is minutes; 60s keep-alive is plenty.
    if (emulatorActive_ || browserActive_) {
        static uint32_t lastKeepAliveMs = 0;
        uint32_t now = millis();
        if (now - lastKeepAliveMs > 60000 || lastKeepAliveMs == 0) {
            lastKeepAliveMs = now;
            powerFSM.trigger(EVENT_INPUT);
        }
    }

    // Re-suppress LVGL flush if emulator is active — screen sleep/wake
    // may restore the real flush callback behind our back.
    // Also keep the LVGL refresh timer paused so cursor blink doesn't bleed through.
    // Browser uses LVGL directly so no suppression needed for it.
#if HAS_TFT
    if (emulatorActive_ && savedFlushCb_) {
        lv_display_t *disp = lv_display_get_default();
        if (disp) {
            if (disp->flush_cb != nullptr) {
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
                    lv_display_flush_ready(d);
                });
            }
            if (disp->refr_timer) lv_timer_pause(disp->refr_timer);
        }
    }
#endif

    // blitFrame() moved to emulator task on Core 1 — runOnce() is too slow for screen updates

    // Deferred browser open — SD ops must not run on the LVGL task
    if (pendingBrowserOpen_) {
        pendingBrowserOpen_ = false;
        LOG_INFO("[MonsterMesh] browser open (runOnce) emuInit=%d\n", (int)emuInitialized_);
        // Pause LVGL rendering during SD scan to avoid SPI bus contention
        // (LovyanGFX TFT flush and SD share the SPI bus)
#if HAS_TFT
        lv_display_t *disp = lv_display_get_default();
        if (disp && disp->refr_timer) lv_timer_pause(disp->refr_timer);
#endif
        browser_.open("/", emuInitialized_);
        LOG_INFO("[MonsterMesh] browser.open done count=%d\n", browser_.count());
#if HAS_TFT
        // Restore LVGL flush + resume timer now that SD is done
        if (disp) {
            if (savedFlushCb_) {
                lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                savedFlushCb_ = nullptr;
            }
            if (disp->refr_timer) lv_timer_resume(disp->refr_timer);
        }
#endif
    }

    // Process buffered browser keys and render
    if (browserActive_) {
        uint8_t key = pendingBrowserKey_;
        if (key != 0) {
            pendingBrowserKey_ = 0;
            LOG_DEBUG("[MonsterMesh] browser key=0x%02X cursor=%d count=%d\n",
                      key, browser_.cursor(), browser_.count());
            // handleKey may call scan() on dir change — suppress LVGL flush + pause timer
            // to avoid SPI bus contention (SD reinit vs LGFX DMA on shared bus)
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            void *bkSavedCb = disp ? (void *)disp->flush_cb : nullptr;
            if (disp) {
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                    lv_display_flush_ready(d);
                });
                if (disp->refr_timer) lv_timer_pause(disp->refr_timer);
            }
#endif
            bool selected = browser_.handleKey(key);
#if HAS_TFT
            if (disp && bkSavedCb) lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)bkSavedCb);
            if (disp && disp->refr_timer) lv_timer_resume(disp->refr_timer);
#endif
            LOG_DEBUG("[MonsterMesh] handleKey returned selected=%d ejected=%d\n",
                      (int)selected, (int)browser_.isEjected());
            if (selected) {
                if (browser_.isEjected()) {
                    // Eject cart confirmed — stop emulator, go to Meshtastic
                    LOG_INFO("[MonsterMesh] cart ejected by user\n");
                    if (emu_.audio_) emu_.audio_->setMuted(true);
                    if (emu_.isRunning()) pendingSave_ = true;
                    emuInitialized_ = false;  // emu task loop will idle
                    emulatorActive_ = false;
                    browserActive_ = false;
#if HAS_TFT
                    lvgl_hide_browser();
                    // Restore LVGL display — flush cb + refresh timer
                    lv_display_t *ejDisp = lv_display_get_default();
                    if (ejDisp) {
                        if (savedFlushCb_) {
                            lv_display_set_flush_cb(ejDisp, (lv_display_flush_cb_t)savedFlushCb_);
                            savedFlushCb_ = nullptr;
                        }
                        if (ejDisp->refr_timer) lv_timer_resume(ejDisp->refr_timer);
                        lv_obj_invalidate(lv_screen_active());
                    }
#endif
                    kbSetMode(false);
                    setupStatus_ = "Cart ejected";
                } else {
                    LOG_DEBUG("[MonsterMesh] selectedPath='%s'\n", browser_.selectedPath());
                    launchROM(browser_.selectedPath());
                }
            }
        }
        renderBrowser();
    }

    // Deferred save — triggered on emulator exit, done here outside LVGL callback
    if (pendingSave_) {
        pendingSave_ = false;
        emu_.save();
    }

    // Simulated weather — rotates every 3 hours using Unix time as seed.
    // Gives daycare weather events without requiring WiFi.
    {
        static uint32_t lastWeatherWindowMs = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastWeatherWindowMs >= 60000) {  // check once per minute
            lastWeatherWindowMs = nowMs;
            uint32_t t = getTime();
            if (t > 1000000000u) {  // valid RTC
                uint32_t window = t / (3 * 3600);  // changes every 3 hours
                static uint32_t lastWindow = 0;
                if (window != lastWindow) {
                    lastWindow = window;
                    // Hash window to pick weather
                    uint32_t h = window * 2654435761u;
                    static const DaycareWeatherType kWeather[] = {
                        WEATHER_CLEAR, WEATHER_CLEAR, WEATHER_CLEAR,
                        WEATHER_RAIN, WEATHER_RAIN,
                        WEATHER_WINDY, WEATHER_WINDY,
                        WEATHER_FOG,
                        WEATHER_THUNDERSTORM,
                        WEATHER_HOT,
                        WEATHER_COLD,
                        WEATHER_SNOW,
                    };
                    DaycareWeatherType wt = kWeather[h % 12];
                    int8_t tempC = (int8_t)(((h >> 8) % 30) - 5);  // -5..24°C
                    uint8_t wind = (uint8_t)((h >> 16) % 15);
                    daycare_.setWeather(wt, tempC, wind);
                }
            }
        }
    }

    // Daycare tick — only in Meshtastic mode (not while emu/browser active).
    // Generates events every 5 min, sends beacons, autosaves. Skipping it
    // during ROM play prevents background mesh/SD work from competing with
    // the emulator on the shared SPI bus.
    if (!emulatorActive_ && !browserActive_ && daycare_.isActive()) {
        uint32_t prevEventTime = daycare_.getLastEventTime();
        daycare_.tick(millis());
        if (daycare_.getLastEventTime() != prevEventTime && daycare_.getLastEventTime() != 0) {
            const auto &evt = daycare_.getLastEvent();
            if (evt.message[0]) {
                // Local user sees their event
                sendTextDM(nodeDB->getNodeNum(), evt.message);
                LOG_INFO("[MonsterMesh] event DM: %s\n", evt.message);
                // If event involved a remote trainer, DM them too so both
                // sides see the interaction in their phone chat.
                if (evt.targetNodeId != 0 &&
                    evt.targetNodeId != nodeDB->getNodeNum()) {
                    sendTextDM(evt.targetNodeId, evt.message);
                    LOG_INFO("[MonsterMesh] event DM → peer 0x%08X\n",
                             (unsigned)evt.targetNodeId);
                }
            }
        }
    }

    drainTxQueue();

    // Process deferred MMT battle-start from handleReceived Y path.
    // We land here on the OSThread, which is safe for LVGL terminal_ calls.
    if (pendingMmtStartPartner_ != 0 && terminal_.ready()) {
        uint32_t partner = pendingMmtStartPartner_;
        uint32_t seed    = pendingMmtStartSeed_;
        pendingMmtStartPartner_ = 0;
        pendingMmtStartSeed_    = 0;
        Gen1Party oppParty{};
        const DaycareNeighborPokemon *peers = daycare_.getNeighbors();
        uint8_t count = daycare_.getNeighborCount();
        for (uint8_t i = 0; i < count; i++) {
            if (peers[i].nodeId == partner) {
                terminal_.buildAsyncOpponent(peers[i], oppParty);
                break;
            }
        }
        terminal_.receiveNetAccept(partner, seed, oppParty);
    }
    if (pendingMmtReject_ != 0 && terminal_.ready()) {
        terminal_.receiveNetReject(pendingMmtReject_);
        pendingMmtReject_ = 0;
    }
    // Deferred daycare notifications — safe to send DMs from OSThread
    if (pendingNeighborMsgReady_) {
        pendingNeighborMsgReady_ = false;
        sendTextDM(nodeDB->getNodeNum(), pendingNeighborMsg_);
    }
    if (pendingArrivalMsgReady_) {
        pendingArrivalMsgReady_ = false;
        sendTextDM(nodeDB->getNodeNum(), pendingArrivalMsg_);
        if (pendingArrivalTargetNode_ != 0 &&
            pendingArrivalTargetNode_ != nodeDB->getNodeNum()) {
            sendTextDM(pendingArrivalTargetNode_, pendingArrivalMsg_);
        }
        pendingArrivalTargetNode_ = 0;
    }

    // Expire pending challenge after 60s — send "fled" to initiator
    if (pendingChallengerFrom_ != 0 &&
        (millis() - pendingChallengeMs_ > 60000)) {
        char msg[32];
        snprintf(msg, sizeof(msg), "[%s] fled!", getShortName(nodeDB->getNodeNum()));
        sendTextDM(pendingChallengerFrom_, msg);
        pendingChallengerFrom_ = 0;
        pendingChallengeMs_    = 0;
        LOG_INFO("[MonsterMesh] challenge timed out\n");
    }

    // Update status overlay to reflect LoRa cable link state while emulator is running
    if (setupDone_ && emu_.isRunning()) {
        auto shimState = shim_.state();
        if (shimState == MonsterMeshBattleShim::State::ADVERTISING) {
            setupStatus_ = "MM: Seeking link...";
        } else if (shimState == MonsterMeshBattleShim::State::CONNECTED ||
                   shimState == MonsterMeshBattleShim::State::IN_BATTLE) {
            snprintf(setupStatusBuf_, sizeof(setupStatusBuf_),
                     "MM: Linked! %08X", (unsigned)shim_.remoteId());
            setupStatus_ = setupStatusBuf_;
        } else if (shimState == MonsterMeshBattleShim::State::DONE) {
            setupStatus_ = "MM: Link closed";
        } else {
            setupStatus_ = "Playing!";
        }
    }

    return 50;
}

// ── getShortName() — return 4-char short name for a node, or hex fallback ────

const char *MonsterMeshModule::getShortName(uint32_t nodeId)
{
    auto *node = nodeDB->getMeshNode(nodeId);
    if (node && node->has_user && node->user.short_name[0] != '\0')
        return node->user.short_name;
    if (node && node->has_user && node->user.long_name[0] != '\0')
        return node->user.long_name;
    static char buf[10];
    snprintf(buf, sizeof(buf), "%08X", (unsigned)nodeId);
    return buf;
}

// ── sendTextDM() — send a text DM to a specific node ─────────────────────────

void MonsterMeshModule::sendTextDM(uint32_t to, const char *text)
{
    meshtastic_MeshPacket *p = router->allocForSending();
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

void MonsterMeshModule::drainTxQueue()
{
    uint8_t buf[237];
    size_t len = 0;

    // Send ONE packet per call — don't flood the router TX queue.
    // The while-loop was sending all queued packets at once, overflowing the 16-slot TX queue.
    if (!transport_.hasPendingSend()) return;
    if (!transport_.dequeueSend(buf, len, sizeof(buf))) return;

    uint32_t remoteId = shim_.remoteId();
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_WARN("[MonsterMesh] drainTxQueue: packet pool exhausted, dropping\n");
        return;
    }
    p->to = (remoteId != 0) ? remoteId : NODENUM_BROADCAST;
    // Battle serial data: use primary channel (0) for DM. PRIVATE_APP portnum means it
    // won't appear in the Meshtastic app UI. ch=255 was invalid and caused decryption failures.
    p->channel = (remoteId != 0) ? 0 : MONSTERMESH_CHANNEL;
    p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    p->decoded.payload.size = len;
    memcpy(p->decoded.payload.bytes, buf, len);
    service->sendToMesh(p);
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
// ── LVGL browser screen ───────────────────────────────────────────────────────
static lv_obj_t *g_browserLvScr   = nullptr;
static lv_obj_t *g_browserLvLabel = nullptr;
static lv_obj_t *g_prevLvScr      = nullptr;

static lv_obj_t *g_browserBorder   = nullptr;  // white border box
static lv_obj_t *g_browserInner    = nullptr;  // dark inner panel
static lv_obj_t *g_browserTitle    = nullptr;  // "GAME PAK" title bar
static lv_obj_t *g_browserFooter   = nullptr;  // bottom hint bar

static void lvgl_show_browser(MonsterMeshFileBrowser &b)
{
    // ── Classic Game Boy DMG green palette ─────────────────────────────
    // 00 lightest #E0F8D0, 01 light #88C070, 10 dark #346856, 11 darkest #081820
    static constexpr lv_color_t GB_00 = {.blue = 0xD0, .green = 0xF8, .red = 0xE0};  // lightest
    static constexpr lv_color_t GB_01 = {.blue = 0x70, .green = 0xC0, .red = 0x88};  // light
    static constexpr lv_color_t GB_10 = {.blue = 0x56, .green = 0x68, .red = 0x34};  // dark
    static constexpr lv_color_t GB_11 = {.blue = 0x20, .green = 0x18, .red = 0x08};  // darkest

    if (!g_browserLvScr) {
        // Screen background — darkest green
        g_browserLvScr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(g_browserLvScr, GB_11, 0);
        lv_obj_set_style_bg_opa(g_browserLvScr, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(g_browserLvScr, 0, 0);

        // Border box — lightest green
        g_browserBorder = lv_obj_create(g_browserLvScr);
        lv_obj_set_pos(g_browserBorder, 6, 6);
        lv_obj_set_size(g_browserBorder, 308, 228);
        lv_obj_set_style_bg_color(g_browserBorder, GB_00, 0);
        lv_obj_set_style_bg_opa(g_browserBorder, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(g_browserBorder, 4, 0);
        lv_obj_set_style_border_width(g_browserBorder, 0, 0);
        lv_obj_set_style_pad_all(g_browserBorder, 3, 0);
        lv_obj_clear_flag(g_browserBorder, LV_OBJ_FLAG_SCROLLABLE);

        // Inner panel — dark green
        g_browserInner = lv_obj_create(g_browserBorder);
        lv_obj_set_pos(g_browserInner, 0, 0);
        lv_obj_set_size(g_browserInner, 302, 222);
        lv_obj_set_style_bg_color(g_browserInner, GB_10, 0);
        lv_obj_set_style_bg_opa(g_browserInner, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(g_browserInner, 2, 0);
        lv_obj_set_style_border_width(g_browserInner, 0, 0);
        lv_obj_set_style_pad_all(g_browserInner, 0, 0);
        lv_obj_clear_flag(g_browserInner, LV_OBJ_FLAG_SCROLLABLE);

        // Title bar — light green bg, darkest text, retro pixel font
        g_browserTitle = lv_label_create(g_browserInner);
        lv_obj_set_pos(g_browserTitle, 0, 2);
        lv_obj_set_size(g_browserTitle, 302, 20);
        lv_obj_set_style_text_color(g_browserTitle, GB_11, 0);
        lv_obj_set_style_text_align(g_browserTitle, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(g_browserTitle, GB_01, 0);
        lv_obj_set_style_bg_opa(g_browserTitle, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(g_browserTitle, &lv_font_unscii_8, 0);
        lv_label_set_recolor(g_browserTitle, true);

        // File list — lightest text on dark bg, retro pixel font
        g_browserLvLabel = lv_label_create(g_browserInner);
        lv_obj_set_pos(g_browserLvLabel, 8, 24);
        lv_obj_set_size(g_browserLvLabel, 286, 170);
        lv_label_set_long_mode(g_browserLvLabel, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(g_browserLvLabel, GB_00, 0);
        lv_obj_set_style_text_font(g_browserLvLabel, &lv_font_unscii_8, 0);
        lv_label_set_recolor(g_browserLvLabel, true);

        // Footer — light green bg, darkest text, retro pixel font
        g_browserFooter = lv_label_create(g_browserInner);
        lv_obj_set_pos(g_browserFooter, 0, 198);
        lv_obj_set_size(g_browserFooter, 302, 20);
        lv_obj_set_style_text_color(g_browserFooter, GB_11, 0);
        lv_obj_set_style_text_align(g_browserFooter, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(g_browserFooter, GB_01, 0);
        lv_obj_set_style_bg_opa(g_browserFooter, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(g_browserFooter, &lv_font_unscii_8, 0);
    }

    // Title bar: "GAME PAK" + current directory
    {
        static char titleBuf[128];
        const char *dir = b.currentDir();
        if (strcmp(dir, "/") == 0)
            snprintf(titleBuf, sizeof(titleBuf), "MonsterMesh  < Select ROM >");
        else
            snprintf(titleBuf, sizeof(titleBuf), "MonsterMesh  < %s >", dir);
        lv_label_set_text(g_browserTitle, titleBuf);
    }

    // File list
    static char buf[1200];
    int pos = 0;
    if (b.count() == 0) {
        pos += snprintf(buf+pos, sizeof(buf)-pos, "\n  No ROMs found on SD card\n");
    } else {
        int visible = b.scroll();
        int end = b.scroll() + FB_VISIBLE_ROWS;
        if (end > b.count()) end = b.count();
        for (int i = visible; i < end && pos < (int)sizeof(buf)-120; i++) {
            const auto &e = b.entries()[i];
            // > arrow for selected (indents text), flush left for others
            const char *cursor = (i == b.cursor())
                ? "> "
                : " ";
            bool isEjectEntry = (strcmp(e.name, "[Eject Cart]") == 0);
            if (isEjectEntry) {
                if (b.isConfirmingEject() && i == b.cursor()) {
                    pos += snprintf(buf+pos, sizeof(buf)-pos,
                                    "%s#081820 Eject cart? K to confirm#\n", cursor);  // darkest
                } else {
                    pos += snprintf(buf+pos, sizeof(buf)-pos,
                                    "%s#081820 %s#\n", cursor, e.name);  // darkest
                }
            } else if (e.isDir) {
                pos += snprintf(buf+pos, sizeof(buf)-pos, "%s#88C070 [%s]#\n", cursor, e.name);  // light green
            } else if (e.hasSave) {
                // Save file — lightest, with star
                pos += snprintf(buf+pos, sizeof(buf)-pos, "%s#E0F8D0 %s [SAV]#\n", cursor, e.name);
            } else {
                pos += snprintf(buf+pos, sizeof(buf)-pos, "%s#88C070 %s#\n", cursor, e.name);  // light green
            }
        }
    }
    lv_label_set_text(g_browserLvLabel, buf);

    // Footer
    lv_label_set_text(g_browserFooter, "W/S:Nav  K:Select  ALT:Meshtastic");

    if (lv_screen_active() != g_browserLvScr) {
        g_prevLvScr = lv_screen_active();
        lv_screen_load(g_browserLvScr);
    }
}

static void lvgl_hide_browser()
{
    if (g_prevLvScr && lv_screen_active() == g_browserLvScr) {
        lv_screen_load(g_prevLvScr);
        g_prevLvScr = nullptr;
    }
    if (g_browserLvScr) {
        lv_obj_delete(g_browserLvScr);
        g_browserLvScr   = nullptr;
        g_browserLvLabel  = nullptr;
        g_browserBorder   = nullptr;
        g_browserInner    = nullptr;
        g_browserTitle    = nullptr;
        g_browserFooter   = nullptr;
    }
}

volatile uint8_t g_emuPaletteIdx = 0;  // index into EMU_PALETTES[]

static lv_indev_t          *g_kbIndev      = nullptr;
static lv_indev_read_cb_t   g_savedKbReadCb = nullptr;  // original LVGL keypad read cb
static bool        g_symActive    = false;  // KEY mode: SYM modifier latch
static bool        g_rawMode      = false;  // true when MCU is in RAW (5-byte) mode
static bool        g_symEConsumed = false;  // RAW mode: debounce SYM+E toggle
static bool        g_altWasHeldKey = false; // KEY mode: ALT debounce for screen toggle

// Switch keyboard MCU between KEY mode (0x04, 1 byte) and RAW mode (0x03, 5 bytes).
// T-Deck keyboard MCU (ESP32-C3 at 0x55) supports this via PR #87.
static bool g_altWasHeldRaw = false;   // RAW mode: ALT edge detection

static void kbSetMode(bool raw)
{
    Wire.beginTransmission(0x55);
    Wire.write(raw ? 0x03 : 0x04);
    Wire.endTransmission();
    g_rawMode = raw;
    // When switching modes, carry the "held" state forward so the other
    // mode's edge detector doesn't re-fire while ALT is still physically held.
    if (raw)  { g_altWasHeldRaw = g_altWasHeldKey; g_altWasHeldKey = false; }
    if (!raw) { g_altWasHeldKey = g_altWasHeldRaw; g_altWasHeldRaw = false; }
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

    bool mmActive = monsterMeshModule &&
                    (monsterMeshModule->isEmulatorActive() || monsterMeshModule->isBrowserActive());

    // Keep device-ui backlight alive while emulator or browser is active.
    if (mmActive) {
        lv_display_trigger_activity(NULL);
    }

    // ── Pass-through when MonsterMesh is not active ───────────────────────
    // Peek at RAW bytes to detect bare ALT press (toggle to MonsterMesh).
    // Switch MCU to RAW, read 5 bytes, switch back to KEY — then pass through.
    if (!mmActive) {
        Wire.beginTransmission(0x55);
        Wire.write(0x03);  // RAW mode
        Wire.endTransmission();
        Wire.requestFrom((uint8_t)0x55, (uint8_t)5);
        uint8_t rb[5] = {};
        for (int i = 0; i < 5 && Wire.available(); i++) rb[i] = Wire.read();
        Wire.beginTransmission(0x55);
        Wire.write(0x04);  // back to KEY mode
        Wire.endTransmission();
        g_rawMode = false;

        bool altNow = (rb[0] & 0x10) != 0;
        bool symNow = (rb[0] & 0x04) != 0;
        if (altNow && !g_altWasHeldKey) {
            g_altWasHeldKey = true;
            uint32_t now = millis();
            if (now - g_micLastToggleMs > 600 && monsterMeshModule) {
                g_micLastToggleMs = now;
                // Sym+Alt → 0x06 (file browser), Alt alone → 0x05
                monsterMeshModule->handleKeyFromLVGL(symNow ? 0x06 : 0x05);
                return;  // consumed — don't pass KEY event through
            }
        }
        if (!altNow) g_altWasHeldKey = false;

        if (g_savedKbReadCb) g_savedKbReadCb(indev, data);
        return;
    }

    if (g_rawMode) {
        // ── RAW mode: read 5-byte bitmask ────────────────────────────────
        Wire.requestFrom((uint8_t)0x55, (uint8_t)5);
        uint8_t b[5] = {};
        for (int i = 0; i < 5 && Wire.available(); i++) b[i] = Wire.read();

        // ALT button in RAW mode — Sym+Alt → browser (0x06), Alt alone → Meshtastic (0x05)
        bool altHeld = (b[0] & 0x10) != 0;
        bool symHeld = (b[0] & 0x04) != 0;
        if (altHeld && !g_altWasHeldRaw) {
            uint32_t now = millis();
            if (now - g_micLastToggleMs > 600) {
                g_micLastToggleMs = now;
                if (monsterMeshModule) {
                    monsterMeshModule->setJoypadDirect(0);
                    monsterMeshModule->handleKeyFromLVGL(symHeld ? 0x06 : 0x05);
                    kbSetMode(false);
                }
            }
            return;  // consumed — don't fall through to joypad/other handlers
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

        // (Sym+Alt is handled by the ALT edge detector above — no separate block needed)

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

    LOG_INFO("[MonsterMesh] KEY key=0x%02X '%c' emu=%d browser=%d\n",
             key, key >= 0x20 ? key : '?',
             monsterMeshModule ? (int)monsterMeshModule->isEmulatorActive() : -1,
             monsterMeshModule ? (int)monsterMeshModule->isBrowserActive() : -1);

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
    // Already hooked — don't reset keyboard MCU again
    if (g_kbIndev) return;

    // Guarantee keyboard MCU starts in KEY mode
    Wire.beginTransmission(0x55);
    Wire.write(0x04);
    Wire.endTransmission();
    g_rawMode    = false;
    g_symActive  = false;
    g_symEConsumed = false;

    // Find the KEYPAD indev (type 2) — that's the keyboard.
    // Type 3 (ENCODER) is the trackball — don't hook that.
    lv_indev_t *indev = nullptr;
    while ((indev = lv_indev_get_next(indev)) != nullptr) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            g_kbIndev = indev;
            g_savedKbReadCb = indev->read_cb;  // save original so we can restore on exit
            lv_indev_set_read_cb(indev, monsterMeshKeyboardRead);
            LOG_INFO("[MonsterMesh] LVGL kb hook installed (indev=%p)\n", indev);
            return;
        }
    }
    LOG_WARN("[MonsterMesh] No LVGL keypad indev found — hook not installed\n");
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
    lgfx::LGFX_Device *gfx = getGfx();
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
        // Skip emulation if ROM was ejected — task stays alive for reuse
        if (!emuInitialized_) {
            vTaskDelay(pdMS_TO_TICKS(100));
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

        // BattleShim tick disabled — cable-club feature neutralized;
        // running this per-frame was a suspected contributor to the mid-play
        // crash/freeze since it pokes at shared state every emu frame.
        // shim_.tick();

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

        // Lobby tick disabled — cable club neutralized.
        // lobby_.tick(millis());

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
                        // Lobby disabled (Cable Club neutralized)
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

    lgfx::LGFX_Device *gfx = getGfx();
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
        display->drawString(x + 64, y + 32, MONSTERMESH_VERSION);
        display->drawString(x + 64, y + 44, setupStatus_);
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

    // ── ALT key (0x05): navigate between screens ───────────────────────
    //   Browser  → Meshtastic
    //   Emulator → Meshtastic
    //   Meshtastic → File Browser
    if (ascii == 0x05) {
        if (browserActive_) {
            // ── Browser → Meshtastic ──────────────────────────────────────
            browserActive_ = false;
#if HAS_TFT
            lvgl_hide_browser();
#endif
            kbSetMode(false);
            return;
        }

        if (emulatorActive_) {
            // ── Emulator → Meshtastic ─────────────────────────────────────
            emulatorActive_ = false;
            if (emu_.isRunning()) pendingSave_ = true;
            if (emu_.audio_) emu_.audio_->setMuted(true);
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            if (disp) {
                if (savedFlushCb_) {
                    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                    savedFlushCb_ = nullptr;
                }
                if (disp->refr_timer) lv_timer_resume(disp->refr_timer);
                lv_obj_invalidate(lv_screen_active());
            }
#endif
            kbSetMode(false);
            return;
        }

        // ── Meshtastic → Emulator (if ROM loaded) or File Browser ─────
        if (!setupDone_) {
            LOG_WARN("[MonsterMesh] not ready — status: %s\n", setupStatus_);
            return;
        }
        if (emuInitialized_) {
            // ROM is loaded — switch back to emulator
            LOG_INFO("[MonsterMesh] ALT → resuming emulator\n");
            emulatorActive_ = true;
            if (emu_.audio_) emu_.audio_->setMuted(false);
#if HAS_TFT
            lv_display_t *disp = lv_display_get_default();
            if (disp) {
                savedFlushCb_ = (void *)disp->flush_cb;
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
                    lv_display_flush_ready(d);
                });
                if (disp->refr_timer) lv_timer_pause(disp->refr_timer);
                lgfx::LGFX_Device *gfx = getGfx();
                if (gfx) gfx->clearClipRect();
            }
#endif
#if HAS_SCREEN
            requestFocus();
#endif
            kbSetMode(true);
        } else {
            // No ROM loaded — open file browser
            // Wait for device-ui's notifyMessagesRestored (phone history replay
            // finished) to avoid racing LVGL state init. Fall through after
            // 10s in case no phone is connected and the hook never fires.
            if (!g_mmMessagesRestored && millis() < 10000) {
                setupStatus_ = "Waiting for UI sync...";
                LOG_INFO("[MonsterMesh] ALT ignored: messages not restored yet\n");
                return;
            }
            if (!g_mmMessagesRestored) {
                LOG_INFO("[MonsterMesh] ALT: msg-restored timeout, opening anyway\n");
                g_mmMessagesRestored = true;  // stop re-checking
            }
            if (!daycareCheckinDone_) {
                if (millis() < 3000) {
                    setupStatus_ = "Loading party...";
                    return;
                }
                daycareCheckinDone_ = true;  // give up waiting, open browser
            }
            LOG_INFO("[MonsterMesh] ALT → opening browser\n");
            kbSetMode(false);  // ensure KEY mode for browser navigation
            browserActive_ = true;
            pendingBrowserOpen_ = true;
        }
        return;
    }

    // ── Sym+Alt (0x06): toggle between File Browser and Emulator ──────
    if (ascii == 0x06) {
        if (browserActive_ && emuInitialized_) {
            // ── Browser → Emulator ────────────────────────────────────────
            browserActive_ = false;
#if HAS_TFT
            lvgl_hide_browser();
            lv_display_t *disp = lv_display_get_default();
            if (disp) {
                savedFlushCb_ = (void *)disp->flush_cb;
                lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
                    lv_display_flush_ready(d);
                });
                if (disp->refr_timer) lv_timer_pause(disp->refr_timer);
                lgfx::LGFX_Device *gfx = getGfx();
                if (gfx) gfx->clearClipRect();
            }
#endif
            emulatorActive_ = true;
            if (emu_.audio_) emu_.audio_->setMuted(false);
#if HAS_SCREEN
            requestFocus();
#endif
            kbSetMode(true);
            return;
        }
        if (browserActive_) return;  // no ROM loaded, stay in browser

        if (emulatorActive_) {
            // ── Emulator → File Browser ───────────────────────────────────
            emulatorActive_ = false;
            if (emu_.isRunning()) pendingSave_ = true;
            if (emu_.audio_) emu_.audio_->setMuted(true);
            // Keep LVGL flush suppressed — restore it only when the browser
            // LVGL screen is actually created (in runOnce/pendingBrowserOpen).
            // Resuming the timer here would let Meshtastic UI bleed through.
        }

        if (!setupDone_) {
            LOG_WARN("[MonsterMesh] not ready — status: %s\n", setupStatus_);
            return;
        }
        if (!daycareCheckinDone_) {
            if (millis() < 15000) {
                setupStatus_ = "Loading party...";
                return;
            }
            daycareCheckinDone_ = true;
        }
        LOG_INFO("[MonsterMesh] Sym+Alt → opening browser\n");
        browserActive_ = true;
        pendingBrowserOpen_ = true;
        return;
    }

    // ── File browser key handling ──────────────────────────────────────
    if (browserActive_) {
        // All keys buffered for processing in runOnce — avoid doing SPI/rendering
        // from within the LVGL callback context. L/Backspace = go up dir (handled by browser).
        LOG_INFO("[MonsterMesh] browser key buffered: 0x%02X '%c'\n", ascii, ascii >= 0x20 ? ascii : '?');
        pendingBrowserKey_ = ascii;
        return;
    }

    if (!emulatorActive_) return;

    // ── Tab: debug overlay toggle ──────────────────────────────────────
    if (ascii == 0x09) {
        debugActive_ = !debugActive_;
        return;
    }

    // Cable Club (lobby) disabled: bisect identified the link-cable code paths
    // as the source of the emulator video freeze. All entry points neutralized.
    if (ascii == 'p' || ascii == 'P') {
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

// ── renderBrowser() — draw file browser to TFT ─────────────────────────────

void MonsterMeshModule::renderBrowser()
{
#if HAS_TFT
    if (!browser_.isDirty()) return;
    browser_.clearDirty();
    // Update the LVGL label with current file list
    lvgl_show_browser(browser_);
#endif
}

// ── launchROM() — load selected ROM and start emulator ──────────────────────

void MonsterMeshModule::launchROM(const char *path)
{
    // Show "Loading ROM..." then immediately suppress LVGL so the SD read
    // doesn't race with the TFT flush on the shared SPI bus.
#if HAS_TFT
    if (g_browserLvLabel) {
        lv_label_set_text(g_browserLvLabel, "\n\n\n     Loading ROM...");
        lv_refr_now(lv_display_get_default());
    }
    // Pause LVGL BEFORE any SD I/O — TFT and SD share the SPI bus.
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        savedFlushCb_ = (void *)disp->flush_cb;
        lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
            lv_display_flush_ready(d);
        });
        if (disp->refr_timer) lv_timer_pause(disp->refr_timer);
    }
#endif

    // Browser returns SD-relative paths like "/pokemon.gb"
    // POSIX fopen needs VFS path "/sd/pokemon.gb"
    char vfsPath[FB_MAX_PATH + 4];
    snprintf(vfsPath, sizeof(vfsPath), "/sd%s", path);
    LOG_INFO("[MonsterMesh] Launching ROM: %s\n", vfsPath);

    bool romOk = emu_.begin(vfsPath);
    if (!romOk) {
        LOG_WARN("[MonsterMesh] Failed to load ROM: %s\n", vfsPath);
        snprintf(setupStatusBuf_, sizeof(setupStatusBuf_), "FAIL: %s", vfsPath);
        setupStatus_ = setupStatusBuf_;
        // Restore LVGL before returning to browser
#if HAS_TFT
        if (disp && savedFlushCb_) {
            lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
            savedFlushCb_ = nullptr;
        }
        if (disp && disp->refr_timer) lv_timer_resume(disp->refr_timer);
#endif
        browser_.markDirty();
        return;
    }

    // Bail early if PSRAM framebuffer can't be allocated
    if (!frameBuf_) {
        frameBuf_ = static_cast<uint16_t *>(ps_malloc(PM_DISP_W * PM_DISP_H * sizeof(uint16_t)));
        if (!frameBuf_) {
            LOG_WARN("[MonsterMesh] PSRAM framebuffer alloc failed — aborting ROM launch\n");
#if HAS_TFT
            if (disp && savedFlushCb_) {
                lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)savedFlushCb_);
                savedFlushCb_ = nullptr;
            }
            if (disp && disp->refr_timer) lv_timer_resume(disp->refr_timer);
#endif
            browser_.markDirty();
            return;
        }
        memset(frameBuf_, 0, PM_DISP_W * PM_DISP_H * sizeof(uint16_t));
    }

    emuInitialized_ = true;
    browserActive_ = false;
#if HAS_TFT
    lvgl_hide_browser();
#endif
    emulatorActive_ = true;
    kbSetMode(true);  // switch keyboard to RAW mode for emulator input

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
            8192, this, 2, &renderTaskHandle_, 0
        );
    }

    // Set up LGFX for emulator rendering (LVGL flush already suppressed)
#if HAS_TFT
    lgfx::LGFX_Device *gfx = getGfx();
    if (gfx) {
        gfx->clearClipRect();
        // Reset display scroll/window state and clear to black so the first
        // frame has a clean canvas (prevents horizontal sync artifact on load)
        gfx->setScrollRect(0, 0, 0, 0, 0);
        concurrency::LockGuard _g(spiLock);
        gfx->fillScreen(TFT_BLACK);
    }
#endif

    kbSetMode(true);  // RAW mode for held-key d-pad input
    setupStatus_ = "Playing!";
    LOG_INFO("[MonsterMesh] ROM loaded, emulator started\n");

    // Remember this ROM path for auto-daycare on next boot
    {
        concurrency::LockGuard g(spiLock);
        File lrf = SD.open("/last_rom.txt", FILE_WRITE);
        if (lrf) {
            lrf.print(path);  // SD-relative path like "/pokemon.gb"
            lrf.close();
            LOG_INFO("[MonsterMesh] saved last ROM path: %s\n", path);
        }
    }

    // Auto check-in to daycare — SRAM is already loaded from the .sav file,
    // so we can read party data directly without calling emu_.save() (which
    // would do SD I/O and deadlock with the emulator task we just started).
    {
        char gameName[8] = {};
        const uint8_t *sram = emu_.cartRam_;
        for (int i = 0; i < 7; i++) {
            uint8_t c = sram[0x2598 + i];  // Gen 1 player name offset
            if (c == 0x50) break;
            gameName[i] = gen1CharToAscii(c);
        }
        const char *shortName = getShortName(nodeDB->getNodeNum());
        daycare_.checkIn(sram, shortName, gameName);
        daycare_.forceBeacon();
        LOG_INFO("[MonsterMesh] daycare auto-checked in: trainer='%s' party=%d, beacon sent\n",
                 gameName, daycare_.getState().partyCount);
    }
}

// ── Daycare ─────────────────────────────────────────────────────────────────

// Gen 1 player name is at offset 0x2598 in the SRAM save file
static constexpr uint16_t SAV_PLAYER_NAME = 0x2598;

void MonsterMeshModule::daycareCheckIn()
{
    if (!emuInitialized_ || !emu_.isRunning()) {
        LOG_WARN("[MonsterMesh] daycare checkin: no ROM loaded\n");
        return;
    }

    // Read trainer name from SRAM (Gen 1 offset 0x2598, encoded in Gen 1 charset)
    char gameName[8] = {};
    const uint8_t *sram = emu_.cartRam_;
    for (int i = 0; i < 7; i++) {
        uint8_t c = sram[SAV_PLAYER_NAME + i];
        if (c == 0x50) break;  // Gen 1 string terminator
        gameName[i] = gen1CharToAscii(c);
    }

    // Get Meshtastic short name
    const char *shortName = getShortName(nodeDB->getNodeNum());

    LOG_INFO("[MonsterMesh] daycare checkin: trainer='%s' node='%s'\n", gameName, shortName);

    // Save emulator state first so SRAM is up to date
    emu_.save();

    daycare_.checkIn(sram, shortName, gameName);

    const auto &state = daycare_.getState();
    LOG_INFO("[MonsterMesh] daycare active: %d pokemon checked in\n", state.partyCount);
}

void MonsterMeshModule::daycareCheckOut()
{
    if (!daycare_.isActive()) {
        LOG_WARN("[MonsterMesh] daycare checkout: not active\n");
        return;
    }

    // Write XP back to SRAM if emulator is running
    if (emuInitialized_ && emu_.isRunning()) {
        daycare_.checkOut(emu_.cartRam_);
        emu_.save();  // persist patched SRAM to SD
        LOG_INFO("[MonsterMesh] daycare checkout: XP written back to SRAM + saved\n");
    } else {
        daycare_.checkOut(nullptr);  // just stop daycare, no SRAM patch
        LOG_INFO("[MonsterMesh] daycare checkout: no emulator, state saved\n");
    }
}

// Fill buf with a human-readable daycare status string.
// Used by the Terminal to display status locally (no DM round-trip).
void MonsterMeshModule::daycareStatusString(char *buf, size_t bufsize)
{
    if (!buf || bufsize < 16) return;
    int pos = 0;

    if (!daycare_.isActive()) {
        snprintf(buf, bufsize, "Daycare: Not active (no SAV loaded).");
        return;
    }

    const auto &state = daycare_.getState();
    pos += snprintf(buf + pos, bufsize - pos, "Daycare: %d Pokemon\n", state.partyCount);
    for (uint8_t i = 0; i < state.partyCount && i < 6; i++) {
        const auto &p = state.pokemon[i];
        const char *name = p.nickname[0] ? p.nickname : "???";
        uint8_t level = p.savLevel + p.totalLevelsGained;
        pos += snprintf(buf + pos, bufsize - pos,
                        "%s Lv%d +%luXP\n", name, level, (unsigned long)p.totalXpGained);
        if (pos >= (int)bufsize - 40) break;
    }
    snprintf(buf + pos, bufsize - pos, "Neighbors: %d", daycare_.getNeighborCount());
}

void MonsterMeshModule::daycareStatus(uint32_t replyTo)
{
    char buf[256];
    daycareStatusString(buf, sizeof(buf));
    sendTextDM(replyTo, buf);
}

// ── Auto check-in: load last .sav from SD without starting emulator ────────

void MonsterMeshModule::daycareAutoCheckIn()
{
    char romPath[256] = {};
    char savPath[256];
    size_t len = 0;

    // Step 1: read last ROM path (small file, quick SPI lock)
    // Re-init SD inside spiLock so LGFX can't be mid-DMA while we reconfigure the bus.
    {
        concurrency::LockGuard g(spiLock);
        SD.end();
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
        if (!SD.begin(SDCARD_CS, SPI, 4000000U)) {
            LOG_WARN("[MonsterMesh] auto-daycare: SD re-init failed\n");
            return;
        }
        File lrf = SD.open("/last_rom.txt", FILE_READ);
        if (!lrf) {
            LOG_INFO("[MonsterMesh] no /last_rom.txt — skipping auto-daycare\n");
            return;
        }
        len = lrf.readBytes(romPath, sizeof(romPath) - 1);
        lrf.close();
    }
    if (len == 0) return;
    romPath[len] = '\0';
    while (len > 0 && (romPath[len-1] == '\n' || romPath[len-1] == '\r' || romPath[len-1] == ' '))
        romPath[--len] = '\0';

    LOG_INFO("[MonsterMesh] auto-daycare: last ROM = '%s'\n", romPath);
    MonsterMeshEmulator::romPathToSavePath(romPath, savPath, sizeof(savPath));

    // Step 2: allocate PSRAM buffer (no SPI needed)
    uint8_t *sram = static_cast<uint8_t *>(ps_malloc(0x8000));
    if (!sram) {
        LOG_WARN("[MonsterMesh] auto-daycare: PSRAM alloc failed\n");
        return;
    }
    memset(sram, 0, 0x8000);

    // Step 3: read .sav file (32KB, separate SPI lock + SD re-init)
    size_t n = 0;
    {
        concurrency::LockGuard g(spiLock);
        SD.end();
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
        if (!SD.begin(SDCARD_CS, SPI, 4000000U)) {
            LOG_WARN("[MonsterMesh] auto-daycare: SD re-init failed for .sav\n");
            free(sram);
            return;
        }
        File sf = SD.open(savPath, FILE_READ);
        if (!sf) {
            LOG_INFO("[MonsterMesh] no save file '%s' — skipping auto-daycare\n", savPath);
            free(sram);
            return;
        }
        n = sf.read(sram, 0x8000);
        sf.close();
    }
    LOG_INFO("[MonsterMesh] auto-daycare: loaded %u bytes from %s\n", (unsigned)n, savPath);

    if (n == 0) {
        free(sram);
        return;
    }

    // Read trainer name from SRAM (Gen 1 offset 0x2598)
    char gameName[8] = {};
    for (int i = 0; i < 7; i++) {
        uint8_t c = sram[SAV_PLAYER_NAME + i];
        if (c == 0x50) break;  // Gen 1 string terminator
        gameName[i] = gen1CharToAscii(c);
    }

    const char *shortName = getShortName(nodeDB->getNodeNum());
    LOG_INFO("[MonsterMesh] auto-daycare: trainer='%s' node='%s'\n", gameName, shortName);

    daycare_.checkIn(sram, shortName, gameName);

    // Cache party for terminal before freeing SRAM
    buildTerminalPartyFromSram(sram);
    free(sram);

    if (daycare_.isActive()) {
        const auto &state = daycare_.getState();
        LOG_INFO("[MonsterMesh] auto-daycare: %d pokemon checked in\n", state.partyCount);
        daycare_.forceBeacon();
        daycare_.forceEvent();
        // DM the first event to the user
        const auto &evt = daycare_.getLastEvent();
        if (evt.message[0]) {
            sendTextDM(nodeDB->getNodeNum(), evt.message);
            LOG_INFO("[MonsterMesh] first event: %s\n", evt.message);
        }
        LOG_INFO("[MonsterMesh] auto-daycare: beacon + event sent\n");
        setupStatus_ = "Daycare active";
    }
}

void MonsterMeshModule::buildTerminalPartyFromSram(const uint8_t *sram)
{
    memset(&terminalParty_, 0, sizeof(terminalParty_));
    uint8_t count = sram[SAV_PARTY_COUNT];
    if (count > 6) count = 6;
    terminalParty_.count = count;

    memcpy(terminalParty_.species, &sram[SAV_SPECIES_LIST], 7);
    memcpy(terminalParty_.mons, &sram[SAV_POKEMON_DATA], count * sizeof(Gen1Pokemon));

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *nickRaw = &sram[SAV_NICKNAMES + i * SAV_NAME_SIZE];
        for (int j = 0; j < 10; ++j) {
            if (nickRaw[j] == SAV_STRING_TERMINATOR) {
                terminalParty_.nicknames[i][j] = 0;
                break;
            }
            terminalParty_.nicknames[i][j] = (uint8_t)gen1CharToAscii(nickRaw[j]);
            if (j == 9) terminalParty_.nicknames[i][10] = 0;
        }
    }

    terminalPartyReady_ = true;
    LOG_INFO("[MonsterMesh] terminal party: %u pokemon cached\n", (unsigned)count);
}

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH
