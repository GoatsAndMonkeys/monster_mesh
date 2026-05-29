#pragma once
#include "../shared/platform.h"
#include "../shared/PokemonData.h"
#include <string>
#include <functional>

class SaveWatcher {
public:
    using PartyCallback = std::function<void(const Gen1Party &party, const char *savPath)>;

    SaveWatcher();
    ~SaveWatcher();

    // Start watching the save directory. Returns true on success.
    bool start(const char *saveDir);
    void stop();

    void setPartyCallback(PartyCallback cb) { cb_ = cb; }

    // Poll for changes. Returns true if party was updated.
    bool poll();

    // Read party from a specific save file. Returns true on success.
    static bool readPartyFromSav(const char *path, Gen1Party &out);

    // The currently loaded save file path
    const char *currentSavPath() const { return currentSav_; }
    bool hasParty() const { return hasParty_; }
    const Gen1Party &party() const { return party_; }

    // Read the raw 32KB SAV buffer (for passing to daycare checkIn)
    // Returns true if available. out must be at least 32768 bytes.
    bool getRawSav(uint8_t *out, size_t bufSize) const;

private:
    int inotifyFd_    = -1;
    int watchFd_      = -1;
    uint32_t lastPollMs_ = 0;       // macOS poll-based fallback
    static constexpr uint32_t POLL_INTERVAL_MS = 2000;
    char watchDir_[256] = {};
    char currentSav_[256] = {};
    bool hasParty_ = false;
    Gen1Party party_ = {};
    uint8_t rawSav_[32768] = {};
    bool hasRawSav_ = false;
    PartyCallback cb_;

    bool scanAndLoad();
    bool loadSav(const char *path);
};
