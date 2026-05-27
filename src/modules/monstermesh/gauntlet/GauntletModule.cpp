// SPDX-License-Identifier: MIT
// See GauntletModule.h.

#include "GauntletModule.h"

#if !MESHTASTIC_EXCLUDE_GAUNTLET

#include "GauntletBattle.h"
#include "GauntletGyms.h"
#include "Gen1MinimalStats.h"
#include "GauntletBBS.h"
#include "GauntletMQTT.h"
#include "GauntletProfile.h"
#include "../BattlePacket.h"
#include "../Gen1BattleEngine.h"
#include "../Gen1Species.h"
#include "MeshService.h"
#include "Router.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>

GauntletModule *gauntletModule;

static constexpr uint16_t REPLY_MAX  = 220;   // single-packet text size cap
static constexpr uint32_t LOOP_MS    = 5000;  // session expiry sweep cadence

// ── Construction ─────────────────────────────────────────────────────────────

GauntletModule::GauntletModule()
    : SinglePortModule("Gauntlet", meshtastic_PortNum_TEXT_MESSAGE_APP),
      concurrency::OSThread("Gauntlet")
{
    memset(sessions_, 0, sizeof(sessions_));

    uint32_t myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    const char *defaultName  = "Pallet Gym";
    const char *defaultBadge = "Boulder Badge";

    if (!gauntletLoad(state_, defaultName, defaultBadge, myNode)) {
        gauntletSave(state_);
    }

    // Admin claim window opens for 120 sec ONLY if no admin is set yet —
    // rebooting a device that already has an admin must NOT reopen claim.
    // To re-claim later, an admin uses the on-device "Reset Admin" menu
    // (which calls gymAdminResetAndOpenWindow).
    if (state_.adminNodeNum == 0) {
        adminClaimUntilMs_ = millis() + ADMIN_CLAIM_WINDOW_MS;
    }

    // Auto-fill the gym when there's no admin and no preset configured. The
    // pick is sticky — re-runs only happen on a fresh state (admin reset
    // clears autoEnabled too via existing `admin reset` paths).
    if (state_.adminNodeNum == 0 &&
        state_.gymPresetIdx >= GAUNTLET_GYM_COUNT &&
        !state_.autoEnabled) {
        autoSetup();
    }

    if (inputBroker) inputObserver_.observe(inputBroker);

    gauntletModule = this;
    LOG_INFO("Gauntlet: %s [%s] loaded — leader=%s roster=%u prev=%u\n",
             state_.gymName, state_.gymBadge,
             state_.leader.nodeNum ? state_.leader.name : "(none)",
             (unsigned)state_.rosterSize, (unsigned)state_.prevLeaderCount);
}

// ── Packet filtering: only DMs to us with text payload ───────────────────────

bool GauntletModule::wantPacket(const meshtastic_MeshPacket *p)
{
    if (!p || !nodeDB) return false;

    // PRIVATE_APP DMs to us — networked battle protocol (BattlePacket).
    // Also accept PRIVATE_APP broadcasts so we can answer BBS_PING discovery
    // probes from MM Terminal. PRIVATE_APP is not displayed as chat by
    // standard Meshtastic clients, so this is safe to broadcast.
    if (p->decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        bool dmToUs     = (p->to == nodeDB->getNodeNum() && !isBroadcast(p->to));
        bool broadcast_ = isBroadcast(p->to);
        return dmToUs || broadcast_;
    }

    // Text DMs to us (full command set). We deliberately do NOT accept text
    // broadcasts — BBS discovery used to do that and it polluted mesh chat.
    // Discovery now lives on PRIVATE_APP above.
    if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        return (p->to == nodeDB->getNodeNum() && !isBroadcast(p->to));
    }
    return false;
}

// ── Receive ──────────────────────────────────────────────────────────────────

ProcessMessage GauntletModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.decoded.payload.size == 0) return ProcessMessage::CONTINUE;

    // PRIVATE_APP path: the BattlePacket / TEXT_BATTLE_* protocol used by
    // MonsterMesh Terminal's `bbs fight N` networked-battle screen.
    if (mp.decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        handleBattlePacket(mp);
        return ProcessMessage::CONTINUE;
    }

    // Copy payload into a local buffer; null-terminate. Meshtastic text payload
    // is not guaranteed to be NUL-terminated.
    char text[238];
    uint16_t n = mp.decoded.payload.size;
    if (n >= sizeof(text)) n = sizeof(text) - 1;
    memcpy(text, mp.decoded.payload.bytes, n);
    text[n] = '\0';

    // Trim leading whitespace
    char *t = text;
    while (*t == ' ' || *t == '\t') t++;
    if (!*t) return ProcessMessage::CONTINUE;

    // Prefix matcher — recognises:
    //   "mmg ..."  "gym ..."  "bbs ..."  (each optionally `!`-prefixed)
    // The `!` is optional and the prefix word must be followed by a word
    // boundary (space, tab, end-of-string) so things like "Gyarados" in a
    // party CSV don't accidentally match "gym".
    // `mmg` is the canonical name (MonsterMesh Gym); `gym` and `bbs` are
    // kept as backward-compatible aliases.
    // Returns the number of bytes to skip past the prefix, or 0 for no match.
    auto matchPrefix = [](const char *s) -> int {
        int off = (*s == '!') ? 1 : 0;
        if (strncasecmp(s + off, "mmg", 3) != 0 &&
            strncasecmp(s + off, "gym", 3) != 0 &&
            strncasecmp(s + off, "bbs", 3) != 0) return 0;
        char c = s[off + 3];
        bool boundary = (c == '\0' || c == ' ' || c == '\t' ||
                         c == '\n' || c == '\r');
        return boundary ? (off + 3) : 0;
    };

    // Standalone "help" aliases — work without any prefix when DMed to us.
    auto isHelpAlias = [](const char *s) -> bool {
        return strcasecmp(s, "help") == 0 ||
               strcasecmp(s, "list") == 0 ||
               strcasecmp(s, "ls")   == 0;
    };

    // Standalone command aliases — typing any of these as a bare DM (no
    // `mmg`/`gym`/`bbs` prefix) routes them as if prefixed.
    auto isCommandAlias = [](const char *s) -> int {
        static const char *cmds[] = {
            "admin", "challenge", "leader", "roster", "records",
            "profile", "msg", "ping",
        };
        for (auto c : cmds) {
            size_t n = strlen(c);
            if (strncasecmp(s, c, n) == 0) {
                char b = s[n];
                if (b == '\0' || b == ' ' || b == '\t' || b == '\n' || b == '\r')
                    return 1;
            }
        }
        return 0;
    };

    // (BBS discovery probes used to come in here as text broadcasts; they
    // now arrive on PRIVATE_APP via handleBattlePacket, so we no longer
    // accept any text broadcast here.)

    GauntletSession *sess = getOrCreateSession(getFrom(&mp));
    if (!sess) return ProcessMessage::CONTINUE;
    sess->lastActivity = (uint32_t)getTime();

    int skip = matchPrefix(t);

    // If the player is in the middle of submitting a party, treat any
    // non-prefixed, non-help-alias line as their party CSV.
    // (DM-text gauntlet is gone — battles run locally on the T-Deck via the
    // BBS_FIGHT_REQUEST/PARTY/RESULT protocol. Stale AWAIT_PARTY sessions
    // just fall through to normal command handling now.)

    // Standalone help — DM `help` / `list` / `ls` shows the command list.
    if (!skip && isHelpAlias(t)) {
        handleCommand(mp, *sess, "help");
        return ProcessMessage::CONTINUE;
    }

    // Standalone bare commands (`admin`, `leader`, `roster`, `challenge`,
    // `records`, `profile`, `msg`, `ping`) — route as if prefixed.
    if (!skip && isCommandAlias(t)) {
        // `ping` has its own dispatcher.
        if (strncasecmp(t, "ping", 4) == 0) {
            handleCmdPing(mp);
        } else {
            handleCommand(mp, *sess, t);
        }
        return ProcessMessage::CONTINUE;
    }

    // Otherwise must be prefixed — leave non-gym DMs to the rest of the
    // firmware.
    if (!skip) return ProcessMessage::CONTINUE;

    char *cmd = t + skip;
    while (*cmd == ' ' || *cmd == '\t') cmd++;

    // `ping` works as a DM too — same response as the broadcast probe.
    if (strncasecmp(cmd, "ping", 4) == 0) {
        handleCmdPing(mp);
        return ProcessMessage::CONTINUE;
    }

    handleCommand(mp, *sess, cmd);
    return ProcessMessage::CONTINUE;
}

// ── Periodic sweep ───────────────────────────────────────────────────────────

int32_t GauntletModule::runOnce()
{
    expireSessions();
    // Time out an in-flight networked battle if the peer goes silent (120s).
    if (netBattle_ && netLastRxMs_ != 0 &&
        (millis() - netLastRxMs_) > 60000) {
        LOG_INFO("Gauntlet: net battle timed out — tearing down\n");
        teardownNetBattle();
    }

    // BBS discovery model: PROBE-AND-REPLY (user-driven), no periodic beacon.
    // The MM Terminal's `bbs` command broadcasts a silent BBS_PING on MM
    // channel 1 / PRIVATE_APP; we answer in handleBattlePacket. That uses
    // less airtime than continuous beaconing.
    //
    // If you ever re-add a periodic beacon, it MUST be PRIVATE_APP / MM
    // channel only, NEVER TEXT_MESSAGE_APP, NEVER channel 0. See feedback
    // memory `feedback_no_public_beacons`.
    (void)lastBeaconMs_;

    return LOOP_MS;
}

// ── Session bookkeeping ──────────────────────────────────────────────────────

GauntletSession *GauntletModule::getOrCreateSession(uint32_t nodeNum)
{
    if (!nodeNum) return nullptr;

    for (uint8_t i = 0; i < GAUNTLET_SESSION_MAX; i++)
        if (sessions_[i].nodeNum == nodeNum) return &sessions_[i];

    for (uint8_t i = 0; i < GAUNTLET_SESSION_MAX; i++) {
        if (sessions_[i].nodeNum == 0) {
            memset(&sessions_[i], 0, sizeof(GauntletSession));
            sessions_[i].nodeNum = nodeNum;
            return &sessions_[i];
        }
    }

    // Evict oldest idle session
    uint8_t  oldestSlot = 0;
    uint32_t oldestTs   = 0xFFFFFFFFu;
    bool     foundIdle  = false;
    for (uint8_t i = 0; i < GAUNTLET_SESSION_MAX; i++) {
        if (sessions_[i].state == GS_IDLE && sessions_[i].lastActivity < oldestTs) {
            oldestTs   = sessions_[i].lastActivity;
            oldestSlot = i;
            foundIdle  = true;
        }
    }
    if (!foundIdle) return nullptr; // all sessions actively battling
    memset(&sessions_[oldestSlot], 0, sizeof(GauntletSession));
    sessions_[oldestSlot].nodeNum = nodeNum;
    return &sessions_[oldestSlot];
}

void GauntletModule::expireSessions()
{
    uint32_t now = (uint32_t)getTime();
    if (now == 0) return;
    for (uint8_t i = 0; i < GAUNTLET_SESSION_MAX; i++) {
        if (sessions_[i].nodeNum == 0) continue;
        if (now - sessions_[i].lastActivity > GAUNTLET_SESSION_TTL) {
            memset(&sessions_[i], 0, sizeof(GauntletSession));
        }
    }
}

// ── Command handlers ─────────────────────────────────────────────────────────

// Discovery probe DM reply (rare path — most probes come in as PRIVATE_APP
// BBS_PING and are answered by sendBbsReply below). This text-DM variant is
// kept so a user can manually check the gym via `gym ping` from the chat
// app; the reply is unicast to that one user only.
void GauntletModule::handleCmdPing(const meshtastic_MeshPacket &mp)
{
    char buf[120];
    const char *leader = state_.leader.nodeNum ? state_.leader.name : "open";
    snprintf(buf, sizeof(buf), "GYM:%s|%s|%s|%u",
             state_.gymName, state_.gymBadge, leader, (unsigned)state_.rosterSize);
    sendReply(mp, buf);
}

void GauntletModule::handleCommand(const meshtastic_MeshPacket &mp,
                                    GauntletSession &sess, const char *cmd)
{
    char buf[REPLY_MAX];

    if (!*cmd) {
        snprintf(buf, sizeof(buf),
                 "%s [%s]\nLeader: %s\nRoster: %u  Prev: %u  Battles: %lu\n"
                 "Send 'gym challenge' to fight.",
                 state_.gymName, state_.gymBadge,
                 state_.leader.nodeNum ? state_.leader.name : "(open)",
                 (unsigned)state_.rosterSize, (unsigned)state_.prevLeaderCount,
                 (unsigned long)state_.totalBattles);
        sendReply(mp, buf);
        return;
    }

    if (strncasecmp(cmd, "challenge", 9) == 0) {
        sendReply(mp,
                  "Battles run on MonsterMesh Terminal.\n"
                  "On a T-Deck: type `bbs`, then `bbs fight N` to fight this gym.");
        return;
    }

    if (strncasecmp(cmd, "roster", 6) == 0) {
        int off = snprintf(buf, sizeof(buf), "-- %s ladder --\n", state_.gymName);
        if (state_.leader.nodeNum) {
            char p[40];
            gauntletFormatParty(state_.leader.party, p, sizeof(p));
            off += snprintf(buf + off, sizeof(buf) - off,
                             "LEADER %s: %s\n", state_.leader.name, p);
        } else {
            off += snprintf(buf + off, sizeof(buf) - off, "LEADER: (open)\n");
        }
        // Show top of roster (slot 0 is hardest, last slot is easiest)
        uint8_t shown = 0;
        for (uint8_t i = 0; i < state_.rosterSize && shown < 5 && off < (int)sizeof(buf) - 16; i++, shown++) {
            off += snprintf(buf + off, sizeof(buf) - off,
                             "#%u %s\n", (unsigned)(i + 1), state_.roster[i].name);
        }
        if (state_.rosterSize > 5)
            snprintf(buf + off, sizeof(buf) - off, "...+%u more", state_.rosterSize - 5);
        sendReply(mp, buf);
        return;
    }

    if (strncasecmp(cmd, "leader", 6) == 0) {
        if (!state_.leader.nodeNum) { sendReply(mp, "No leader yet — claim the title!"); return; }
        char p[64];
        gauntletFormatParty(state_.leader.party, p, sizeof(p));
        snprintf(buf, sizeof(buf), "Leader: %s\nParty: %s",
                 state_.leader.name, p);
        sendReply(mp, buf);
        return;
    }

    if (strncasecmp(cmd, "records", 7) == 0) {
        if (state_.prevLeaderCount == 0) { sendReply(mp, "No previous leaders on record."); return; }
        int off = snprintf(buf, sizeof(buf), "-- Hall of Champions --\n");
        for (uint8_t i = 0; i < state_.prevLeaderCount && off < (int)sizeof(buf) - 24; i++) {
            char p[32];
            gauntletFormatParty(state_.prevLeaders[i].party, p, sizeof(p));
            off += snprintf(buf + off, sizeof(buf) - off, "%s: %s\n",
                             state_.prevLeaders[i].name, p);
        }
        sendReply(mp, buf);
        return;
    }

    if (strncasecmp(cmd, "msg ", 4) == 0 || strncasecmp(cmd, "msg\t", 4) == 0) {
        const char *body = cmd + 4;
        while (*body == ' ' || *body == '\t') body++;
        if (!*body) { sendReply(mp, "Usage: gym msg <your text>"); return; }
        gauntletBBSLogMessage(getFrom(&mp), senderShortName(getFrom(&mp)), body);
        sendReply(mp, "Message posted to gym board.");
        return;
    }

    if (strncasecmp(cmd, "profile", 7) == 0) {
        GauntletProfile p;
        uint32_t who = getFrom(&mp);
        if (!gauntletProfileLoad(who, p)) {
            sendReply(mp, "No profile yet — challenge the gym!");
            return;
        }
        char rankStr[24];
        if (p.bestRank == 0)        snprintf(rankStr, sizeof(rankStr), "n/a");
        else if (p.bestRank == 1 && p.gymTitles > 0) snprintf(rankStr, sizeof(rankStr), "LEADER");
        else                         snprintf(rankStr, sizeof(rankStr), "#%u", p.bestRank);
        snprintf(buf, sizeof(buf),
                 "%s @ %s\nChall: %lu  W: %lu  L: %lu\nBest rank: %s  Titles: %u",
                 p.name[0] ? p.name : senderShortName(who), state_.gymName,
                 (unsigned long)p.totalChallenges,
                 (unsigned long)p.totalWins,
                 (unsigned long)p.totalLosses,
                 rankStr, (unsigned)p.gymTitles);
        sendReply(mp, buf);
        return;
    }

    // ── gym admin <set | reset | help | list> ────────────────────────────
    // Subcommands:
    //   help / list  — show help + gym index (no ACL)
    //   set          — wizard: pick gym → member → boss level
    //   reset        — drop admin AND wipe preset (admin only).
    //                  After reset, the device must be REBOOTED to claim
    //                  admin again — physical access required.
    //
    // ACL: the first DM `admin set/reset` within ADMIN_CLAIM_WINDOW_MS of
    // boot claims the sender as admin. After that, only that sender can run
    // these commands. The on-device admin slide bypasses ACL (physical
    // presence implies trust).
    if (strncasecmp(cmd, "admin", 5) == 0) {
        const char *sub = cmd + 5;
        while (*sub == ' ' || *sub == '\t') sub++;

        // help / list / no subcommand — no ACL. `admin set` / `admin gym`
        // enter wizards (handled below); only "help", "list", or an
        // unrecognized sub lands here.
        bool isHelp  = (strncasecmp(sub, "help",  4) == 0);
        bool isList  = (strncasecmp(sub, "list",  4) == 0);
        bool isReset = (strncasecmp(sub, "reset", 5) == 0);
        bool isSet   = (strncasecmp(sub, "set",   3) == 0);
        bool isGym   = (strncasecmp(sub, "gym",   3) == 0) &&
                       (sub[3] == '\0' || sub[3] == ' ' || sub[3] == '\t');
        if (!isReset && !isSet && !isGym && (isHelp || isList || !*sub)) {
            // Resolve a header gym name (state's stored name or device long
            // name fallback) so the admin sees which gym they're managing.
            const char *gymName = state_.gymName[0] ? state_.gymName : nullptr;
            if (!gymName && nodeDB) {
                const meshtastic_NodeInfoLite *me =
                    nodeDB->getMeshNode(nodeDB->getNodeNum());
                if (me && me->has_user && me->user.long_name[0])
                    gymName = me->user.long_name;
            }
            if (!gymName) gymName = "MonsterMesh Gym";

            int off = snprintf(buf, sizeof(buf),
                "== %s — Admin ==\n"
                "  set [N M L]    one member\n"
                "  gym [N L]      whole gym\n"
                "  name <text>    rename gym\n"
                "  badge <text>   set badge\n"
                "  reset          drop admin\n"
                "  help/list      this menu\n"
                "Gyms:\n", gymName);
            for (uint8_t i = 0; i < GAUNTLET_GYM_COUNT && off < (int)sizeof(buf) - 24; ++i) {
                const GymPreset *g = gauntletGymPreset(i);
                if (!g) continue;
                off += snprintf(buf + off, sizeof(buf) - off,
                                 "%u.%s ", (unsigned)(i + 1), g->name);
            }
            // Status footer.
            uint32_t now = millis();
            const char *status;
            char  statusBuf[40];
            if (state_.adminNodeNum != 0) {
                snprintf(statusBuf, sizeof(statusBuf),
                         "\nAdmin: 0x%08x", (unsigned)state_.adminNodeNum);
                status = statusBuf;
            } else if (adminClaimUntilMs_ != 0 && now < adminClaimUntilMs_) {
                uint32_t s = (adminClaimUntilMs_ - now) / 1000;
                snprintf(statusBuf, sizeof(statusBuf),
                         "\nClaim window: %us left", (unsigned)s);
                status = statusBuf;
            } else {
                status = "\nAdmin: none — reboot to claim";
            }
            if (off < (int)sizeof(buf) - 1)
                snprintf(buf + off, sizeof(buf) - off, "%s", status);
            sendReply(mp, buf);
            return;
        }

        // Detect "admin name <text>" — set the gym display name.
        bool isName = (strncasecmp(sub, "name", 4) == 0) &&
                      (sub[4] == ' ' || sub[4] == '\t');
        // Detect "admin badge <text>" — set the gym's badge label.
        bool isBadge = (strncasecmp(sub, "badge", 5) == 0) &&
                       (sub[5] == ' ' || sub[5] == '\t');
        if (!isReset && !isSet && !isGym && !isName && !isBadge) {
            sendReply(mp, "Usage: admin <set | gym | reset | name | badge | help>");
            return;
        }
        // ACL split: `admin set` is the ONLY command that can claim during
        // the open window. Every other admin command requires an already-
        // established admin (no implicit claim).
        bool wasAdminBefore = (state_.adminNodeNum == mp.from);
        bool justClaimed    = false;
        if (isSet) {
            if (!isAdminAuthorized(mp.from)) {
                sendReply(mp,
                    "Admin not authorized. Use the on-device\n"
                    "Reset Admin menu, then send `admin set`\n"
                    "within 120s to claim.");
                return;
            }
            justClaimed = !wasAdminBefore && state_.adminNodeNum == mp.from;
        } else {
            if (!wasAdminBefore) {
                sendReply(mp,
                    "Only the admin can run that.\n"
                    "Send `admin set` first to claim admin.");
                return;
            }
        }
        // First successful claim: send a short, single-packet confirmation
        // and return. Bundling the wizard reply with the claim message
        // overflows the ~237-byte LoRa text-DM packet limit and the whole
        // reply gets silently dropped. The user just sends the same command
        // again to enter the wizard now that they're admin.
        if (justClaimed) {
            char claimMsg[160];
            snprintf(claimMsg, sizeof(claimMsg),
                "** Admin claimed by 0x%08x **\n"
                "Send `admin set` again for the wizard,\n"
                "or `admin help` for commands.",
                (unsigned)mp.from);
            sendReply(mp, claimMsg);
            return;
        }
        // Already admin — replies pass straight through.
        auto adminReply = [&](const char *body) { sendReply(mp, body); };

        if (isReset) {
            state_.gymPresetIdx = 0xFF;
            memset(state_.memberLevels, 0, sizeof(state_.memberLevels));
            memset(&state_.leader, 0, sizeof(state_.leader));
            state_.adminNodeNum = 0;
            state_.autoEnabled  = 0;
            strncpy(state_.gymName,  "Pallet Gym",     GAUNTLET_NAME_MAX  - 1);
            strncpy(state_.gymBadge, "Boulder Badge",  GAUNTLET_BADGE_MAX - 1);
            state_.lastUpdate = (uint32_t)getTime();
            gauntletSave(state_);
            // Re-randomise the auto-fill so the gym is still functional while
            // waiting for a new admin to claim.
            autoSetup();
            // Reset opens a fresh 120 s claim window so a new admin can claim
            // immediately (matches the on-device "Reset Admin" menu).
            adminClaimUntilMs_ = millis() + ADMIN_CLAIM_WINDOW_MS;
            adminReply(
                "Admin dropped, preset cleared.\n"
                "120s claim window OPEN — first node to send\n"
                "`admin set` becomes the new admin.");
            return;
        }

        // ── `admin name <text>` — rename the gym ─────────────────────────
        if (isName) {
            const char *p = sub + 4;
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) {
                adminReply("Usage: admin name <text>");
                return;
            }
            strncpy(state_.gymName, p, GAUNTLET_NAME_MAX - 1);
            state_.gymName[GAUNTLET_NAME_MAX - 1] = '\0';
            state_.lastUpdate = (uint32_t)getTime();
            gauntletSave(state_);
            char r[80];
            snprintf(r, sizeof(r), "Gym renamed to: %s", state_.gymName);
            adminReply(r);
            return;
        }

        // ── `admin badge <text>` — set the badge label clients display ───
        // The badge string is shipped in BBS_REPLY (PRIVATE_APP discovery) so
        // MM Terminal can show it next to the gym name in its `mmg` listing.
        if (isBadge) {
            const char *p = sub + 5;
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) {
                adminReply("Usage: admin badge <text>");
                return;
            }
            strncpy(state_.gymBadge, p, GAUNTLET_BADGE_MAX - 1);
            state_.gymBadge[GAUNTLET_BADGE_MAX - 1] = '\0';
            state_.lastUpdate = (uint32_t)getTime();
            gauntletSave(state_);
            char r[80];
            snprintf(r, sizeof(r), "Badge set to: %s", state_.gymBadge);
            adminReply(r);
            return;
        }

        // ── `admin gym ...` — whole-gym configuration ────────────────────
        // Sets ALL 5 members of a gym at once. Scaling is leader-relative:
        // the LEADER gets boss level L, and each grunt scales DOWN by the
        // canonical delta between their canonical boss and the canonical
        // leader's boss (preserves Red/Blue's "grunts weaker than leader"
        // feel). Use `admin set N M L` for a single member.
        //
        //   admin gym               → list of gyms
        //   admin gym N             → load gym N at canonical levels
        //   admin gym N L           → load gym N, leader=L, grunts scaled
        //   admin gym N stock       → same as `admin gym N`
        if (isGym) {
            const char *p = sub + 3;
            while (*p == ' ' || *p == '\t') p++;

            if (!*p) {
                int off = snprintf(buf, sizeof(buf),
                    "Pick a gym (reply: admin gym N [L]):\n");
                for (uint8_t i = 0; i < GAUNTLET_GYM_COUNT && off < (int)sizeof(buf) - 24; ++i) {
                    const GymPreset *g2 = gauntletGymPreset(i);
                    if (!g2) continue;
                    off += snprintf(buf + off, sizeof(buf) - off,
                                     "%u. %s (%s)\n",
                                     (unsigned)(i + 1), g2->name, g2->leaderName);
                }
                adminReply( buf);
                return;
            }

            int gn = atoi(p);
            if (gn < 1 || gn > GAUNTLET_GYM_COUNT) {
                adminReply( "Pick gym 1..8. Try `admin gym` for the list.");
                return;
            }
            uint8_t gymIdx = (uint8_t)(gn - 1);

            // Skip digits + whitespace to look for optional level.
            while (*p && *p != ' ' && *p != '\t') p++;
            while (*p == ' ' || *p == '\t') p++;

            uint8_t level = 0;   // 0 = stock
            if (*p) {
                if (strncasecmp(p, "stock", 5) == 0) {
                    level = 0;
                } else {
                    int lv = atoi(p);
                    if (lv < 1 || lv > 100) {
                        adminReply( "Level must be 1..100 or 'stock'.");
                        return;
                    }
                    level = (uint8_t)lv;
                }
            }

            // Apply to all 5 members. gymAdminApply switches preset on the
            // first call; subsequent calls just update memberLevels.
            //
            // Leader-relative scaling: when level > 0, leader gets boss=L,
            // grunts scale DOWN by canonical (canonGruntBoss-canonLeaderBoss)
            // delta. So if Brock canon=L14 and Hiker canon=L10 (Δ=−4), and
            // user sets L=60, Brock=60 and Hiker=56.
            const GymPreset *gp = gauntletGymPreset(gymIdx);
            uint8_t canonLeaderBoss = 0;
            if (gp) {
                const GymPresetTrainer &lt = gp->trainers[GAUNTLET_GYM_LEADER_IDX];
                for (uint8_t k = 0; k < lt.monCount; ++k)
                    if (lt.party[k].level > canonLeaderBoss)
                        canonLeaderBoss = lt.party[k].level;
            }
            if (canonLeaderBoss == 0) canonLeaderBoss = 1;

            for (uint8_t m = 0; m < 5; ++m) {
                uint8_t applyLvl = 0;
                if (level > 0 && gp) {
                    const GymPresetTrainer &t = gp->trainers[m];
                    uint8_t canonBoss = 0;
                    for (uint8_t k = 0; k < t.monCount; ++k)
                        if (t.party[k].level > canonBoss)
                            canonBoss = t.party[k].level;
                    int delta = (int)canonBoss - (int)canonLeaderBoss;   // ≤ 0
                    int scaled = (int)level + delta;
                    if (scaled < 1)   scaled = 1;
                    if (scaled > 100) scaled = 100;
                    applyLvl = (uint8_t)scaled;
                }
                gymAdminApply(gymIdx, m, applyLvl);
            }

            const GymPreset *g3 = gauntletGymPreset(gymIdx);
            int off = snprintf(buf, sizeof(buf),
                "%s Gym -- %s\n",
                g3 ? g3->name : "?",
                level ? "boss L" : "stock levels");
            if (level && off < (int)sizeof(buf) - 8) {
                off += snprintf(buf + off, sizeof(buf) - off,
                                "all members boss=L%u\n", (unsigned)level);
            }
            for (uint8_t i = 0; i < 5 && off < (int)sizeof(buf) - 24 && g3; ++i) {
                const GymPresetTrainer &t = g3->trainers[i];
                uint8_t cb = 0;
                for (uint8_t k = 0; k < t.monCount; ++k)
                    if (t.party[k].level > cb) cb = t.party[k].level;
                uint8_t over = state_.memberLevels[i];
                uint8_t eff  = over ? over : cb;
                off += snprintf(buf + off, sizeof(buf) - off,
                                 "%u.%-7s L%u%s\n",
                                 (unsigned)(i + 1), t.name, (unsigned)eff,
                                 over ? "*" : "");
            }
            adminReply( buf);
            return;
        }

        // isSet — fall through to the wizard.
        const char *args = sub + 3;
        while (*args == ' ' || *args == '\t') args++;

        // Step 1 — no further args: list gyms.
        if (!*args) {
            int off = snprintf(buf, sizeof(buf), "Pick a gym (reply: admin set N):\n");
            for (uint8_t i = 0; i < GAUNTLET_GYM_COUNT && off < (int)sizeof(buf) - 24; ++i) {
                const GymPreset *g = gauntletGymPreset(i);
                if (!g) continue;
                off += snprintf(buf + off, sizeof(buf) - off,
                                 "%u. %s (%s)\n",
                                 (unsigned)(i + 1), g->name, g->leaderName);
            }
            adminReply( buf);
            return;
        }

        // Step 2 — gym index parsed.
        int n = atoi(args);
        if (n < 1 || n > GAUNTLET_GYM_COUNT) {
            adminReply( "Pick 1..8. Try `admin set` to see the list.");
            return;
        }
        uint8_t gymIdx = (uint8_t)(n - 1);
        const GymPreset *g = gauntletGymPreset(gymIdx);
        if (!g) { adminReply( "Invalid gym."); return; }

        // Skip the digits of N + whitespace.
        while (*args && *args != ' ' && *args != '\t') args++;
        while (*args == ' ' || *args == '\t') args++;

        if (!*args) {
            // No member yet — list 5 members with their boss pokemon.
            int off = snprintf(buf, sizeof(buf),
                                "%s Gym — pick a member\n(reply: admin set %u M):\n",
                                g->name, (unsigned)(gymIdx + 1));
            for (uint8_t m = 0; m < 5 && off < (int)sizeof(buf) - 32; ++m) {
                const GymPresetTrainer &tr = g->trainers[m];
                uint8_t bossLvl = 0;
                const char *bossName = "?";
                for (uint8_t i = 0; i < tr.monCount; ++i) {
                    if (tr.party[i].level > bossLvl) {
                        bossLvl  = tr.party[i].level;
                        bossName = gen1SpeciesName(gen1DexToInternal(tr.party[i].dex));
                    }
                }
                uint8_t cur = state_.memberLevels[m];
                if (cur > 0) {
                    off += snprintf(buf + off, sizeof(buf) - off,
                                     "%u. %s (%s L%u*)\n",
                                     (unsigned)(m + 1),
                                     tr.name, bossName, (unsigned)cur);
                } else {
                    off += snprintf(buf + off, sizeof(buf) - off,
                                     "%u. %s (%s L%u)\n",
                                     (unsigned)(m + 1),
                                     tr.name, bossName, (unsigned)bossLvl);
                }
            }
            adminReply( buf);
            return;
        }

        // Step 3 — member index parsed.
        int mNum = atoi(args);
        if (mNum < 1 || mNum > 5) {
            adminReply( "Pick member 1..5. Try `admin set N` to see them.");
            return;
        }
        uint8_t memberIdx = (uint8_t)(mNum - 1);
        const GymPresetTrainer &tr = g->trainers[memberIdx];

        // Find this member's canonical boss for the prompt.
        uint8_t canonBoss = 0;
        const char *canonBossName = "?";
        for (uint8_t i = 0; i < tr.monCount; ++i) {
            if (tr.party[i].level > canonBoss) {
                canonBoss     = tr.party[i].level;
                canonBossName = gen1SpeciesName(gen1DexToInternal(tr.party[i].dex));
            }
        }

        while (*args && *args != ' ' && *args != '\t') args++;
        while (*args == ' ' || *args == '\t') args++;

        if (!*args) {
            // Prompt for level.
            snprintf(buf, sizeof(buf),
                     "%s — %s\nBoss: %s (canon L%u)\nReply: admin set %u %u L\n  L = 1..100, or 'stock'",
                     g->name, tr.name, canonBossName, (unsigned)canonBoss,
                     (unsigned)(gymIdx + 1), (unsigned)mNum);
            adminReply( buf);
            return;
        }

        // Step 4 — apply level.
        uint8_t newLvl;
        if (strncasecmp(args, "stock", 5) == 0) {
            newLvl = 0;
        } else {
            int lv = atoi(args);
            if (lv < 1 || lv > 100) {
                adminReply( "Level must be 1..100 or 'stock'.");
                return;
            }
            newLvl = (uint8_t)lv;
        }

        // Persist — adopting this preset if not already, and storing the
        // chosen level for this member.
        if (state_.gymPresetIdx != gymIdx) {
            state_.gymPresetIdx = gymIdx;
            // Switching gyms wipes any per-member overrides from the previous
            // gym (their canonical levels won't make sense).
            memset(state_.memberLevels, 0, sizeof(state_.memberLevels));
            char nm[GAUNTLET_NAME_MAX];
            snprintf(nm, sizeof(nm), "%s Gym", g->name);
            strncpy(state_.gymName, nm, GAUNTLET_NAME_MAX - 1);
            char bd[GAUNTLET_BADGE_MAX];
            snprintf(bd, sizeof(bd), "%s Badge", g->badgeName);
            strncpy(state_.gymBadge, bd, GAUNTLET_BADGE_MAX - 1);
        }
        state_.memberLevels[memberIdx] = newLvl;

        // If the leader (idx 4) was just configured, install it as the current
        // NPC leader so BBS_FIGHT_REQUEST has a party to ship.
        if (memberIdx == GAUNTLET_GYM_LEADER_IDX) {
            memset(&state_.leader, 0, sizeof(state_.leader));
            state_.leader.nodeNum   = 0xFFFFFFFEu;
            state_.leader.timestamp = (uint32_t)getTime();
            strncpy(state_.leader.name, g->leaderName, GAUNTLET_NAME_MAX - 1);
            gauntletBuildPresetTrainerParty(gymIdx, memberIdx,
                                              state_.leader.party, newLvl);
        }

        state_.lastUpdate = (uint32_t)getTime();
        gauntletSave(state_);

        // Build a summary listing all 5 members + their effective boss
        // levels (canonical or override) so the operator sees the full gym
        // state after each change.
        int off = snprintf(buf, sizeof(buf), "%s Gym — saved member %u:\n",
                            g->name, (unsigned)mNum);
        for (uint8_t i = 0; i < 5 && off < (int)sizeof(buf) - 32; ++i) {
            const GymPresetTrainer &t = g->trainers[i];
            uint8_t cb = 0;
            for (uint8_t k = 0; k < t.monCount; ++k)
                if (t.party[k].level > cb) cb = t.party[k].level;
            uint8_t over = state_.memberLevels[i];
            uint8_t eff  = over ? over : cb;
            off += snprintf(buf + off, sizeof(buf) - off,
                             "%u. %s L%u%s\n",
                             (unsigned)(i + 1), t.name, (unsigned)eff,
                             over ? "*" : "");
        }
        adminReply( buf);
        return;
    }

    // Bare `mmg`/`gym`/`bbs` (no subcommand) and `help` both land here:
    // a user-facing menu, headed by the gym's display name so the player
    // sees which gym they're talking to. Admin commands are surfaced as a
    // separate `admin help` to avoid cluttering the player view.
    if (!cmd[0] || strncasecmp(cmd, "help", 4) == 0) {
        // Resolve gym name: state's stored name, or the device long name as
        // fallback (matches the on-device gym frame header).
        const char *gymName = state_.gymName[0] ? state_.gymName : nullptr;
        if (!gymName && nodeDB) {
            const meshtastic_NodeInfoLite *me =
                nodeDB->getMeshNode(nodeDB->getNodeNum());
            if (me && me->has_user && me->user.long_name[0])
                gymName = me->user.long_name;
        }
        if (!gymName) gymName = "MonsterMesh Gym";

        char hbuf[REPLY_MAX];
        snprintf(hbuf, sizeof(hbuf),
                 "== %s ==\n"
                 "challenge    start fight\n"
                 "leader       current leader\n"
                 "roster       ladder\n"
                 "records      past leaders\n"
                 "profile      your stats\n"
                 "msg <text>   post to board\n"
                 "help         this menu\n"
                 "admin help   admin commands",
                 gymName);
        sendReply(mp, hbuf);
        return;
    }

    sendReply(mp, "Unknown command. Try 'help'.");
}

// (DM-text gauntlet — handleParty + runGauntlet — was removed when battles
// moved to the T-Deck. The gym no longer simulates fights; it only ships
// parties via TEXT_BATTLE_PARTY chunks and accepts BBS_FIGHT_RESULT to
// update its leader/roster state.)

// ── State updates ────────────────────────────────────────────────────────────

void GauntletModule::insertAtRank(const meshtastic_MeshPacket &mp,
                                    GauntletSession &sess, uint8_t rank)
{
    GauntletTrainer t;
    memset(&t, 0, sizeof(t));
    t.nodeNum   = getFrom(&mp);
    t.timestamp = (uint32_t)getTime();
    t.party     = sess.party;
    strncpy(t.name, senderShortName(t.nodeNum), GAUNTLET_NAME_MAX - 1);

    if (rank > state_.rosterSize) rank = state_.rosterSize;

    if (state_.rosterSize < GAUNTLET_ROSTER_MAX) {
        // Shift slots [rank .. rosterSize-1] down by one, then insert
        for (int i = (int)state_.rosterSize; i > (int)rank; i--)
            state_.roster[i] = state_.roster[i - 1];
        state_.rosterSize++;
    } else {
        // Drop the easiest (last) slot, then shift
        for (int i = GAUNTLET_ROSTER_MAX - 1; i > (int)rank; i--)
            state_.roster[i] = state_.roster[i - 1];
    }
    state_.roster[rank] = t;
    state_.lastUpdate   = (uint32_t)getTime();

    char buf[REPLY_MAX];
    snprintf(buf, sizeof(buf),
             "You take Rank #%u! Everyone below shifts down. Defend it!",
             (unsigned)(rank + 1));
    sendReply(mp, buf);

    // Persist gym + per-player profile, broadcast over MQTT.
    char partyCsv[64];
    gauntletFormatParty(sess.party, partyCsv, sizeof(partyCsv));
    gauntletProfileUpdate(t.nodeNum, t.name, partyCsv,
                          /*challengeStarted=*/false,
                          /*ranked=*/true, (uint8_t)(rank + 1),
                          /*becameLeader=*/false,
                          /*reachedLeader=*/(rank == 0));

    sess.state = GS_IDLE;
    gauntletSave(state_);
    gauntletMQTTPublishState(state_);

    char eventPayload[160];
    snprintf(eventPayload, sizeof(eventPayload),
             "{\"node\":%lu,\"name\":\"%s\",\"rank\":%u,\"party\":\"%s\"}",
             (unsigned long)t.nodeNum, t.name, (unsigned)(rank + 1), partyCsv);
    gauntletMQTTPublishEvent(state_, "roster", eventPayload);
    gauntletBBSLogRoster(state_, t.nodeNum, t.name, (uint8_t)(rank + 1));
}

void GauntletModule::promoteToLeader(const meshtastic_MeshPacket &mp,
                                       GauntletSession &sess)
{
    const char *name = senderShortName(getFrom(&mp));

    // Archive old leader as previous-leader entry (if any)
    if (state_.leader.nodeNum) {
        if (state_.prevLeaderCount < GAUNTLET_PREV_MAX) {
            for (int i = (int)state_.prevLeaderCount; i > 0; i--)
                state_.prevLeaders[i] = state_.prevLeaders[i - 1];
            state_.prevLeaders[0] = state_.leader;
            state_.prevLeaderCount++;
        } else {
            for (int i = GAUNTLET_PREV_MAX - 1; i > 0; i--)
                state_.prevLeaders[i] = state_.prevLeaders[i - 1];
            state_.prevLeaders[0] = state_.leader;
        }
    }

    // Install new leader, clear ranked roster (everyone was just defeated)
    memset(&state_.leader, 0, sizeof(state_.leader));
    state_.leader.nodeNum   = getFrom(&mp);
    state_.leader.party     = sess.party;
    state_.leader.timestamp = (uint32_t)getTime();
    strncpy(state_.leader.name, name, GAUNTLET_NAME_MAX - 1);
    state_.rosterSize = 0;
    state_.lastUpdate = (uint32_t)getTime();

    char buf[REPLY_MAX];
    char p[64];
    gauntletFormatParty(sess.party, p, sizeof(p));
    snprintf(buf, sizeof(buf),
             "CHAMPION! %s is the new %s leader.\n"
             "%s Badge earned. Party: %s",
             name, state_.gymName, state_.gymBadge, p);
    sendReply(mp, buf);

    // Profile + MQTT
    gauntletProfileUpdate(state_.leader.nodeNum, name, p,
                          /*challengeStarted=*/false,
                          /*ranked=*/false, /*rank=*/0,
                          /*becameLeader=*/true,
                          /*reachedLeader=*/true);

    sess.state = GS_IDLE;
    gauntletSave(state_);
    gauntletMQTTPublishState(state_);

    char eventPayload[160];
    snprintf(eventPayload, sizeof(eventPayload),
             "{\"node\":%lu,\"name\":\"%s\",\"party\":\"%s\"}",
             (unsigned long)state_.leader.nodeNum, name, p);
    gauntletMQTTPublishEvent(state_, "leader_change", eventPayload);
    gauntletBBSLogLeader(state_, state_.leader.nodeNum, name, p);
}

// ── Reply / lookup helpers ───────────────────────────────────────────────────

void GauntletModule::sendReply(const meshtastic_MeshPacket &req, const char *text)
{
    if (!text || !*text) return;
    if (!service) return;

    meshtastic_MeshPacket *pkt = allocDataPacket();
    if (!pkt) return;
    pkt->to            = getFrom(&req);
    pkt->channel       = req.channel;
    pkt->want_ack      = false;
    pkt->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    uint16_t len = (uint16_t)strlen(text);
    if (len > sizeof(pkt->decoded.payload.bytes))
        len = sizeof(pkt->decoded.payload.bytes);
    memcpy(pkt->decoded.payload.bytes, text, len);
    pkt->decoded.payload.size = len;

    service->sendToMesh(pkt, RX_SRC_LOCAL, true);
}

const char *GauntletModule::senderShortName(uint32_t nodeNum)
{
    static char fallback[12];
    if (nodeDB) {
        meshtastic_NodeInfoLite *ni = nodeDB->getMeshNode(nodeNum);
        if (ni && ni->has_user && ni->user.short_name[0])
            return ni->user.short_name;
    }
    snprintf(fallback, sizeof(fallback), "!%08lx", (unsigned long)nodeNum);
    return fallback;
}

// ── Networked Gen 1 battle (Phase C-2) ────────────────────────────────────────
// Speaks the BattlePacket TEXT_BATTLE_* protocol via PRIVATE_APP so the MM
// Terminal `bbs fight N` command lands in its existing battle screen. Gym
// drives side 0 of the engine via cpuPickAction; challenger drives side 1
// from the terminal UI.
//
// Limitation (matches existing peer-vs-peer): the engine runs with both
// parties mirrored from the side-0 party (no cross-wire party exchange yet).
// Mechanically the loop still works — same-stat fight where each side picks
// moves; deterministic outcome via shared rngSeed.

class GauntletModule::NetBattle {
  public:
    Gen1BattleEngine engine;
    Gen1Party        gymParty;
};

bool GauntletModule::isAdminAuthorized(uint32_t sender)
{
    if (sender == 0) return false;
    if (state_.adminNodeNum == sender) return true;
    if (state_.adminNodeNum == 0 &&
        adminClaimUntilMs_ != 0 &&
        millis() < adminClaimUntilMs_) {
        // Window open + no admin set → first comer claims.
        state_.adminNodeNum = sender;
        adminClaimUntilMs_  = 0;        // close window after a successful claim
        state_.lastUpdate   = (uint32_t)getTime();
        gauntletSave(state_);
        LOG_INFO("Gauntlet: admin claimed by 0x%08x\n", (unsigned)sender);
        return true;
    }
    return false;
}

// ── Elite Four + Champion pool (used for auto-fill leader picks) ─────────────
// Boss = highest-level mon in the trainer's canonical party. Used both as a
// random-leader source and as the level reference when the auto-leader gets
// scaled down to slot-4's boss level.
struct ElitePoolEntry {
    const char *name;
    const uint8_t (*party)[2];   // pairs of (dex, level), 0/0 terminator
    uint8_t bossLevel;
    const char *bossName;
};

static const uint8_t e4_lorelei [][2] = { {87,52},{91,53},{80,54},{124,56},{91,54},{131,56},{0,0} };
static const uint8_t e4_bruno   [][2] = { {95,53},{107,55},{106,55},{95,56},{68,58},{0,0} };
static const uint8_t e4_agatha  [][2] = { {94,56},{42,56},{93,55},{24,56},{94,60},{0,0} };
static const uint8_t e4_lance   [][2] = { {130,58},{148,56},{142,60},{149,56},{149,60},{6,60},{0,0} };
static const uint8_t e4_gary    [][2] = { {18,61},{65,59},{112,61},{103,61},{59,63},{6,65},{0,0} };

static const ElitePoolEntry ELITE_POOL[5] = {
    { "Lorelei", e4_lorelei, 56, "Lapras" },
    { "Bruno",   e4_bruno,   58, "Machamp" },
    { "Agatha",  e4_agatha,  60, "Gengar" },
    { "Lance",   e4_lance,   60, "Dragonite" },
    { "Gary",    e4_gary,    65, "Charizard" },
};

// Look up a trainer by (source, trainer) pair from autoSlots.
// Returns true on success; fills outName, outBossLevel, and (optionally) builds
// the party if outParty != nullptr.
static bool autoLookupTrainer(uint8_t source, uint8_t trainer,
                               const char **outName,
                               uint8_t *outBossLevel,
                               Gen1Party *outParty,
                               uint8_t scaleToBoss = 0)
{
    if (source < GAUNTLET_GYM_COUNT) {
        const GymPreset *g = gauntletGymPreset(source);
        if (!g || trainer >= 5) return false;
        const GymPresetTrainer &tr = g->trainers[trainer];
        uint8_t bossLvl = 0;
        for (uint8_t i = 0; i < tr.monCount; ++i)
            if (tr.party[i].level > bossLvl) bossLvl = tr.party[i].level;
        if (outName)      *outName = tr.name;
        if (outBossLevel) *outBossLevel = bossLvl;
        if (outParty) {
            gauntletBuildPresetTrainerParty(source, trainer, *outParty,
                                              scaleToBoss);
        }
        return true;
    }
    if (source == 8 && trainer < 5) {
        const ElitePoolEntry &e = ELITE_POOL[trainer];
        if (outName)      *outName = e.name;
        if (outBossLevel) *outBossLevel = e.bossLevel;
        if (outParty) {
            // Build a Gen1Party from raw (dex, level) pairs. Re-uses
            // gauntletParseParty by formatting as a CSV — simpler than
            // duplicating the writeMonByDex pipeline.
            char csv[120];
            int off = 0;
            for (uint8_t i = 0; e.party[i][0] != 0 && off < (int)sizeof(csv) - 12; ++i) {
                int useLvl = e.party[i][1];
                if (scaleToBoss > 0) {
                    int delta = (int)e.party[i][1] - (int)e.bossLevel;   // ≤ 0
                    int scaled = (int)scaleToBoss + delta;
                    if (scaled < 1)   scaled = 1;
                    if (scaled > 100) scaled = 100;
                    useLvl = scaled;
                }
                off += snprintf(csv + off, sizeof(csv) - off, "%s%u:%d",
                                 (off > 0 ? "," : ""), (unsigned)e.party[i][0],
                                 useLvl);
            }
            gauntletParseParty(csv, *outParty, e.name);
        }
        return true;
    }
    return false;
}

void GauntletModule::autoSetup()
{
    // Build the 32-grunt pool: 8 gyms × 4 grunts each (slots 0..3).
    struct GruntRef { uint8_t gym; uint8_t trainer; uint8_t bossLvl; };
    GruntRef pool[32];
    uint8_t poolN = 0;
    for (uint8_t g = 0; g < GAUNTLET_GYM_COUNT; ++g) {
        const GymPreset *gp = gauntletGymPreset(g);
        if (!gp) continue;
        for (uint8_t t = 0; t < 4; ++t) {
            const GymPresetTrainer &tr = gp->trainers[t];
            uint8_t bl = 0;
            for (uint8_t i = 0; i < tr.monCount; ++i)
                if (tr.party[i].level > bl) bl = tr.party[i].level;
            pool[poolN++] = { g, t, bl };
        }
    }

    // Pick 4 distinct grunts at random.
    GruntRef chosen[4];
    uint8_t chosenN = 0;
    while (chosenN < 4 && poolN > 0) {
        uint8_t pick = (uint8_t)random(poolN);
        chosen[chosenN++] = pool[pick];
        // Swap-remove from pool to avoid duplicates.
        pool[pick] = pool[--poolN];
    }

    // Sort ascending by boss level (slot 0 = weakest, slot 3 = strongest).
    for (uint8_t i = 0; i + 1 < chosenN; ++i) {
        for (uint8_t j = i + 1; j < chosenN; ++j) {
            if (chosen[j].bossLvl < chosen[i].bossLvl) {
                GruntRef tmp = chosen[i]; chosen[i] = chosen[j]; chosen[j] = tmp;
            }
        }
    }

    // Pick a random leader: 8 gym leaders + 5 elite/champion = 13 candidates.
    // Indices 0..7 → gym leader (source = gym idx, trainer = 4)
    // Indices 8..12 → elite pool (source = 8, trainer = 0..4)
    uint8_t pickL = (uint8_t)random(13);
    uint8_t leaderSource;
    uint8_t leaderTrainer;
    uint8_t leaderBossLvl = 0;
    if (pickL < 8) {
        leaderSource  = pickL;
        leaderTrainer = 4;
        const GymPreset *gp = gauntletGymPreset(pickL);
        if (gp) {
            const GymPresetTrainer &tr = gp->trainers[4];
            for (uint8_t i = 0; i < tr.monCount; ++i)
                if (tr.party[i].level > leaderBossLvl) leaderBossLvl = tr.party[i].level;
        }
    } else {
        leaderSource  = 8;
        leaderTrainer = pickL - 8;
        leaderBossLvl = ELITE_POOL[leaderTrainer].bossLevel;
    }

    // If the leader's canonical boss out-levels slot 3 (strongest grunt),
    // scale the leader down to slot 3's boss level. Otherwise leave canonical.
    uint8_t topGruntBoss = (chosenN > 0) ? chosen[chosenN - 1].bossLvl : 0;
    uint8_t leaderOverride = 0;
    if (topGruntBoss > 0 && leaderBossLvl > topGruntBoss) {
        leaderOverride = topGruntBoss;
    }

    // Stamp into state.
    state_.autoEnabled = 1;
    for (uint8_t i = 0; i < 4; ++i) {
        state_.autoSlots[i][0] = chosen[i].gym;
        state_.autoSlots[i][1] = chosen[i].trainer;
    }
    state_.autoSlots[4][0] = leaderSource;
    state_.autoSlots[4][1] = leaderTrainer;
    state_.autoLeaderLevel = leaderOverride;

    // Update gym name / badge to reflect that this is auto-generated.
    strncpy(state_.gymName,  "MonsterMesh Gym", GAUNTLET_NAME_MAX  - 1);
    strncpy(state_.gymBadge, "Random Badge",    GAUNTLET_BADGE_MAX - 1);

    // Install the leader as the NPC leader (so BBS_FIGHT_REQUEST has a party).
    memset(&state_.leader, 0, sizeof(state_.leader));
    state_.leader.nodeNum   = 0xFFFFFFFEu;
    state_.leader.timestamp = (uint32_t)getTime();
    const char *leaderName = "?";
    autoLookupTrainer(leaderSource, leaderTrainer, &leaderName, nullptr,
                       &state_.leader.party, leaderOverride);
    strncpy(state_.leader.name, leaderName, GAUNTLET_NAME_MAX - 1);

    state_.lastUpdate = (uint32_t)getTime();
    gauntletSave(state_);

    LOG_INFO("Gauntlet: auto-filled — leader=%s (boss L%u%s), grunts: ",
             leaderName, (unsigned)(leaderOverride ? leaderOverride : leaderBossLvl),
             leaderOverride ? "*" : "");
    for (uint8_t i = 0; i < 4; ++i) {
        const char *gn = "?";
        autoLookupTrainer(chosen[i].gym, chosen[i].trainer, &gn, nullptr, nullptr);
        LOG_INFO("%s%s(L%u)", i ? "," : "", gn, (unsigned)chosen[i].bossLvl);
    }
    LOG_INFO("\n");
}

void GauntletModule::gymAdminResetAndOpenWindow()
{
    state_.adminNodeNum = 0;
    state_.gymPresetIdx = 0xFF;
    state_.autoEnabled  = 0;
    memset(state_.memberLevels, 0, sizeof(state_.memberLevels));
    memset(&state_.leader, 0, sizeof(state_.leader));
    strncpy(state_.gymName,  "Pallet Gym",     GAUNTLET_NAME_MAX  - 1);
    strncpy(state_.gymBadge, "Boulder Badge",  GAUNTLET_BADGE_MAX - 1);
    state_.lastUpdate = (uint32_t)getTime();
    gauntletSave(state_);
    // Re-randomise the auto-fill so the gym is still functional while
    // waiting for a new admin to claim.
    autoSetup();
    adminClaimUntilMs_ = millis() + ADMIN_CLAIM_WINDOW_MS;
    LOG_INFO("Gauntlet: admin reset via menu — 120s claim window open\n");
}

bool GauntletModule::gymAdminApply(uint8_t gymIdx, uint8_t memberIdx, uint8_t level)
{
    if (gymIdx >= GAUNTLET_GYM_COUNT) return false;
    if (memberIdx >= 5)               return false;
    if (level > 100)                  return false;
    const GymPreset *g = gauntletGymPreset(gymIdx);
    if (!g) return false;

    // Per-slot source: slot N pulls from source gym G's trainer at index N.
    // The gym is now a "mixed" build — each slot can come from a different
    // Kanto gym (slide flow: pick member → pick source gym → pick level).
    state_.autoEnabled = 1;
    state_.autoSlots[memberIdx][0] = gymIdx;
    state_.autoSlots[memberIdx][1] = memberIdx;
    state_.memberLevels[memberIdx] = level;
    state_.gymPresetIdx = 0xFF;          // legacy field — no longer source of truth

    if (memberIdx == GAUNTLET_GYM_LEADER_IDX) {
        memset(&state_.leader, 0, sizeof(state_.leader));
        state_.leader.nodeNum   = 0xFFFFFFFEu;
        state_.leader.timestamp = (uint32_t)getTime();
        const GymPresetTrainer &lt = g->trainers[memberIdx];
        strncpy(state_.leader.name, lt.name, GAUNTLET_NAME_MAX - 1);
        gauntletBuildPresetTrainerParty(gymIdx, memberIdx,
                                          state_.leader.party, level);
        state_.autoLeaderLevel = 0;       // admin override takes precedence
    }

    state_.lastUpdate = (uint32_t)getTime();
    gauntletSave(state_);
    return true;
}

// ── Per-challenger ladder progression ────────────────────────────────────────
// The challenger (MM Terminal) re-fires BBS_FIGHT_REQUEST after each win to
// step through the gym's 5 trainers. Track progress per nodeNum so we serve
// trainer 0..4 in order; on a win for the LEADER slot (idx 4) trigger the
// existing promoteToLeader path. Loss / silence resets to idx 0.

GauntletModule::GauntletLadderProgress *
GauntletModule::lookupLadder(uint32_t nodeNum)
{
    if (!nodeNum) return nullptr;
    for (uint8_t i = 0; i < LADDER_SLOTS; ++i) {
        if (ladders_[i].nodeNum == nodeNum) return &ladders_[i];
    }
    return nullptr;
}

GauntletModule::GauntletLadderProgress *
GauntletModule::lookupOrInsertLadder(uint32_t nodeNum)
{
    if (!nodeNum) return nullptr;
    GauntletLadderProgress *p = lookupLadder(nodeNum);
    if (p) return p;
    expireLadders();

    // Find an empty slot.
    for (uint8_t i = 0; i < LADDER_SLOTS; ++i) {
        if (ladders_[i].nodeNum == 0) {
            ladders_[i].nodeNum    = nodeNum;
            ladders_[i].ladderIdx  = 0;
            ladders_[i].lastSeenMs = millis();
            return &ladders_[i];
        }
    }

    // All slots full — evict the LRU.
    uint8_t  oldestIdx = 0;
    uint32_t oldestMs  = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < LADDER_SLOTS; ++i) {
        if (ladders_[i].lastSeenMs < oldestMs) {
            oldestMs  = ladders_[i].lastSeenMs;
            oldestIdx = i;
        }
    }
    ladders_[oldestIdx].nodeNum    = nodeNum;
    ladders_[oldestIdx].ladderIdx  = 0;
    ladders_[oldestIdx].lastSeenMs = millis();
    return &ladders_[oldestIdx];
}

void GauntletModule::expireLadders()
{
    uint32_t now = millis();
    for (uint8_t i = 0; i < LADDER_SLOTS; ++i) {
        if (ladders_[i].nodeNum == 0) continue;
        if ((now - ladders_[i].lastSeenMs) > LADDER_IDLE_MS) {
            ladders_[i].nodeNum   = 0;
            ladders_[i].ladderIdx = 0;
        }
    }
}

void GauntletModule::buildLadderTrainerParty(Gen1Party &out, uint8_t idx)
{
    // Leader slot — same priority chain as buildGymBattleParty.
    if (idx == GAUNTLET_GYM_LEADER_IDX) {
        buildGymBattleParty(out);
        return;
    }

    // Grunt slot 0..3 — pull from autoSlots, scale by memberLevels.
    if (state_.autoEnabled && idx < 5) {
        uint8_t scale = state_.memberLevels[idx];
        autoLookupTrainer(state_.autoSlots[idx][0], state_.autoSlots[idx][1],
                           nullptr, nullptr, &out, scale);
        if (out.count > 0) return;
    }
    // Fallback: serve the leader's party so the challenger isn't stuck
    // waiting on an empty packet.
    buildGymBattleParty(out);
}

void GauntletModule::buildGymBattleParty(Gen1Party &out)
{
    // Player-claimed leader takes priority.
    if (state_.leader.nodeNum && state_.leader.nodeNum != 0xFFFFFFFEu) {
        out = state_.leader.party;
        return;
    }
    // Configured gym (auto-fill or admin) → look up the leader slot via
    // autoSlots and build that trainer's party at the current scaling.
    if (state_.autoEnabled) {
        uint8_t scale = state_.memberLevels[GAUNTLET_GYM_LEADER_IDX];
        if (scale == 0 && state_.autoLeaderLevel != 0) scale = state_.autoLeaderLevel;
        autoLookupTrainer(state_.autoSlots[GAUNTLET_GYM_LEADER_IDX][0],
                           state_.autoSlots[GAUNTLET_GYM_LEADER_IDX][1],
                           nullptr, nullptr, &out, scale);
        if (out.count > 0) return;
    }
    // Stored leader (NPC sentinel from previous config).
    if (state_.leader.party.count > 0) {
        out = state_.leader.party;
        return;
    }
    // Final fallback: Lorelei.
    gauntletBuildE4Party(0, out);
}

void GauntletModule::teardownNetBattle()
{
    if (netBattle_) {
        delete netBattle_;
        netBattle_ = nullptr;
    }
    netOpponent_  = 0;
    netSessionId_ = 0;
    netLastRxMs_  = 0;
}

void GauntletModule::onNetBattleEnd()
{
    if (!netBattle_) return;
    auto res = netBattle_->engine.result();
    LOG_INFO("Gauntlet: net battle ended result=%d turns=%u\n",
             (int)res, (unsigned)netBattle_->engine.turn());

    // P2_WIN means side 1 (challenger) won → grant them leader title.
    // (Side 0 = gym/CPU; side 1 = challenger driving from the terminal.)
    if (res == Gen1BattleEngine::Result::P2_WIN && netOpponent_) {
        // Synthesize a minimal session for the existing promote logic.
        // We don't have the challenger's actual party (full party exchange
        // is TODO), so we record the gym's mirror party as a placeholder.
        GauntletSession tmpSess;
        memset(&tmpSess, 0, sizeof(tmpSess));
        tmpSess.nodeNum = netOpponent_;
        buildGymBattleParty(tmpSess.party);

        meshtastic_MeshPacket fakeMp;
        memset(&fakeMp, 0, sizeof(fakeMp));
        fakeMp.from = netOpponent_;

        promoteToLeader(fakeMp, tmpSess);
    }
    teardownNetBattle();
}

// Build + send a BBS_REPLY BattlePacket. Used both as a unicast reply to a
// BBS_PING (legacy / manual probe) and as a broadcast announcement beacon.
// payload format (length-prefixed):
//   u8 nameLen | name[]
//   u8 badgeLen | badge[]
//   u8 leaderLen | leader[]
//   u8 rosterSize
void GauntletModule::sendBbsReply(uint32_t target, uint8_t channel)
{
    if (!service || !router) return;
    meshtastic_MeshPacket *out = router->allocForSending();
    if (!out) return;
    out->to              = target;
    out->channel         = channel;
    out->want_ack        = false;
    out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;

    uint8_t buf[BATTLELINK_MAX_PKT];
    BattlePacket *r = (BattlePacket *)buf;
    memset(buf, 0, sizeof(buf));
    r->type = (uint8_t)PktType::BBS_REPLY;
    r->setSessionId(0);
    r->seq = 0;

    const char *leader = state_.leader.nodeNum ? state_.leader.name : "open";
    uint8_t *p   = r->payload;
    size_t   cap = BATTLELINK_MAX_PAYLOAD;
    size_t   pos = 0;
    auto pushStr = [&](const char *s) {
        uint8_t len = (uint8_t)strlen(s);
        if (len > 24) len = 24;
        if (pos + 1 + len > cap) return;
        p[pos++] = len;
        memcpy(p + pos, s, len);
        pos += len;
    };
    pushStr(state_.gymName);
    pushStr(state_.gymBadge);
    pushStr(leader);
    if (pos < cap) p[pos++] = state_.rosterSize;

    size_t total = BATTLELINK_HDR_SIZE + pos;
    if (total > sizeof(out->decoded.payload.bytes))
        total = sizeof(out->decoded.payload.bytes);
    memcpy(out->decoded.payload.bytes, buf, total);
    out->decoded.payload.size = total;
    service->sendToMesh(out, RX_SRC_LOCAL, true);
}

void GauntletModule::sendBattleStart(uint32_t seed)
{
    if (!netBattle_ || !service || !router) return;
    meshtastic_MeshPacket *out = router->allocForSending();
    if (!out) return;
    out->to               = netOpponent_;
    out->channel          = MM_CHANNEL;  // MonsterMesh — matches T-Deck
    out->want_ack         = false;
    out->decoded.portnum  = meshtastic_PortNum_PRIVATE_APP;

    uint8_t buf[BATTLELINK_MAX_PKT];
    BattlePacket *pkt = (BattlePacket *)buf;
    memset(buf, 0, sizeof(buf));
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_START;
    pkt->setSessionId(netSessionId_);
    pkt->seq = 0;
    pkt->payload[0] = (seed >> 24) & 0xFF;
    pkt->payload[1] = (seed >> 16) & 0xFF;
    pkt->payload[2] = (seed >> 8)  & 0xFF;
    pkt->payload[3] =  seed        & 0xFF;
    pkt->payload[4] = 1;                                  // gen
    pkt->payload[5] = netBattle_->gymParty.count;
    for (int i = 0; i < 8; ++i)
        pkt->payload[6 + i] = netBattle_->gymParty.species[i % 7];

    size_t total = BATTLELINK_HDR_SIZE + 14;
    memcpy(out->decoded.payload.bytes, buf, total);
    out->decoded.payload.size = total;
    service->sendToMesh(out, RX_SRC_LOCAL, true);
}

void GauntletModule::sendBattleAction(uint8_t actionType, uint8_t index)
{
    if (!netBattle_ || !service || !router) return;
    meshtastic_MeshPacket *out = router->allocForSending();
    if (!out) return;
    out->to               = netOpponent_;
    out->channel          = MM_CHANNEL;  // MonsterMesh — matches T-Deck
    out->want_ack         = false;
    out->decoded.portnum  = meshtastic_PortNum_PRIVATE_APP;

    uint8_t buf[BATTLELINK_MAX_PKT];
    BattlePacket *pkt = (BattlePacket *)buf;
    memset(buf, 0, sizeof(buf));
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACTION;
    pkt->setSessionId(netSessionId_);
    pkt->seq = netBattle_->engine.turn() & 0xFF;
    uint16_t turn = netBattle_->engine.turn();
    pkt->payload[0] = (turn >> 8) & 0xFF;
    pkt->payload[1] =  turn       & 0xFF;
    pkt->payload[2] = actionType;
    pkt->payload[3] = index;

    size_t total = BATTLELINK_HDR_SIZE + 4;
    memcpy(out->decoded.payload.bytes, buf, total);
    out->decoded.payload.size = total;
    service->sendToMesh(out, RX_SRC_LOCAL, true);
}

void GauntletModule::handleBattlePacket(const meshtastic_MeshPacket &mp)
{
    const uint8_t *bytes = mp.decoded.payload.bytes;
    size_t         sz    = mp.decoded.payload.size;
    if (sz < BATTLELINK_HDR_SIZE) return;
    const BattlePacket *pkt = (const BattlePacket *)bytes;
    PktType t = (PktType)pkt->type;

    // ── BBS_PING — discovery probe (kept for backward compat / manual test) ──
    // The MM Terminal no longer sends BBS_PING (active probes leaked to
    // LongFast); discovery is now passive — see runOnce() / sendBbsReply()
    // below for the periodic announcement. Still answer if someone DMs us
    // a BBS_PING manually.
    if (t == PktType::BBS_PING) {
        sendBbsReply(mp.from, mp.channel);
        return;
    }

    // ── BBS_FIGHT_REQUEST — T-Deck wants to fight us ──────────────────────
    // Send the trainer party for the challenger's CURRENT ladder slot.
    // The challenger steps through trainers 0..4 by re-firing requests
    // after each win; we track per-sender progress in `ladders_`.
    if (t == PktType::BBS_FIGHT_REQUEST) {
        GauntletLadderProgress *p = lookupOrInsertLadder(mp.from);
        uint8_t idx = p ? p->ladderIdx : GAUNTLET_GYM_LEADER_IDX;
        if (p) p->lastSeenMs = millis();
        Gen1Party gym;
        buildLadderTrainerParty(gym, idx);
        LOG_INFO("Gauntlet: BBS request from %08x → trainer %u\n",
                 (unsigned)mp.from, (unsigned)idx);

        const uint8_t *src = (const uint8_t *)&gym;
        size_t total       = sizeof(Gen1Party);

        // Chunk size leaves 2 bytes for partIdx + partTotal in payload.
        const size_t CHUNK = BATTLELINK_MAX_PAYLOAD - 2;
        uint8_t totalParts = (uint8_t)((total + CHUNK - 1) / CHUNK);

        for (uint8_t i = 0; i < totalParts; ++i) {
            size_t off = (size_t)i * CHUNK;
            size_t len = (off + CHUNK > total) ? (total - off) : CHUNK;

            if (!service || !router) return;
            meshtastic_MeshPacket *out = router->allocForSending();
            if (!out) return;
            out->to              = mp.from;
            out->channel         = MM_CHANNEL;
            out->want_ack        = false;
            out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;

            uint8_t buf[BATTLELINK_MAX_PKT];
            BattlePacket *p2 = (BattlePacket *)buf;
            memset(buf, 0, sizeof(buf));
            p2->type = (uint8_t)PktType::TEXT_BATTLE_PARTY;
            p2->setSessionId(pkt->sessionId());
            p2->seq = i;
            p2->payload[0] = i;
            p2->payload[1] = totalParts;
            memcpy(p2->payload + 2, src + off, len);

            size_t pktTotal = BATTLELINK_HDR_SIZE + 2 + len;
            memcpy(out->decoded.payload.bytes, buf, pktTotal);
            out->decoded.payload.size = pktTotal;
            service->sendToMesh(out, RX_SRC_LOCAL, true);
        }
        LOG_INFO("Gauntlet: BBS fight party sent (%u chunks) to %08x\n",
                 (unsigned)totalParts, (unsigned)mp.from);
        return;
    }

    // ── BBS_LADDER_REQUEST — bulk-dump all 5 trainer parties + names ──────
    // Sends two PRIVATE_APP packets back to the challenger:
    //   1. BBS_LADDER_NAMES   — 5 trainer names (length-prefixed)
    //   2. BBS_LADDER_PARTIES — 5 minimal parties (dex+level+moves per mon)
    // T-Deck runs all 5 fights locally without re-requesting; only emits
    // BBS_FIGHT_RESULT for the final leader fight (or an early-loss report).
    if (t == PktType::BBS_LADDER_REQUEST) {
        if (!service || !router) return;

        // Helper: write minimal-format mons for slot `idx` into `out[]`.
        // Returns mon count (0..6). Honors per-member level scaling.
        struct LadderMonWire { uint8_t dex, level, moves[4]; };
        auto buildSlot = [&](uint8_t idx, LadderMonWire out[6]) -> uint8_t {
            if (!state_.autoEnabled || idx >= 5) return 0;
            uint8_t source  = state_.autoSlots[idx][0];
            uint8_t trainer = state_.autoSlots[idx][1];
            uint8_t scaleTo = state_.memberLevels[idx];
            if (idx == GAUNTLET_GYM_LEADER_IDX && scaleTo == 0)
                scaleTo = state_.autoLeaderLevel;

            // Kanto preset gym source.
            if (source < GAUNTLET_GYM_COUNT) {
                const GymPreset *g = gauntletGymPreset(source);
                if (!g || trainer >= 5) return 0;
                const GymPresetTrainer &tr = g->trainers[trainer];
                uint8_t canonBoss = 0;
                for (uint8_t i = 0; i < tr.monCount; ++i)
                    if (tr.party[i].level > canonBoss) canonBoss = tr.party[i].level;
                if (canonBoss == 0) canonBoss = 1;

                uint8_t n = 0;
                for (uint8_t i = 0; i < tr.monCount && n < 6; ++i) {
                    const GymPresetMon &m = tr.party[i];
                    if (m.dex == 0) continue;
                    uint8_t lvl = m.level;
                    if (scaleTo > 0) {
                        int delta = (int)m.level - (int)canonBoss;     // ≤ 0
                        int s = (int)scaleTo + delta;
                        if (s < 1) s = 1; if (s > 100) s = 100;
                        lvl = (uint8_t)s;
                    }
                    out[n].dex   = m.dex;
                    out[n].level = lvl;
                    bool hasMoves = (m.moves[0] || m.moves[1] || m.moves[2] || m.moves[3]);
                    if (hasMoves) memcpy(out[n].moves, m.moves, 4);
                    else {
                        // Mirror GauntletBattle.cpp's pickDefaultMoves table —
                        // 4 default moves keyed off primary type.
                        uint8_t type1 = GEN1_BASE_STATS[m.dex].type1;
                        if (type1 >= 16) type1 = 0;
                        static const uint8_t kDefaultMoves[16][4] = {
                            { 33,  34,  98,  63 }, {  2,  67,  66,  69 }, { 17,  64,  65,  19 },
                            { 40, 124, 123,  51 }, { 89,  91,  28,  31 }, { 88, 157,  89,  33 },
                            { 17,  64,  65,  19 }, { 42,  41, 141,  33 }, {122, 109, 101,  95 },
                            { 52,  53, 126,   7 }, { 55,  57,  56, 145 }, { 22,  75,  76,  79 },
                            { 84,  85,  87,  86 }, { 93,  94,  95,  60 }, { 58,  59,  62,   8 },
                            { 82,  63,  21,  33 },
                        };
                        memcpy(out[n].moves, kDefaultMoves[type1], 4);
                    }
                    n++;
                }
                return n;
            }

            // Elite Four / Champion source.
            if (source == 8 && trainer < 5) {
                const ElitePoolEntry &e = ELITE_POOL[trainer];
                uint8_t canonBoss = e.bossLevel ? e.bossLevel : 1;
                uint8_t n = 0;
                for (uint8_t i = 0; e.party[i][0] != 0 && n < 6; ++i) {
                    uint8_t dex = e.party[i][0];
                    uint8_t lvl = e.party[i][1];
                    if (scaleTo > 0) {
                        int delta = (int)lvl - (int)canonBoss;
                        int s = (int)scaleTo + delta;
                        if (s < 1) s = 1; if (s > 100) s = 100;
                        lvl = (uint8_t)s;
                    }
                    out[n].dex   = dex;
                    out[n].level = lvl;
                    uint8_t type1 = (dex >= 1 && dex <= 151) ? GEN1_BASE_STATS[dex].type1 : 0;
                    if (type1 >= 16) type1 = 0;
                    static const uint8_t kDefaultMoves[16][4] = {
                        { 33,  34,  98,  63 }, {  2,  67,  66,  69 }, { 17,  64,  65,  19 },
                        { 40, 124, 123,  51 }, { 89,  91,  28,  31 }, { 88, 157,  89,  33 },
                        { 17,  64,  65,  19 }, { 42,  41, 141,  33 }, {122, 109, 101,  95 },
                        { 52,  53, 126,   7 }, { 55,  57,  56, 145 }, { 22,  75,  76,  79 },
                        { 84,  85,  87,  86 }, { 93,  94,  95,  60 }, { 58,  59,  62,   8 },
                        { 82,  63,  21,  33 },
                    };
                    memcpy(out[n].moves, kDefaultMoves[type1], 4);
                    n++;
                }
                return n;
            }
            return 0;
        };

        // Helper: trainer display name for slot idx.
        auto trainerName = [&](uint8_t idx) -> const char * {
            if (!state_.autoEnabled || idx >= 5) return "?";
            uint8_t source  = state_.autoSlots[idx][0];
            uint8_t trainer = state_.autoSlots[idx][1];
            if (source < GAUNTLET_GYM_COUNT) {
                const GymPreset *g = gauntletGymPreset(source);
                if (!g || trainer >= 5) return "?";
                return g->trainers[trainer].name;
            }
            if (source == 8 && trainer < 5) return ELITE_POOL[trainer].name;
            return "?";
        };

        auto allocLadderPkt = [&](PktType pktType) -> meshtastic_MeshPacket * {
            meshtastic_MeshPacket *out = router->allocForSending();
            if (!out) return nullptr;
            out->to              = mp.from;
            out->channel         = MM_CHANNEL;
            out->want_ack        = false;
            out->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
            return out;
        };

        // ── Packet 1: BBS_LADDER_NAMES ────────────────────────────────────
        // Layout: u8 trainerCount, [trainer name lenpfx]×N, u8 gymNameLen,
        // gymName[], u8 badgeLen, badge[]. Gym name + badge tail are
        // optional — older challengers stop reading after the trainer
        // names and don't notice them.
        {
            meshtastic_MeshPacket *out = allocLadderPkt(PktType::BBS_LADDER_NAMES);
            if (!out) return;
            uint8_t buf[BATTLELINK_MAX_PKT];
            BattlePacket *p2 = (BattlePacket *)buf;
            memset(buf, 0, sizeof(buf));
            p2->type = (uint8_t)PktType::BBS_LADDER_NAMES;
            p2->setSessionId(pkt->sessionId());
            p2->seq = 0;
            uint8_t off = 0;
            p2->payload[off++] = 5;   // trainerCount
            for (uint8_t i = 0; i < 5; ++i) {
                const char *nm = trainerName(i);
                size_t nmLen = strlen(nm);
                if (nmLen > 16) nmLen = 16;
                if (off + 1 + nmLen > sizeof(p2->payload)) break;
                p2->payload[off++] = (uint8_t)nmLen;
                memcpy(p2->payload + off, nm, nmLen);
                off += nmLen;
            }
            // Optional tail: gymName + badge so the T-Deck can title the
            // battle screen as "<gymName> [<badge>] — Trainer N/5".
            const char *gymNameStr  = state_.gymName[0]  ? state_.gymName  : "";
            const char *gymBadgeStr = state_.gymBadge[0] ? state_.gymBadge : "";
            size_t gnLen = strlen(gymNameStr);  if (gnLen > 16) gnLen = 16;
            size_t gbLen = strlen(gymBadgeStr); if (gbLen > 16) gbLen = 16;
            if (off + 2 + gnLen + gbLen <= sizeof(p2->payload)) {
                p2->payload[off++] = (uint8_t)gnLen;
                memcpy(p2->payload + off, gymNameStr, gnLen);
                off += gnLen;
                p2->payload[off++] = (uint8_t)gbLen;
                memcpy(p2->payload + off, gymBadgeStr, gbLen);
                off += gbLen;
            }
            size_t pktTotal = BATTLELINK_HDR_SIZE + off;
            memcpy(out->decoded.payload.bytes, buf, pktTotal);
            out->decoded.payload.size = pktTotal;
            service->sendToMesh(out, RX_SRC_LOCAL, true);
        }

        // ── Packet 2: BBS_LADDER_PARTIES ──────────────────────────────────
        {
            meshtastic_MeshPacket *out = allocLadderPkt(PktType::BBS_LADDER_PARTIES);
            if (!out) return;
            uint8_t buf[BATTLELINK_MAX_PKT];
            BattlePacket *p2 = (BattlePacket *)buf;
            memset(buf, 0, sizeof(buf));
            p2->type = (uint8_t)PktType::BBS_LADDER_PARTIES;
            p2->setSessionId(pkt->sessionId());
            p2->seq = 1;
            uint8_t off = 0;
            p2->payload[off++] = 5;   // trainerCount
            for (uint8_t i = 0; i < 5; ++i) {
                LadderMonWire mons[6];
                uint8_t mc = buildSlot(i, mons);
                if (off + 1 + (size_t)mc * 6 > sizeof(p2->payload)) break;
                p2->payload[off++] = mc;
                for (uint8_t j = 0; j < mc; ++j) {
                    p2->payload[off++] = mons[j].dex;
                    p2->payload[off++] = mons[j].level;
                    p2->payload[off++] = mons[j].moves[0];
                    p2->payload[off++] = mons[j].moves[1];
                    p2->payload[off++] = mons[j].moves[2];
                    p2->payload[off++] = mons[j].moves[3];
                }
            }
            size_t pktTotal = BATTLELINK_HDR_SIZE + off;
            memcpy(out->decoded.payload.bytes, buf, pktTotal);
            out->decoded.payload.size = pktTotal;
            service->sendToMesh(out, RX_SRC_LOCAL, true);
        }

        LOG_INFO("Gauntlet: BBS ladder dump sent to %08x\n", (unsigned)mp.from);
        return;
    }

    // ── BBS_FIGHT_RESULT — T-Deck reports the outcome ─────────────────────
    // Payload: u8 outcome (0 lost / 1 won), u8 nameLen, name[]
    if (t == PktType::BBS_FIGHT_RESULT) {
        if (sz < BATTLELINK_HDR_SIZE + 1) return;
        uint8_t outcome = pkt->payload[0];
        char    name[GAUNTLET_NAME_MAX] = {0};
        if (sz >= BATTLELINK_HDR_SIZE + 2) {
            uint8_t nameLen = pkt->payload[1];
            size_t  avail   = sz - (BATTLELINK_HDR_SIZE + 2);
            if (nameLen > avail) nameLen = avail;
            if (nameLen >= sizeof(name)) nameLen = sizeof(name) - 1;
            memcpy(name, pkt->payload + 2, nameLen);
            name[nameLen] = '\0';
        }
        if (!name[0]) {
            const char *fb = senderShortName(mp.from);
            strncpy(name, fb ? fb : "?", sizeof(name) - 1);
        }

        // Find (or insert) the ladder entry — needed to know which trainer
        // the challenger just beat.
        GauntletLadderProgress *p = lookupOrInsertLadder(mp.from);
        uint8_t idx = p ? p->ladderIdx : GAUNTLET_GYM_LEADER_IDX;

        LOG_INFO("Gauntlet: BBS fight result from %08x: %s by %s (trainer %u)\n",
                 (unsigned)mp.from, outcome ? "WIN" : "LOSS", name, (unsigned)idx);

        if (outcome == 1 && idx < GAUNTLET_GYM_LEADER_IDX) {
            // Won a grunt fight — advance ladder, no leader change.
            if (p) {
                p->ladderIdx++;
                p->lastSeenMs = millis();
            }
            state_.totalBattles++;
            gauntletSave(state_);
            return;
        }

        if (outcome == 1) {
            // Won the leader fight (idx == 4) — promote to gym leader and
            // reset their ladder so the next run starts at trainer 0.
            GauntletSession tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.nodeNum = mp.from;
            buildGymBattleParty(tmp.party);

            meshtastic_MeshPacket fakeMp;
            memset(&fakeMp, 0, sizeof(fakeMp));
            fakeMp.from = mp.from;
            promoteToLeader(fakeMp, tmp);

            char partyCsv[64];
            gauntletFormatParty(tmp.party, partyCsv, sizeof(partyCsv));
            gauntletProfileUpdate(mp.from, name, partyCsv,
                                  /*challengeStarted*/true,
                                  /*ranked*/false, /*rank*/0,
                                  /*becameLeader*/true,
                                  /*reachedLeader*/true);

            if (p) {
                p->ladderIdx  = 0;
                p->lastSeenMs = millis();
            }
        } else {
            // Loss — bump challenge counter, reset ladder.
            gauntletProfileUpdate(mp.from, name, nullptr,
                                  /*challengeStarted*/true,
                                  /*ranked*/false, /*rank*/0,
                                  /*becameLeader*/false,
                                  /*reachedLeader*/false);
            if (p) {
                p->ladderIdx  = 0;
                p->lastSeenMs = millis();
            }
        }
        state_.totalBattles++;
        state_.totalChallenges++;
        gauntletSave(state_);
        return;
    }

    // (TEXT_BATTLE_START / ACTION / FORFEIT / HASH from the old per-turn
    // protocol are deliberately unhandled — gym side no longer drives the
    // engine. T-Deck runs the battle locally now.)
}

// ── On-device gym frame + admin slide ───────────────────────────────────────
//
// The carousel slot after Pikachu shows gym status (5-member roster with
// effective boss levels). Long-press from this frame opens the admin slide:
//
//   ADM_MAIN  ─Setup→ ADM_PICK_GYM → ADM_PICK_MEMBER → ADM_LEVEL_CHOICE
//                                                       ├─Stock→ ADM_SUMMARY
//                                                       └─Custom→ ADM_LEVEL_TENS
//                                                                  → ADM_LEVEL_ONES
//                                                                  → ADM_SUMMARY
//             ─Reset Admin→ ADM_RESET_CONFIRM → ADM_RESET_DONE
//             ─Back→ exit
//
// Short press cycles within a step, long press confirms/advances. The slide
// bypasses ACL — physical presence implies trust.

#if HAS_SCREEN

#include "modules/pikachu/config.h"  // SCREEN_W

int GauntletModule::handleInputEvent(const InputEvent *event)
{
    // Only act when the gym frame was the last drawn frame. 3 s window so a
    // parked carousel doesn't lose presses.
    if (lastDrawMs_ == 0 || (millis() - lastDrawMs_) > 3000) return 0;

    if (admUi_ != ADM_OFF) {
        if (event->inputEvent == INPUT_BROKER_SELECT)         admLongPress();
        else if (event->inputEvent == INPUT_BROKER_USER_PRESS) admShortPress();
        return 1;
    }

    // Long press from gym status → enter admin slide.
    if (event->inputEvent == INPUT_BROKER_SELECT) {
        admEnter();
        return 1;
    }
    return 0;
}

void GauntletModule::admEnter()
{
    admUi_           = ADM_MAIN;
    admMainCursor_   = 0;
    admGymIdx_       = 0;
    admMemberIdx_    = 0;
    admChoice_       = 0;
    admTens_         = 0;
    admOnes_         = 0;
    admResetConfirm_ = 0;
    admSummaryUntil_ = 0;
}

void GauntletModule::admExit()
{
    admUi_ = ADM_OFF;
    admSummaryUntil_ = 0;
}

void GauntletModule::admShortPress()
{
    switch (admUi_) {
        case ADM_MAIN:          admMainCursor_   = (uint8_t)((admMainCursor_   + 1) % 4);  break;  // Setup/Show/Reset/Back
        case ADM_PICK_GYM:      admGymIdx_       = (uint8_t)((admGymIdx_       + 1) % 8);  break;
        case ADM_PICK_MEMBER:   admMemberIdx_    = (uint8_t)((admMemberIdx_    + 1) % 6);  break;  // 0..4=slots, 5=Back
        case ADM_LEVEL_CHOICE:  admChoice_       = (uint8_t)((admChoice_       + 1) % 2);  break;
        case ADM_LEVEL_TENS:    admTens_         = (uint8_t)((admTens_         + 1) % 10); break;
        case ADM_LEVEL_ONES:    admOnes_         = (uint8_t)((admOnes_         + 1) % 10); break;
        case ADM_RESET_CONFIRM: admResetConfirm_ = (uint8_t)((admResetConfirm_ + 1) % 2);  break;
        case ADM_CLAIM_WINDOW:  break;     // ignore short press during countdown
        case ADM_WINDOW_CLOSED:
        case ADM_ADMIN_CLAIMED:
        case ADM_SUMMARY:
        case ADM_SHOW_ADMIN:    admExit(); break;
        default: break;
    }
}

void GauntletModule::admLongPress()
{
    switch (admUi_) {
        case ADM_MAIN:
            if      (admMainCursor_ == 0) { admUi_ = ADM_PICK_MEMBER; admMemberIdx_ = 0; }
            else if (admMainCursor_ == 1) {
                admUi_ = ADM_SHOW_ADMIN;
                admSummaryUntil_ = millis() + 6000;
            }
            else if (admMainCursor_ == 2) { admUi_ = ADM_RESET_CONFIRM; admResetConfirm_ = 0; }
            else                          { admExit(); }
            break;
        case ADM_PICK_MEMBER:
            if (admMemberIdx_ == 5) admUi_ = ADM_MAIN;     // "Back"
            else { admUi_ = ADM_PICK_GYM; admGymIdx_ = 0; }
            break;
        case ADM_PICK_GYM:    admUi_ = ADM_LEVEL_CHOICE; admChoice_ = 0; break;
        case ADM_LEVEL_CHOICE:
            if (admChoice_ == 0) admApplyAndShowSummary();
            else { admUi_ = ADM_LEVEL_TENS; admTens_ = 0; admOnes_ = 0; }
            break;
        case ADM_LEVEL_TENS:  admUi_ = ADM_LEVEL_ONES; break;
        case ADM_LEVEL_ONES:  admApplyAndShowSummary(); break;
        case ADM_RESET_CONFIRM:
            if (admResetConfirm_ == 1) {
                gymAdminResetAndOpenWindow();
                admUi_ = ADM_CLAIM_WINDOW;
                admSummaryUntil_ = 0;        // not used in countdown state
            } else {
                admUi_ = ADM_MAIN;
            }
            break;
        case ADM_CLAIM_WINDOW:
        case ADM_WINDOW_CLOSED:
        case ADM_ADMIN_CLAIMED:
        case ADM_SUMMARY:
        case ADM_SHOW_ADMIN: admExit(); break;
        default: break;
    }
}

void GauntletModule::admApplyAndShowSummary()
{
    uint8_t level = 0;
    if (admChoice_ == 1) {
        int lv = admTens_ * 10 + admOnes_;
        if (lv < 1)   lv = 1;
        if (lv > 100) lv = 100;
        level = (uint8_t)lv;
    }
    gymAdminApply(admGymIdx_, admMemberIdx_, level);
    admUi_ = ADM_SUMMARY;
    admSummaryUntil_ = millis() + 4000;
}

void GauntletModule::drawGymStatus(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    // T114 (240×135 colour TFT) doubles font + row pitch. Heltec V3 (128×64
    // mono OLED) keeps the original tight Plain_10 layout.
#if defined(ARCH_NRF52)
    // Plain_24 header + Plain_16 rows — Plain_24 across the board was too
    // cramped to fit 5 trainers + leader highlight in the 135 px height.
    const auto FONT_HDR  = ArialMT_Plain_24;
    const auto FONT_ROW  = ArialMT_Plain_16;
    const int  HDR_H     = 26;     // y offset to first row
    const int  ROW_PITCH = 18;
    const int  ROW_H     = 17;     // fillRect height for leader highlight
#else
    const auto FONT_HDR  = ArialMT_Plain_10;
    const auto FONT_ROW  = ArialMT_Plain_10;
    const int  HDR_H     = 11;
    const int  ROW_PITCH = 10;
    const int  ROW_H     = 10;
#endif
    display->setFont(FONT_HDR);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // Header — show "Admin: SHORTNAME" if admin is claimed, else gym name.
    // If gymName is empty, fall back to the device's long name.
    char hdr[24];
    if (state_.adminNodeNum != 0) {
        snprintf(hdr, sizeof(hdr), "Admin: %s",
                 senderShortName(state_.adminNodeNum));
    } else {
        const char *base = state_.gymName[0] ? state_.gymName : nullptr;
        if (!base && nodeDB) {
            const meshtastic_NodeInfoLite *me = nodeDB->getMeshNode(nodeDB->getNodeNum());
            if (me && me->has_user && me->user.long_name[0])
                base = me->user.long_name;
        }
        if (!base) base = "MonsterMesh Gym";
        snprintf(hdr, sizeof(hdr), "%s", base);
    }
    display->drawString(ox + 2, oy + 0, hdr);

    char buf[28];
    display->setFont(FONT_ROW);

    if (!state_.autoEnabled) {
        display->drawString(ox + 2, oy + HDR_H,                 "(no gym set)");
        display->drawString(ox + 2, oy + HDR_H + ROW_PITCH,     "Long press \xe2\x86\x92");
        display->drawString(ox + 2, oy + HDR_H + ROW_PITCH * 2, "set up admin");
        return;
    }

    // Render the 5-slot lineup from autoSlots.
    for (uint8_t i = 0; i < 5; ++i) {
        const char *nm = "?";
        uint8_t bossLvl = 0;
        autoLookupTrainer(state_.autoSlots[i][0], state_.autoSlots[i][1],
                           &nm, &bossLvl, nullptr);
        uint8_t over = state_.memberLevels[i];
        if (i == GAUNTLET_GYM_LEADER_IDX && over == 0 && state_.autoLeaderLevel != 0)
            over = state_.autoLeaderLevel;
        uint8_t eff = over ? over : bossLvl;
        snprintf(buf, sizeof(buf), "%u.%-7s L%u%s",
                 (unsigned)(i + 1), nm, (unsigned)eff,
                 over ? "*" : "");
        int yy = oy + HDR_H + i * ROW_PITCH;
        if (i == GAUNTLET_GYM_LEADER_IDX) {
            display->fillRect(ox + 0, yy, display->getWidth(), ROW_H);
            display->setColor(BLACK);
            display->drawString(ox + 2, yy, buf);
            display->setColor(WHITE);
        } else {
            display->drawString(ox + 2, yy, buf);
        }
    }
}

void GauntletModule::drawAdminSlide(OLEDDisplay *display, int16_t ox, int16_t oy)
{
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const GymPreset *g = gauntletGymPreset(admGymIdx_);
    const char *gymName    = g ? g->name : "?";
    const char *leaderName = g ? g->leaderName : "?";
    char buf[40];

    switch (admUi_) {
        case ADM_MAIN: {
            display->drawString(ox + 2, oy + 0, "Gym Admin");
            const char *opts[4] = { "Setup Gym", "Show Admin", "Reset Admin", "Back" };
            for (uint8_t i = 0; i < 4; ++i) {
                int yy = oy + 14 + i * 12;
                if (i == admMainCursor_) {
                    display->fillRect(ox + 0, yy, SCREEN_W, 12);
                    display->setColor(BLACK);
                    display->drawString(ox + 6, yy, opts[i]);
                    display->setColor(WHITE);
                } else {
                    display->drawString(ox + 6, yy, opts[i]);
                }
            }
            break;
        }

        case ADM_SHOW_ADMIN: {
            display->drawString(ox + 2, oy + 0, "Current admin");
            char line[40];
            if (state_.adminNodeNum == 0) {
                display->setFont(ArialMT_Plain_24);
                display->setTextAlignment(TEXT_ALIGN_CENTER);
                display->drawString(ox + SCREEN_W / 2, oy + 22, "(none)");
                display->setFont(ArialMT_Plain_10);
                display->setTextAlignment(TEXT_ALIGN_LEFT);
            } else {
                const char *sn = senderShortName(state_.adminNodeNum);
                display->setFont(ArialMT_Plain_24);
                display->setTextAlignment(TEXT_ALIGN_CENTER);
                display->drawString(ox + SCREEN_W / 2, oy + 14, sn);
                display->setFont(ArialMT_Plain_10);
                display->setTextAlignment(TEXT_ALIGN_LEFT);
                snprintf(line, sizeof(line), "0x%08x", (unsigned)state_.adminNodeNum);
                display->setTextAlignment(TEXT_ALIGN_CENTER);
                display->drawString(ox + SCREEN_W / 2, oy + 42, line);
                display->setTextAlignment(TEXT_ALIGN_LEFT);
            }
            break;
        }

        case ADM_PICK_MEMBER: {
            // Step 1 of the slide: pick which slot (1-5) to configure, or
            // cycle to "Back" (admMemberIdx_ == 5) to return to the main
            // menu. Shows the trainer currently in the selected slot so the
            // admin knows what's there.
            display->drawString(ox + 2, oy + 0, "Pick member");
            if (admMemberIdx_ == 5) {
                display->setFont(ArialMT_Plain_24);
                display->setTextAlignment(TEXT_ALIGN_CENTER);
                display->drawString(ox + SCREEN_W / 2, oy + 22, "Back");
                display->setFont(ArialMT_Plain_10);
                display->setTextAlignment(TEXT_ALIGN_LEFT);
            } else {
                display->setFont(ArialMT_Plain_24);
                display->setTextAlignment(TEXT_ALIGN_CENTER);
                snprintf(buf, sizeof(buf), "%u", (unsigned)(admMemberIdx_ + 1));
                display->drawString(ox + 16, oy + 16, buf);
                display->setFont(ArialMT_Plain_10);
                display->setTextAlignment(TEXT_ALIGN_LEFT);
                const char *curName = "(empty)";
                uint8_t     curBoss = 0;
                if (state_.autoEnabled) {
                    autoLookupTrainer(state_.autoSlots[admMemberIdx_][0],
                                       state_.autoSlots[admMemberIdx_][1],
                                       &curName, &curBoss, nullptr);
                }
                display->drawString(ox + 36, oy + 16, curName);
                uint8_t over = state_.memberLevels[admMemberIdx_];
                uint8_t eff  = over ? over : curBoss;
                snprintf(buf, sizeof(buf), "L%u%s (cur)",
                         (unsigned)eff, over ? "*" : "");
                display->drawString(ox + 36, oy + 28, buf);
            }
            break;
        }

        case ADM_PICK_GYM: {
            // Step 2: pick which Kanto gym to source the trainer from.
            // Show the picked source gym's trainer at slot admMemberIdx_.
            display->drawString(ox + 2, oy + 0, "Source gym for slot");
            display->setFont(ArialMT_Plain_24);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            snprintf(buf, sizeof(buf), "%u", (unsigned)(admGymIdx_ + 1));
            display->drawString(ox + 16, oy + 16, buf);
            display->setFont(ArialMT_Plain_10);
            display->setTextAlignment(TEXT_ALIGN_LEFT);
            display->drawString(ox + 36, oy + 16, gymName);
            if (g) {
                const GymPresetTrainer &tr = g->trainers[admMemberIdx_];
                uint8_t bossLvl = 0;
                for (uint8_t i = 0; i < tr.monCount; ++i)
                    if (tr.party[i].level > bossLvl) bossLvl = tr.party[i].level;
                snprintf(buf, sizeof(buf), "%s L%u", tr.name, (unsigned)bossLvl);
                display->drawString(ox + 36, oy + 28, buf);
            }
            break;
        }

        case ADM_LEVEL_CHOICE: {
            const char *trName = (g ? g->trainers[admMemberIdx_].name : "?");
            display->drawString(ox + 2, oy + 0, trName);
            const char *opts[2] = { "Stock", "Custom" };
            for (uint8_t i = 0; i < 2; ++i) {
                int xx = ox + 8 + i * 60;
                int yy = oy + 22;
                if (i == admChoice_) {
                    display->fillRect(xx - 4, yy - 2, 56, 16);
                    display->setColor(BLACK);
                    display->drawString(xx, yy, opts[i]);
                    display->setColor(WHITE);
                } else {
                    display->drawString(xx, yy, opts[i]);
                }
            }
            break;
        }

        case ADM_LEVEL_TENS:
        case ADM_LEVEL_ONES: {
            const char *trName = (g ? g->trainers[admMemberIdx_].name : "?");
            snprintf(buf, sizeof(buf), "%s level", trName);
            display->drawString(ox + 2, oy + 0, buf);

            display->setFont(ArialMT_Plain_24);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            char tens[2] = { (char)('0' + admTens_), 0 };
            char ones[2] = { (char)('0' + admOnes_), 0 };
            int dx = ox + SCREEN_W / 2;
            display->drawString(dx - 12, oy + 16, tens);
            display->drawString(dx + 12, oy + 16, ones);
            int ux = (admUi_ == ADM_LEVEL_TENS) ? (dx - 18) : (dx + 6);
            display->fillRect(ux, oy + 42, 12, 2);

            display->setFont(ArialMT_Plain_10);
            display->setTextAlignment(TEXT_ALIGN_LEFT);
            break;
        }

        case ADM_RESET_CONFIRM: {
            display->drawString(ox + 2, oy + 0,  "Reset admin?");
            display->drawString(ox + 2, oy + 12, "Drops admin and");
            display->drawString(ox + 2, oy + 22, "opens 120s claim.");
            const char *opts[2] = { "No", "Yes" };
            for (uint8_t i = 0; i < 2; ++i) {
                int xx = ox + 16 + i * 56;
                int yy = oy + 38;
                if (i == admResetConfirm_) {
                    display->fillRect(xx - 4, yy - 2, 36, 14);
                    display->setColor(BLACK);
                    display->drawString(xx, yy, opts[i]);
                    display->setColor(WHITE);
                } else {
                    display->drawString(xx, yy, opts[i]);
                }
            }
            break;
        }

        case ADM_CLAIM_WINDOW: {
            uint32_t now = millis();
            uint32_t left = (adminClaimUntilMs_ > now)
                                ? (adminClaimUntilMs_ - now) / 1000 : 0;
            display->drawString(ox + 2, oy + 0,  "Admin claim window");
            display->drawString(ox + 2, oy + 14, "Send DM:");
            display->drawString(ox + 2, oy + 24, "  admin set");
            display->setFont(ArialMT_Plain_24);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            char tbuf[8];
            snprintf(tbuf, sizeof(tbuf), "%us", (unsigned)left);
            display->drawString(ox + SCREEN_W / 2, oy + 36, tbuf);
            display->setFont(ArialMT_Plain_10);
            display->setTextAlignment(TEXT_ALIGN_LEFT);
            break;
        }
        case ADM_WINDOW_CLOSED:
            display->setFont(ArialMT_Plain_24);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(ox + SCREEN_W / 2, oy + 18, "Window");
            display->drawString(ox + SCREEN_W / 2, oy + 38, "closed");
            display->setFont(ArialMT_Plain_10);
            display->setTextAlignment(TEXT_ALIGN_LEFT);
            break;
        case ADM_ADMIN_CLAIMED:
            display->drawString(ox + 2, oy + 0, "Admin set:");
            display->setFont(ArialMT_Plain_24);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(ox + SCREEN_W / 2, oy + 22,
                                 senderShortName(state_.adminNodeNum));
            display->setFont(ArialMT_Plain_10);
            display->setTextAlignment(TEXT_ALIGN_LEFT);
            break;

        case ADM_SUMMARY: {
            display->drawString(ox + 2, oy + 0, gymName);
            if (!g) break;
            for (uint8_t i = 0; i < 5; ++i) {
                const GymPresetTrainer &t = g->trainers[i];
                uint8_t cb = 0;
                for (uint8_t k = 0; k < t.monCount; ++k)
                    if (t.party[k].level > cb) cb = t.party[k].level;
                uint8_t over = state_.memberLevels[i];
                uint8_t eff  = over ? over : cb;
                snprintf(buf, sizeof(buf), "%u.%-7s L%u%s",
                         (unsigned)(i + 1), t.name, (unsigned)eff,
                         over ? "*" : "");
                display->drawString(ox + 2, oy + 11 + i * 10, buf);
            }
            (void)leaderName;
            break;
        }

        default: break;
    }
}

void GauntletModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                                int16_t x, int16_t y)
{
    lastDrawMs_ = millis();
    display->clear();

    if (admUi_ != ADM_OFF) {
        uint32_t now = millis();
        // Auto-close gym setup summary.
        if (admUi_ == ADM_SUMMARY && admSummaryUntil_ != 0 &&
            now >= admSummaryUntil_) {
            admExit();
        }
        // Live claim countdown: a successful claim flips to ADM_ADMIN_CLAIMED
        // (shows the new admin's shortname); the window expiring with NO
        // claim flips to ADM_WINDOW_CLOSED.
        if (admUi_ == ADM_CLAIM_WINDOW) {
            bool gotClaimed = (state_.adminNodeNum != 0);
            bool windowOver = (adminClaimUntilMs_ == 0 || now >= adminClaimUntilMs_);
            if (gotClaimed) {
                admUi_ = ADM_ADMIN_CLAIMED;
                admSummaryUntil_ = now + 10000;
            } else if (windowOver) {
                admUi_ = ADM_WINDOW_CLOSED;
                admSummaryUntil_ = now + 10000;
            }
        }
        // Auto-close splashes after 10 s.
        if ((admUi_ == ADM_WINDOW_CLOSED || admUi_ == ADM_ADMIN_CLAIMED) &&
            admSummaryUntil_ != 0 && now >= admSummaryUntil_) {
            admExit();
        }
        // Auto-close show-admin after 6 s.
        if (admUi_ == ADM_SHOW_ADMIN && admSummaryUntil_ != 0 &&
            now >= admSummaryUntil_) {
            admExit();
        }
    }

    if (admUi_ != ADM_OFF) drawAdminSlide(display, x, y);
    else                    drawGymStatus(display, x, y);
}

#endif // HAS_SCREEN

#endif // !MESHTASTIC_EXCLUDE_GAUNTLET
