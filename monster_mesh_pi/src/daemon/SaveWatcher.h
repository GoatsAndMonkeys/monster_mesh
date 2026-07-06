#pragma once
#include "../shared/platform.h"
#include "../shared/PokemonData.h"
#include "../battle/WirePartyCodec.h"   // WireParty (neutral cross-gen party)
#include <string>
#include <vector>
#include <functional>
#include <ctime>

class SaveWatcher {
public:
    using PartyCallback = std::function<void(const Gen1Party &party, const char *savPath)>;

    SaveWatcher();
    ~SaveWatcher();

    // Start watching one or more save directories (':'-separated list).
    // Newest .sav/.srm across all of them wins. Returns true on success.
    bool start(const char *saveDirs);
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

    // Cross-gen: set when the newest save parsed as Gen 2 (GSC) or Gen 3
    // (RSE/FRLG) via CrossGenSavReader. The party is already in neutral
    // WireParty form (final stats computed by the reader). Gen-1 saves keep
    // the Gen1Party path above (hasWireParty() == false).
    bool hasWireParty() const { return hasWireParty_; }
    const Gen1BattleEngine::WireParty &wireParty() const { return wireParty_; }
    uint8_t savGen() const { return savGen_; }   // 1, 2 or 3

    // Read the raw 32KB SAV buffer (for passing to daycare checkIn)
    // Returns true if available. out must be at least 32768 bytes.
    bool getRawSav(uint8_t *out, size_t bufSize) const;

private:
    int inotifyFd_    = -1;
    std::vector<int> watchFds_;     // one inotify watch per directory
    uint32_t lastPollMs_ = 0;       // macOS poll-based fallback
    static constexpr uint32_t POLL_INTERVAL_MS = 2000;
    std::vector<std::string> watchDirs_;
    char currentSav_[256] = {};
    time_t currentMtime_ = 0;       // mtime of the currently-loaded save, so a
                                    // content change to the *same* path forces
                                    // a reload (party reorder, in-game edits).
    bool hasParty_ = false;
    Gen1Party party_ = {};
    uint8_t rawSav_[32768] = {};
    bool hasRawSav_ = false;
    Gen1BattleEngine::WireParty wireParty_ = {};
    bool    hasWireParty_ = false;
    uint8_t savGen_       = 1;
    PartyCallback cb_;

    bool scanAndLoad();
    bool loadSav(const char *path);
};
