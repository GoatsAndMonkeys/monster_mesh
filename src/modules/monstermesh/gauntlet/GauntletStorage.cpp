// SPDX-License-Identifier: MIT
// See GauntletStorage.h.

#include "GauntletStorage.h"
#include "FSCommon.h"
#include "gps/RTC.h"
#include <string.h>

static constexpr const char *GAUNTLET_PATH = "/monstermesh/gauntlet.dat";

void gauntletInitDefaults(GauntletState &s,
                          const char *gymName, const char *badge,
                          uint32_t nodeNum)
{
    memset(&s, 0, sizeof(s));
    s.magic   = GAUNTLET_MAGIC;
    s.version = GAUNTLET_VERSION;
    s.nodeNum = nodeNum;
    if (gymName) strncpy(s.gymName,  gymName, GAUNTLET_NAME_MAX  - 1);
    if (badge)   strncpy(s.gymBadge, badge,   GAUNTLET_BADGE_MAX - 1);
    s.gymPresetIdx = 0xFF;        // no preset by default
    s.lastUpdate = (uint32_t)getTime();
}

bool gauntletLoad(GauntletState &s,
                  const char *defaultName, const char *defaultBadge,
                  uint32_t nodeNum)
{
#ifdef FSCom
    // GauntletState is ~7.5 KB — must NOT allocate a copy on the stack
    // (Meshtastic loopTask has an 8 KB stack). Read directly into the
    // caller-supplied reference, validate, re-init on failure.
    auto f = FSCom.open(GAUNTLET_PATH, FILE_O_READ);
    if (!f) {
        gauntletInitDefaults(s, defaultName, defaultBadge, nodeNum);
        return false;
    }

    size_t n = f.read((uint8_t *)&s, sizeof(s));
    f.close();

    bool valid = (n == sizeof(s) &&
                  s.magic   == GAUNTLET_MAGIC &&
                  s.version == GAUNTLET_VERSION &&
                  s.nodeNum == nodeNum);
    if (!valid) {
        // A node-number mismatch wipes the ladder (save belongs to another
        // gym); same for any corruption.
        gauntletInitDefaults(s, defaultName, defaultBadge, nodeNum);
        return false;
    }
    return true;
#else
    gauntletInitDefaults(s, defaultName, defaultBadge, nodeNum);
    return false;
#endif
}

bool gauntletSave(const GauntletState &s)
{
#ifdef FSCom
    FSCom.mkdir("/monstermesh");
    if (FSCom.exists(GAUNTLET_PATH)) FSCom.remove(GAUNTLET_PATH);
    auto f = FSCom.open(GAUNTLET_PATH, FILE_O_WRITE);
    if (!f) return false;

    size_t n = f.write((const uint8_t *)&s, sizeof(s));
    f.flush();
    f.close();
    return n == sizeof(s);
#else
    return false;
#endif
}
