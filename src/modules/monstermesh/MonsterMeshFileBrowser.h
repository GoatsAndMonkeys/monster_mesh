#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>
#include "variant.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"

#define FB_MAX_ENTRIES 64
#define FB_MAX_PATH 256
#define FB_MAX_NAME 128
#define FB_VISIBLE_ROWS 14
#define FB_ROW_HEIGHT 20
#define FB_LIST_Y 34
#define FB_FOOTER_Y 222

class MonsterMeshFileBrowser {
public:
    struct Entry {
        char name[FB_MAX_NAME];
        bool isDir;
        bool hasSave;  // true if a .sav file exists alongside this ROM
    };

    void open(const char *dir = "/", bool showEject = false) {
        strncpy(dir_, dir, sizeof(dir_) - 1);
        dir_[sizeof(dir_) - 1] = '\0';
        showEject_ = showEject;
        cursor_ = 0;
        scroll_ = 0;
        selected_ = false;
        ejected_ = false;
        confirmEject_ = false;
        selectedPath_[0] = '\0';
        scan();
        dirty_ = true;
    }

    // Returns true when a ROM file has been selected OR eject confirmed
    bool handleKey(uint8_t key) {
        switch (key) {
            case 'w': case 'W':
                confirmEject_ = false;
                if (cursor_ > 0) { cursor_--; dirty_ = true; }
                if (cursor_ < scroll_) scroll_ = cursor_;
                break;
            case 's': case 'S':
                confirmEject_ = false;
                if (cursor_ < count_ - 1) { cursor_++; dirty_ = true; }
                if (cursor_ >= scroll_ + FB_VISIBLE_ROWS)
                    scroll_ = cursor_ - FB_VISIBLE_ROWS + 1;
                break;
            case 'k': case 'K': case '\r': case '\n':
                return selectCurrent();
            case 'l': case 'L': case 'a': case 'A': case 0x08:
                confirmEject_ = false;
                goUp();
                break;
            case 'd': case 'D':
                confirmEject_ = false;
                if (count_ > 0 && entries_[cursor_].isDir) selectCurrent();
                break;
        }
        return false;
    }

    bool isSelected()  const { return selected_; }
    bool isEjected()   const { return ejected_; }
    bool isConfirmingEject() const { return confirmEject_; }
    const char *selectedPath() const { return selectedPath_; }
    bool isDirty()     const { return dirty_; }
    void clearDirty()        { dirty_ = false; }
    void markDirty()         { dirty_ = true; }

    int count()        const { return count_; }
    int cursor()       const { return cursor_; }
    int scroll()       const { return scroll_; }
    const Entry *entries() const { return entries_; }
    const char *currentDir() const { return dir_; }

private:
    char dir_[FB_MAX_PATH] = "/";
    char selectedPath_[FB_MAX_PATH] = {};
    Entry entries_[FB_MAX_ENTRIES];
    int count_  = 0;
    int cursor_ = 0;
    int scroll_ = 0;
    bool selected_ = false;
    bool dirty_    = true;
    bool showEject_    = false;
    bool ejected_      = false;
    bool confirmEject_ = false;

    static constexpr const char *EJECT_NAME = "[Eject Cart]";

    static bool isGBFile(const char *name) {
        size_t len = strlen(name);
        if (len >= 3 && strcasecmp(name + len - 3, ".gb") == 0) return true;
        if (len >= 4 && strcasecmp(name + len - 4, ".gbc") == 0) return true;
        return false;
    }

    // Build the .sav path for a ROM file in dir_
    bool checkSaveExists(const char *fname) {
        if (!isGBFile(fname)) return false;
        char savPath[FB_MAX_PATH];
        // Strip extension, append .sav
        char base[FB_MAX_NAME];
        strncpy(base, fname, FB_MAX_NAME - 1);
        base[FB_MAX_NAME - 1] = '\0';
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        if (strcmp(dir_, "/") == 0)
            snprintf(savPath, sizeof(savPath), "/%s.sav", base);
        else
            snprintf(savPath, sizeof(savPath), "%s/%s.sav", dir_, base);
        return SD.exists(savPath);
    }

    void scan() {
        count_ = 0;

        // ".." entry if not at SD root
        if (strcmp(dir_, "/") != 0) {
            strncpy(entries_[count_].name, "..", sizeof(entries_[0].name) - 1);
            entries_[count_].isDir = true;
            entries_[count_].hasSave = false;
            count_++;
        }

        // "[Eject Cart]" entry if a ROM is currently loaded
        if (showEject_) {
            strncpy(entries_[count_].name, EJECT_NAME, sizeof(entries_[0].name) - 1);
            entries_[count_].name[sizeof(entries_[0].name) - 1] = '\0';
            entries_[count_].isDir = false;
            entries_[count_].hasSave = false;
            count_++;
        }

        // Serialize SD access against radio/TFT — the shared SPI bus needs
        // exclusive ownership across the full reinit + directory walk.
        concurrency::LockGuard _g(spiLock);

        // Re-init SD before scanning — SPI bus state gets corrupted by TFT/LoRa
        SD.end();
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
        if (!SD.begin(SDCARD_CS, SPI, 4000000U)) {
            log_w("[FileBrowser] SD reinit failed");
            dirty_ = true;
            return;
        }

        // Use Arduino SD library API for directory listing
        // (POSIX opendir/readdir doesn't work with Arduino SD VFS)
        File root = SD.open(dir_);
        if (!root || !root.isDirectory()) {
            log_w("[FileBrowser] SD.open('%s') failed", dir_);
            if (root) root.close();
            dirty_ = true;
            return;
        }

        // First pass: collect directories
        File entry = root.openNextFile();
        while (entry && count_ < FB_MAX_ENTRIES) {
            const char *name = entry.name();
            // SD library returns full path — extract just the filename
            const char *slash = strrchr(name, '/');
            const char *fname = slash ? slash + 1 : name;
            if (fname[0] != '.' && entry.isDirectory()) {
                strncpy(entries_[count_].name, fname, FB_MAX_NAME - 1);
                entries_[count_].name[FB_MAX_NAME - 1] = '\0';
                entries_[count_].isDir = true;
                entries_[count_].hasSave = false;
                count_++;
            }
            entry.close();
            entry = root.openNextFile();
        }
        root.close();

        // Second pass: GB/GBC files first
        root = SD.open(dir_);
        if (!root) { dirty_ = true; return; }
        entry = root.openNextFile();
        while (entry && count_ < FB_MAX_ENTRIES) {
            const char *name = entry.name();
            const char *slash = strrchr(name, '/');
            const char *fname = slash ? slash + 1 : name;
            if (fname[0] != '.' && !entry.isDirectory() && isGBFile(fname)) {
                strncpy(entries_[count_].name, fname, FB_MAX_NAME - 1);
                entries_[count_].name[FB_MAX_NAME - 1] = '\0';
                entries_[count_].isDir = false;
                entries_[count_].hasSave = checkSaveExists(fname);
                count_++;
            }
            entry.close();
            entry = root.openNextFile();
        }
        root.close();

        // Third pass: all other files
        root = SD.open(dir_);
        if (!root) { dirty_ = true; return; }
        entry = root.openNextFile();
        while (entry && count_ < FB_MAX_ENTRIES) {
            const char *name = entry.name();
            const char *slash = strrchr(name, '/');
            const char *fname = slash ? slash + 1 : name;
            if (fname[0] != '.' && !entry.isDirectory() && !isGBFile(fname)) {
                strncpy(entries_[count_].name, fname, FB_MAX_NAME - 1);
                entries_[count_].name[FB_MAX_NAME - 1] = '\0';
                entries_[count_].isDir = false;
                entries_[count_].hasSave = false;
                count_++;
            }
            entry.close();
            entry = root.openNextFile();
        }
        root.close();
        dirty_ = true;
    }

    bool selectCurrent() {
        if (count_ == 0) return false;
        if (strcmp(entries_[cursor_].name, "..") == 0) {
            goUp();
            return false;
        }
        // [Eject Cart]: requires double-K confirmation
        if (strcmp(entries_[cursor_].name, EJECT_NAME) == 0) {
            if (!confirmEject_) {
                confirmEject_ = true;
                dirty_ = true;
                return false;  // first K — show confirmation prompt
            }
            // Second K — confirmed
            ejected_ = true;
            return true;
        }
        confirmEject_ = false;
        if (entries_[cursor_].isDir) {
            char newDir[FB_MAX_PATH];
            // Avoid double slash when at root "/"
            if (strcmp(dir_, "/") == 0)
                snprintf(newDir, sizeof(newDir), "/%s", entries_[cursor_].name);
            else
                snprintf(newDir, sizeof(newDir), "%s/%s", dir_, entries_[cursor_].name);
            strncpy(dir_, newDir, sizeof(dir_) - 1);
            cursor_ = 0;
            scroll_ = 0;
            scan();
            return false;
        }
        // ROM file selected
        if (strcmp(dir_, "/") == 0)
            snprintf(selectedPath_, sizeof(selectedPath_), "/%s", entries_[cursor_].name);
        else
            snprintf(selectedPath_, sizeof(selectedPath_), "%s/%s",
                     dir_, entries_[cursor_].name);
        selected_ = true;
        return true;
    }

    void goUp() {
        if (strcmp(dir_, "/") == 0) return;
        char *slash = strrchr(dir_, '/');
        if (slash && slash == dir_) {
            dir_[1] = '\0';  // go to "/"
        } else if (slash) {
            *slash = '\0';
        }
        cursor_ = 0;
        scroll_ = 0;
        scan();
    }
};
