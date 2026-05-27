// SPDX-License-Identifier: MIT
// See GauntletBBS.h.

#include "GauntletBBS.h"
#include "GauntletBattle.h"
#include "GauntletModule.h"
#include "FSCommon.h"
#include "gps/RTC.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static constexpr const char *GAUNTLET_LOG_DIR     = "/monstermesh/gauntlet_log";
static constexpr const char *GAUNTLET_LOG_RECORDS = "/monstermesh/gauntlet_log/records.jsonl";
static constexpr const char *GAUNTLET_LOG_MSGS    = "/monstermesh/gauntlet_log/messages.jsonl";

// ── JSONL append (with byte-cap rotation) ────────────────────────────────────

static void appendJsonl(const char *path, const char *json)
{
#ifdef FSCom
    if (!path || !json) return;

    FSCom.mkdir("/monstermesh");
    FSCom.mkdir(GAUNTLET_LOG_DIR);

    // If file is over cap, truncate (we're not running a full ring buffer
    // here; MQTT carries the durable feed for off-device aggregation).
    auto stat = FSCom.open(path, FILE_O_READ);
    bool oversized = false;
    if (stat) {
        if (stat.size() > GAUNTLET_LOG_MAX_BYTES) oversized = true;
        stat.close();
    }
    if (oversized) FSCom.remove(path);

    // FSCommon.h only exposes "r"/"w" — emulate append by reading existing
    // content, then write-truncate with old + new data. Files are capped at
    // GAUNTLET_LOG_MAX_BYTES so this stays cheap.
    char *existing = nullptr;
    size_t existingLen = 0;
    auto rd = FSCom.open(path, FILE_O_READ);
    if (rd) {
        existingLen = rd.size();
        if (existingLen > 0 && existingLen < GAUNTLET_LOG_MAX_BYTES) {
            existing = (char *)malloc(existingLen);
            if (existing) rd.read((uint8_t *)existing, existingLen);
            else          existingLen = 0;
        }
        rd.close();
    }

    auto f = FSCom.open(path, FILE_O_WRITE);
    if (!f) {
        if (existing) free(existing);
        return;
    }
    if (existing && existingLen > 0) f.write((const uint8_t *)existing, existingLen);
    if (existing) free(existing);
    f.print(json);
    f.print("\n");
    f.flush();
    f.close();
#else
    (void)path; (void)json;
#endif
}

static size_t escape(char *out, size_t outLen, const char *in)
{
    if (!out || outLen == 0) return 0;
    size_t pos = 0;
    for (const char *p = in ? in : ""; *p && pos + 2 < outLen; p++) {
        char c = *p;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= outLen) break;
            out[pos++] = '\\';
            out[pos++] = c;
        } else if (c >= 0x20) {
            out[pos++] = c;
        }
    }
    out[pos] = '\0';
    return pos;
}

// ── Public API: log writers ──────────────────────────────────────────────────

void gauntletBBSLogLeader(const GauntletState &s,
                           uint32_t nodeNum, const char *name,
                           const char *partyCsv)
{
    char gymEsc[24], nameEsc[24], badgeEsc[24], partyEsc[80];
    escape(gymEsc,   sizeof(gymEsc),   s.gymName);
    escape(nameEsc,  sizeof(nameEsc),  name);
    escape(badgeEsc, sizeof(badgeEsc), s.gymBadge);
    escape(partyEsc, sizeof(partyEsc), partyCsv);

    char json[260];
    snprintf(json, sizeof(json),
             "{\"type\":\"leader\",\"ts\":%lu,\"gym\":\"%s\",\"badge\":\"%s\","
             "\"node\":%lu,\"name\":\"%s\",\"party\":\"%s\"}",
             (unsigned long)getTime(), gymEsc, badgeEsc,
             (unsigned long)nodeNum, nameEsc, partyEsc);
    appendJsonl(GAUNTLET_LOG_RECORDS, json);
}

void gauntletBBSLogRoster(const GauntletState &s,
                           uint32_t nodeNum, const char *name,
                           uint8_t rank)
{
    char gymEsc[24], nameEsc[24];
    escape(gymEsc,  sizeof(gymEsc),  s.gymName);
    escape(nameEsc, sizeof(nameEsc), name);

    char json[200];
    snprintf(json, sizeof(json),
             "{\"type\":\"roster\",\"ts\":%lu,\"gym\":\"%s\","
             "\"node\":%lu,\"name\":\"%s\",\"rank\":%u}",
             (unsigned long)getTime(), gymEsc,
             (unsigned long)nodeNum, nameEsc, (unsigned)rank);
    appendJsonl(GAUNTLET_LOG_RECORDS, json);
}

void gauntletBBSLogMessage(uint32_t nodeNum, const char *name, const char *text)
{
    char nameEsc[24], textEsc[200];
    escape(nameEsc, sizeof(nameEsc), name);
    escape(textEsc, sizeof(textEsc), text);

    char json[260];
    snprintf(json, sizeof(json),
             "{\"type\":\"msg\",\"ts\":%lu,\"node\":%lu,"
             "\"name\":\"%s\",\"text\":\"%s\"}",
             (unsigned long)getTime(),
             (unsigned long)nodeNum, nameEsc, textEsc);
    appendJsonl(GAUNTLET_LOG_MSGS, json);
}

// ── Door-game forward-compat stubs ───────────────────────────────────────────
//
// These return short text suitable for a TinyBBS BBSModule reply. When
// TinyBBS lands, its game handler can route 'Y' to gauntletBBSStartChallenge
// and subsequent input lines to gauntletBBSHandleStep.

const char *gauntletBBSStartChallenge(uint32_t /*nodeNum*/, const char *shortName)
{
    (void)shortName;
    if (!gauntletModule) return "Gym offline.";
    static char reply[200];
    const GauntletState &s = gauntletModule->state();
    if (s.leader.nodeNum) {
        char party[40];
        gauntletFormatParty(s.leader.party, party, sizeof(party));
        snprintf(reply, sizeof(reply),
                 "%s [%s]\nLeader %s: %s\nEnter party (CSV):",
                 s.gymName, s.gymBadge, s.leader.name, party);
    } else {
        snprintf(reply, sizeof(reply),
                 "%s [%s] — no leader yet!\nEnter party to claim:",
                 s.gymName, s.gymBadge);
    }
    return reply;
}

const char *gauntletBBSHandleStep(uint32_t /*nodeNum*/, const char *text, bool *done)
{
    if (done) *done = true;
    (void)text;
    // Placeholder — full BBS-state machine wiring deferred until TinyBBS
    // integration. The DM-based flow in GauntletModule already serves the
    // primary use-case (challenges via direct DM from MonsterMesh Terminal).
    return "Door-game wiring pending — DM the gym directly with !gym challenge.";
}
