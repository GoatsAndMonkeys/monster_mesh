// ── SaveWatcher — SAV file watcher (inotify on Linux, poll on macOS) ──────────

#include "SaveWatcher.h"
#include "../shared/DaycareSavPatcher.h"
#include "../battle/CrossGenSavReader.h"   // Gen 2/3 .sav detection + parsing
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <sys/inotify.h>
#endif

static constexpr size_t GEN1_SAV_SIZE = 32768;

SaveWatcher::SaveWatcher() {
    memset(rawSav_, 0, sizeof(rawSav_));
}

SaveWatcher::~SaveWatcher() {
    stop();
}

bool SaveWatcher::start(const char *saveDirs) {
    stop();

    // Parse the ':'-separated directory list. Newest save across all of them
    // wins, so a GB / GBC / GBA cart's save feeds in from its own roms dir.
    watchDirs_.clear();
    {
        std::string s(saveDirs ? saveDirs : "");
        size_t start = 0;
        while (start <= s.size()) {
            size_t colon = s.find(':', start);
            std::string d = (colon == std::string::npos)
                                ? s.substr(start)
                                : s.substr(start, colon - start);
            if (!d.empty()) watchDirs_.push_back(d);
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
    }
    if (watchDirs_.empty()) return false;

#ifdef __linux__
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        LOG_WARN("SaveWatcher: inotify_init1 failed: %s", strerror(errno));
        return false;
    }
    for (const auto &d : watchDirs_) {
        int wd = inotify_add_watch(inotifyFd_, d.c_str(),
                                    IN_CLOSE_WRITE | IN_MOVED_TO);
        if (wd < 0) {
            // Non-fatal: a missing dir (e.g. no GBA games installed) must not
            // stop us watching the others.
            LOG_WARN("SaveWatcher: inotify_add_watch(%s) failed: %s",
                     d.c_str(), strerror(errno));
        } else {
            watchFds_.push_back(wd);
        }
    }
    if (watchFds_.empty()) {
        ::close(inotifyFd_);
        inotifyFd_ = -1;
        return false;
    }
    LOG_INFO("SaveWatcher: watching %zu dir(s) (inotify)", watchDirs_.size());
#else
    LOG_INFO("SaveWatcher: watching %zu dir(s) (poll every %ums)",
             watchDirs_.size(), POLL_INTERVAL_MS);
#endif

    lastPollMs_ = millis();
    scanAndLoad();
    return true;
}

void SaveWatcher::stop() {
#ifdef __linux__
    for (int wd : watchFds_) {
        if (wd >= 0 && inotifyFd_ >= 0) inotify_rm_watch(inotifyFd_, wd);
    }
    watchFds_.clear();
    if (inotifyFd_ >= 0) {
        ::close(inotifyFd_);
        inotifyFd_ = -1;
    }
#endif
    hasParty_     = false;
    hasRawSav_    = false;
    hasWireParty_ = false;
    currentSav_[0] = '\0';
    currentMtime_  = 0;
}

bool SaveWatcher::poll() {
#ifdef __linux__
    if (inotifyFd_ < 0) return false;

    char evBuf[sizeof(struct inotify_event) + NAME_MAX + 1];
    bool changed = false;

    while (true) {
        ssize_t n = ::read(inotifyFd_, evBuf, sizeof(evBuf));
        if (n <= 0) break;

        const struct inotify_event *ev = (const struct inotify_event *)evBuf;
        if (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
            // Trigger a rescan on ANY file write/move in the dir, not just
            // names ending in ".sav".  RetroPie's libretro cores save to
            // "<rom>.srm"; users commonly symlink that to "<rom>.sav" so the
            // daemon can read it, but the inotify event for a write through
            // the symlink can carry the ".srm" name.  scanAndLoad() still
            // only *loads* valid 32 KB .sav files, so over-triggering here is
            // harmless and a cheap directory restat.
            if (ev->len > 0) changed = true;
        }
    }

    if (changed) return scanAndLoad();
    return false;

#else
    // macOS: poll every POLL_INTERVAL_MS
    uint32_t now = millis();
    if (now - lastPollMs_ < POLL_INTERVAL_MS) return false;
    lastPollMs_ = now;
    return scanAndLoad();
#endif
}

// File-local flag so the missing-watch-dir warning only fires once per
// state change instead of every 2-second poll tick on macOS.
static bool s_warnedMissingDir = false;

bool SaveWatcher::scanAndLoad() {
    char bestPath[256] = {};
    time_t bestMtime = 0;
    bool anyDirOpened = false;

    // Newest .sav/.srm across ALL watched directories wins — so switching
    // carts (play Emerald after Red) automatically swaps in that team.
    for (const auto &wd : watchDirs_) {
        DIR *dir = opendir(wd.c_str());
        if (!dir) continue;
        anyDirOpened = true;

        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            const char *name = ent->d_name;
            size_t nameLen = strlen(name);
            if (nameLen < 4) continue;
            // Accept both .sav and .srm — RetroPie's libretro cores (gambatte,
            // mGBA) write "<rom>.srm". Loaders validate content, so the
            // extension is only a cheap filter.
            const char *ext = name + nameLen - 4;
            if (strcmp(ext, ".sav") != 0 && strcmp(ext, ".srm") != 0) continue;

            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", wd.c_str(), name);

            struct stat st;
            if (stat(fullPath, &st) != 0) continue;

            if (st.st_mtime > bestMtime) {
                bestMtime = st.st_mtime;
                strncpy(bestPath, fullPath, sizeof(bestPath) - 1);
                bestPath[sizeof(bestPath) - 1] = '\0';
            }
        }
        closedir(dir);
    }

    if (!anyDirOpened) {
        if (!s_warnedMissingDir) {
            LOG_WARN("SaveWatcher: none of the %zu watch dir(s) are openable",
                     watchDirs_.size());
            s_warnedMissingDir = true;
        }
        return false;
    }
    s_warnedMissingDir = false;  // reset on successful open

    if (bestPath[0] == '\0') return false;
    // Reload when the newest save is a different file OR the same file whose
    // contents changed (mtime moved). Covers both Gen-1 (hasParty_) and Gen
    // 2/3 (hasWireParty_) currently-loaded states.
    if (strcmp(bestPath, currentSav_) == 0 &&
        (hasParty_ || hasWireParty_) && bestMtime == currentMtime_)
        return false;

    currentMtime_ = bestMtime;
    return loadSav(bestPath);
}

bool SaveWatcher::loadSav(const char *path) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        LOG_WARN("SaveWatcher: cannot open %s: %s", path, strerror(errno));
        return false;
    }

    // Big enough for a Gen-3 GBA save (128 KB) plus emulator RTC footers.
    // Static: keep the 128 KB off the stack (loadSav runs on the poll thread).
    static uint8_t buf[140 * 1024];
    ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);

    if (n < (ssize_t)GEN1_SAV_SIZE) {
        LOG_WARN("SaveWatcher: %s is %zd bytes, too small for any known save", path, n);
        return false;
    }

    // ── Cross-gen first: Gen 3 (128 KB, section signatures) or Gen 2 (32 KB,
    // GSC checksums). Strict validation, so a Gen-1 sav falls through. ──────
    {
        ParsedMon mons[6];
        uint8_t gen = 0;
        uint8_t wcount = crossGenReadSavParty(buf, (size_t)n, mons, gen);
        if (wcount > 0) {
            wireParty_ = Gen1BattleEngine::WireParty();
            wireParty_.count = wcount;
            for (uint8_t i = 0; i < wcount && i < 6; i++) {
                Gen1BattleEngine::WireMon &w = wireParty_.mons[i];
                w.species = mons[i].dex;
                w.level   = mons[i].level;
                w.maxHp   = mons[i].maxHp;
                w.atk     = mons[i].atk;
                w.def     = mons[i].def;
                w.spe     = mons[i].spe;
                w.spa     = mons[i].spa;
                w.spd     = mons[i].spd;
                for (int m = 0; m < 4; m++) w.moves[m] = mons[i].moves[m];
            }
            hasWireParty_ = true;
            savGen_       = gen;
            // Gen-1-only features (daycare checkIn, raw sav patching) don't
            // apply to a Gen 2/3 save.
            hasParty_  = false;
            hasRawSav_ = false;
            strncpy(currentSav_, path, sizeof(currentSav_) - 1);
            currentSav_[sizeof(currentSav_) - 1] = '\0';
            LOG_INFO("SaveWatcher: loaded %s as GEN %u save (%u Pokemon, wire party)",
                     path, (unsigned)gen, (unsigned)wcount);
            // Notify the daemon so a Gen 2/3 team can board the daycare
            // (national-dex species 152-386). Gen-1 uses cb_ (below).
            if (cbWire_) cbWire_(wireParty_, gen, path);
            return true;
        }
    }

    if (n != (ssize_t)GEN1_SAV_SIZE) {
        LOG_WARN("SaveWatcher: %s is %zd bytes — not a valid Gen 1/2/3 save", path, n);
        return false;
    }

    DaycarePartyInfo partyInfo[6];
    uint8_t count = DaycareSavPatcher::readParty(buf, partyInfo);
    if (count == 0) {
        LOG_WARN("SaveWatcher: %s has 0 party members", path);
        return false;
    }
    hasWireParty_ = false;   // Gen-1 save: PvP converts via gen1PartyToWireParty
    savGen_       = 1;

    memset(&party_, 0, sizeof(party_));
    party_.count = count;

    for (uint8_t i = 0; i < count && i < 6; i++) {
        party_.species[i] = dexToInternal[partyInfo[i].dexNum];
        const uint8_t *pkm = &buf[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE];
        memcpy(&party_.mons[i], pkm, sizeof(Gen1Pokemon));
        memcpy(party_.otNames[i],   &buf[SAV_OT_NAMES  + i * SAV_NAME_SIZE], SAV_NAME_SIZE);
        memcpy(party_.nicknames[i], &buf[SAV_NICKNAMES  + i * SAV_NAME_SIZE], SAV_NAME_SIZE);
    }
    party_.species[count] = 0xFF;

    memcpy(rawSav_, buf, GEN1_SAV_SIZE);
    hasRawSav_ = true;

    strncpy(currentSav_, path, sizeof(currentSav_) - 1);
    currentSav_[sizeof(currentSav_) - 1] = '\0';
    hasParty_ = true;

    LOG_INFO("SaveWatcher: loaded %s (%d Pokemon)", path, count);
    if (cb_) cb_(party_, path);
    return true;
}

bool SaveWatcher::getRawSav(uint8_t *out, size_t bufSize) const {
    if (!hasRawSav_ || bufSize < GEN1_SAV_SIZE) return false;
    memcpy(out, rawSav_, GEN1_SAV_SIZE);
    return true;
}

bool SaveWatcher::readPartyFromSav(const char *path, Gen1Party &out) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;

    uint8_t buf[GEN1_SAV_SIZE];
    ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);

    if (n != (ssize_t)GEN1_SAV_SIZE) return false;

    DaycarePartyInfo partyInfo[6];
    uint8_t count = DaycareSavPatcher::readParty(buf, partyInfo);
    if (count == 0) return false;

    memset(&out, 0, sizeof(out));
    out.count = count;
    for (uint8_t i = 0; i < count && i < 6; i++) {
        out.species[i] = dexToInternal[partyInfo[i].dexNum];
        const uint8_t *pkm = &buf[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE];
        memcpy(&out.mons[i], pkm, sizeof(Gen1Pokemon));
        memcpy(out.otNames[i],   &buf[SAV_OT_NAMES  + i * SAV_NAME_SIZE], SAV_NAME_SIZE);
        memcpy(out.nicknames[i], &buf[SAV_NICKNAMES  + i * SAV_NAME_SIZE], SAV_NAME_SIZE);
    }
    out.species[count] = 0xFF;
    return true;
}
