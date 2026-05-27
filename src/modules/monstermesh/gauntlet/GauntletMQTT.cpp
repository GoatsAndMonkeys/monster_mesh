// SPDX-License-Identifier: MIT
// See GauntletMQTT.h.

#include "GauntletMQTT.h"
#include "GauntletBattle.h"   // gauntletFormatParty
#include "configuration.h"

#if HAS_NETWORKING
#include "mqtt/MQTT.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#endif

namespace {

#if HAS_NETWORKING
constexpr const char *TOPIC_PREFIX = "msh-gym";
constexpr size_t      JSON_BUF_MAX = 480;

// Small JSON-string-escaper. Only escapes the characters that matter for our
// short, ASCII-only payloads (gym names, badge names, mon names). Fancy
// Unicode escaping not needed.
size_t jsonEscape(char *out, size_t outLen, const char *in)
{
    if (!out || outLen == 0) return 0;
    size_t pos = 0;
    for (const char *p = in; *p && pos + 2 < outLen; p++) {
        char c = *p;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= outLen) break;
            out[pos++] = '\\';
            out[pos++] = c;
        } else if (c >= 0x20) {
            out[pos++] = c;
        } // else drop control chars
    }
    out[pos] = '\0';
    return pos;
}

// Build the "msh-gym/<hex>/state" or ".../event" topic string.
void buildTopic(char *out, size_t outLen, uint32_t nodeNum, const char *kind)
{
    snprintf(out, outLen, "%s/%08lx/%s", TOPIC_PREFIX,
             (unsigned long)nodeNum, kind);
}

// Append "k":"v", to the buffer at *pos, escaping v.
int appendStr(char *buf, size_t cap, int pos, const char *key, const char *val)
{
    char esc[64];
    jsonEscape(esc, sizeof(esc), val ? val : "");
    return pos + snprintf(buf + pos, cap - pos, "\"%s\":\"%s\",", key, esc);
}

int appendUint(char *buf, size_t cap, int pos, const char *key, uint32_t val)
{
    return pos + snprintf(buf + pos, cap - pos, "\"%s\":%lu,",
                          key, (unsigned long)val);
}
#endif // HAS_NETWORKING

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

bool gauntletMQTTPublishState(const GauntletState &s)
{
#if HAS_NETWORKING
    if (!mqtt || !mqtt->isConnectedDirectly()) return false;

    char topic[64];
    buildTopic(topic, sizeof(topic), s.nodeNum, "state");

    char party[64];
    party[0] = '\0';
    if (s.leader.nodeNum) gauntletFormatParty(s.leader.party, party, sizeof(party));

    char buf[JSON_BUF_MAX];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{");
    pos  = appendStr (buf, sizeof(buf), pos, "gym",     s.gymName);
    pos  = appendStr (buf, sizeof(buf), pos, "badge",   s.gymBadge);
    pos  = appendUint(buf, sizeof(buf), pos, "node",    s.nodeNum);
    pos  = appendStr (buf, sizeof(buf), pos, "leader",  s.leader.nodeNum ? s.leader.name : "");
    pos  = appendStr (buf, sizeof(buf), pos, "party",   party);
    pos  = appendUint(buf, sizeof(buf), pos, "since",   s.leader.timestamp);
    pos  = appendUint(buf, sizeof(buf), pos, "roster",  s.rosterSize);
    pos  = appendUint(buf, sizeof(buf), pos, "prev",    s.prevLeaderCount);
    pos  = appendUint(buf, sizeof(buf), pos, "challenges", s.totalChallenges);
    pos  = appendUint(buf, sizeof(buf), pos, "battles",    s.totalBattles);
    pos  = appendUint(buf, sizeof(buf), pos, "ts",      (uint32_t)getTime());

    // Replace trailing comma with closing brace
    if (pos > 1 && buf[pos - 1] == ',') pos--;
    if (pos < (int)sizeof(buf) - 1) {
        buf[pos++] = '}';
        buf[pos]   = '\0';
    }

    return mqtt->publish(topic, buf, /*retained=*/true);
#else
    (void)s;
    return false;
#endif
}

bool gauntletMQTTPublishEvent(const GauntletState &s,
                               const char *eventType,
                               const char *payloadJson)
{
#if HAS_NETWORKING
    if (!mqtt || !mqtt->isConnectedDirectly()) return false;
    if (!eventType || !*eventType) return false;

    char topic[64];
    buildTopic(topic, sizeof(topic), s.nodeNum, "event");

    char escType[32], escGym[32];
    jsonEscape(escType, sizeof(escType), eventType);
    jsonEscape(escGym,  sizeof(escGym),  s.gymName);

    char buf[JSON_BUF_MAX];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"%s\",\"gym\":\"%s\",\"ts\":%lu,\"data\":%s}",
             escType, escGym, (unsigned long)getTime(),
             (payloadJson && *payloadJson) ? payloadJson : "{}");

    return mqtt->publish(topic, buf, /*retained=*/false);
#else
    (void)s; (void)eventType; (void)payloadJson;
    return false;
#endif
}
