// SPDX-License-Identifier: MIT
// See LordSave.h.

#include "LordSave.h"

#include "FSCommon.h"
#include "gps/RTC.h"

#include <string.h>

static constexpr const char *LORD_PATH = "/monstermesh/lord.dat";

void lordInitDefaults(LordSave &s)
{
    memset(&s, 0, sizeof(s));
    s.magic   = LORD_MAGIC;
    s.version = LORD_VERSION;
}

bool lordLoad(LordSave &s)
{
    lordInitDefaults(s);

#ifdef FSCom
    auto f = FSCom.open(LORD_PATH, FILE_O_READ);
    if (!f) return false;

    LordSave tmp;
    size_t n = f.read((uint8_t *)&tmp, sizeof(tmp));
    f.close();

    if (n != sizeof(tmp))             return false;
    if (tmp.magic   != LORD_MAGIC)    return false;
    if (tmp.version != LORD_VERSION)  return false;

    s = tmp;
    return true;
#else
    return false;
#endif
}

bool lordSave(const LordSave &s)
{
#ifdef FSCom
    // Ensure containing directory exists; FSCom implementations differ on
    // whether open("/a/b") auto-creates "/a". Daycare code avoids this by
    // writing flat filenames, but we keep a subdir for organisation.
    FSCom.mkdir("/monstermesh");

    if (FSCom.exists(LORD_PATH)) FSCom.remove(LORD_PATH);
    auto f = FSCom.open(LORD_PATH, FILE_O_WRITE);
    if (!f) return false;

    size_t n = f.write((const uint8_t *)&s, sizeof(s));
    f.flush();
    f.close();
    return n == sizeof(s);
#else
    return false;
#endif
}

void lordAppendNews(LordSave &s, uint8_t type, uint8_t arg1, uint16_t arg2)
{
    LordNewsEntry &e = s.news[s.newsHead];
    e.ts       = getTime();
    e.type     = type;
    e.arg1     = arg1;
    e.arg2     = arg2;
    e.reserved = 0;

    s.newsHead = (uint8_t)((s.newsHead + 1) % LORD_NEWS_CAP);
    if (s.newsCount < LORD_NEWS_CAP) s.newsCount++;
}
