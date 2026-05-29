// SPDX-License-Identifier: MIT
// See ../shared/LordSave.h.

#include "../shared/LordSave.h"
#include <string.h>
#include <time.h>

void lordInitDefaults(LordSave &s)
{
    memset(&s, 0, sizeof(s));
    s.magic   = LORD_MAGIC;
    s.version = LORD_VERSION;
}

bool lordLoad(LordSave &s)
{
    lordInitDefaults(s);
    FILE *f = fopen(LORD_SAVE_PATH, "rb");
    if (!f) return false;
    LordSave tmp;
    size_t n = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    if (n != sizeof(tmp)) return false;
    if (tmp.magic != LORD_MAGIC) return false;
    if (tmp.version != LORD_VERSION) return false;
    s = tmp;
    return true;
}

bool lordSave(const LordSave &s)
{
    FILE *f = fopen(LORD_SAVE_PATH, "wb");
    if (!f) return false;
    size_t n = fwrite(&s, 1, sizeof(s), f);
    fclose(f);
    return n == sizeof(s);
}

void lordAppendNews(LordSave &s, uint8_t type, uint8_t arg1, uint16_t arg2)
{
    LordNewsEntry &e = s.news[s.newsHead % LORD_NEWS_CAP];
    e.ts       = (uint32_t)time(nullptr);
    e.type     = type;
    e.arg1     = arg1;
    e.arg2     = arg2;
    e.reserved = 0;

    s.newsHead = (s.newsHead + 1) % LORD_NEWS_CAP;
    if (s.newsCount < LORD_NEWS_CAP) s.newsCount++;
}
