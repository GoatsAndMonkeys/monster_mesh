// SPDX-License-Identifier: MIT
// See GauntletProfile.h.

#include "GauntletProfile.h"
#include "FSCommon.h"
#include "gps/RTC.h"
#include <stdio.h>
#include <string.h>

static constexpr const char *PROFILE_DIR = "/monstermesh/gauntlet_profiles";

static void profilePath(uint32_t nodeNum, char *buf, size_t bufLen)
{
    snprintf(buf, bufLen, "%s/%08lx.bin", PROFILE_DIR, (unsigned long)nodeNum);
}

bool gauntletProfileLoad(uint32_t nodeNum, GauntletProfile &p)
{
    memset(&p, 0, sizeof(p));
#ifdef FSCom
    char path[64];
    profilePath(nodeNum, path, sizeof(path));
    auto f = FSCom.open(path, FILE_O_READ);
    if (!f) return false;

    GauntletProfile tmp;
    size_t n = f.read((uint8_t *)&tmp, sizeof(tmp));
    f.close();
    if (n != sizeof(tmp))                            return false;
    if (tmp.magic   != GAUNTLET_PROFILE_MAGIC)       return false;
    if (tmp.version != GAUNTLET_PROFILE_VERSION)     return false;

    p = tmp;
    return true;
#else
    (void)nodeNum;
    return false;
#endif
}

bool gauntletProfileSave(const GauntletProfile &p)
{
#ifdef FSCom
    FSCom.mkdir("/monstermesh");
    FSCom.mkdir(PROFILE_DIR);

    char path[64];
    profilePath(p.nodeNum, path, sizeof(path));
    if (FSCom.exists(path)) FSCom.remove(path);
    auto f = FSCom.open(path, FILE_O_WRITE);
    if (!f) return false;
    size_t n = f.write((const uint8_t *)&p, sizeof(p));
    f.flush();
    f.close();
    return n == sizeof(p);
#else
    (void)p;
    return false;
#endif
}

void gauntletProfileUpdate(uint32_t nodeNum, const char *name,
                            const char *partyCsv,
                            bool challengeStarted,
                            bool ranked, uint8_t rankAchieved,
                            bool becameLeader, bool reachedLeader)
{
    if (!nodeNum) return;

    GauntletProfile p;
    if (!gauntletProfileLoad(nodeNum, p)) {
        memset(&p, 0, sizeof(p));
        p.magic    = GAUNTLET_PROFILE_MAGIC;
        p.version  = GAUNTLET_PROFILE_VERSION;
        p.nodeNum  = nodeNum;
        p.bestRank = 0;
    } else {
        // Bump magic/version in case loader returned a defaulted struct
        p.magic    = GAUNTLET_PROFILE_MAGIC;
        p.version  = GAUNTLET_PROFILE_VERSION;
    }

    if (name && *name) {
        strncpy(p.name, name, GAUNTLET_NAME_MAX - 1);
        p.name[GAUNTLET_NAME_MAX - 1] = '\0';
    }
    if (partyCsv && *partyCsv) {
        strncpy(p.lastParty, partyCsv, sizeof(p.lastParty) - 1);
        p.lastParty[sizeof(p.lastParty) - 1] = '\0';
    }

    if (challengeStarted)               p.totalChallenges++;
    if (becameLeader)                   { p.gymTitles++;   p.totalWins++; }
    else if (ranked)                    p.totalLosses++;

    if (reachedLeader)                  p.reachedLeader = 1;

    // bestRank tracks the *lowest* (hardest) rank achieved.
    // A becameLeader event is conceptually rank 0 (better than 1).
    if (becameLeader) {
        p.bestRank = 1;  // store as 1 (cap), since 0 means "no rank yet"
    } else if (ranked && rankAchieved > 0) {
        if (p.bestRank == 0 || rankAchieved < p.bestRank)
            p.bestRank = rankAchieved;
    }

    p.lastSeen = (uint32_t)getTime();
    gauntletProfileSave(p);
}
