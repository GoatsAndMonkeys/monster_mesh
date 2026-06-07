// ── SaveWatcher — SAV file watcher (inotify on Linux, poll on macOS) ──────────

#include "SaveWatcher.h"
#include "../shared/DaycareSavPatcher.h"
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

bool SaveWatcher::start(const char *saveDir) {
    stop();

    strncpy(watchDir_, saveDir, sizeof(watchDir_) - 1);
    watchDir_[sizeof(watchDir_) - 1] = '\0';

#ifdef __linux__
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        LOG_WARN("SaveWatcher: inotify_init1 failed: %s", strerror(errno));
        return false;
    }

    watchFd_ = inotify_add_watch(inotifyFd_, saveDir,
                                  IN_CLOSE_WRITE | IN_MOVED_TO);
    if (watchFd_ < 0) {
        LOG_WARN("SaveWatcher: inotify_add_watch(%s) failed: %s",
                 saveDir, strerror(errno));
        ::close(inotifyFd_);
        inotifyFd_ = -1;
        return false;
    }
    LOG_INFO("SaveWatcher: watching %s (inotify)", saveDir);
#else
    LOG_INFO("SaveWatcher: watching %s (poll every %ums)", saveDir, POLL_INTERVAL_MS);
#endif

    lastPollMs_ = millis();
    scanAndLoad();
    return true;
}

void SaveWatcher::stop() {
#ifdef __linux__
    if (watchFd_ >= 0 && inotifyFd_ >= 0) {
        inotify_rm_watch(inotifyFd_, watchFd_);
        watchFd_ = -1;
    }
    if (inotifyFd_ >= 0) {
        ::close(inotifyFd_);
        inotifyFd_ = -1;
    }
#endif
    hasParty_  = false;
    hasRawSav_ = false;
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
    DIR *dir = opendir(watchDir_);
    if (!dir) {
        if (!s_warnedMissingDir) {
            LOG_WARN("SaveWatcher: cannot open dir %s: %s",
                     watchDir_, strerror(errno));
            s_warnedMissingDir = true;
        }
        return false;
    }
    s_warnedMissingDir = false;  // reset on successful open

    char bestPath[256] = {};
    time_t bestMtime = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char *name = ent->d_name;
        size_t nameLen = strlen(name);
        if (nameLen < 4) continue;
        if (strcmp(name + nameLen - 4, ".sav") != 0) continue;

        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", watchDir_, name);

        struct stat st;
        if (stat(fullPath, &st) != 0) continue;

        if (st.st_mtime > bestMtime) {
            bestMtime = st.st_mtime;
            strncpy(bestPath, fullPath, sizeof(bestPath) - 1);
        }
    }
    closedir(dir);

    if (bestPath[0] == '\0') return false;
    // Reload when the newest save is a different file OR the same file whose
    // contents changed (mtime moved).  The old code skipped any reload of the
    // same path, so editing the loaded save in-place — e.g. reordering the
    // party in the emulator — never propagated to the daemon.
    if (strcmp(bestPath, currentSav_) == 0 && hasParty_ && bestMtime == currentMtime_)
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

    uint8_t buf[GEN1_SAV_SIZE];
    ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);

    if (n != (ssize_t)GEN1_SAV_SIZE) {
        LOG_WARN("SaveWatcher: %s is %zd bytes, expected %zu", path, n, GEN1_SAV_SIZE);
        return false;
    }

    DaycarePartyInfo partyInfo[6];
    uint8_t count = DaycareSavPatcher::readParty(buf, partyInfo);
    if (count == 0) {
        LOG_WARN("SaveWatcher: %s has 0 party members", path);
        return false;
    }

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
