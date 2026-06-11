#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>
#include "Display.h"
#include "pins.h"

#define FB_MAX_ENTRIES 64
#define FB_MAX_PATH 256
#define FB_MAX_NAME 128
#define FB_VISIBLE_ROWS 14
#define FB_ROW_HEIGHT 16
#define FB_LIST_Y 20
#define FB_HEADER_H 18

class FileBrowser {
public:
    struct Entry {
        char name[FB_MAX_NAME];
        bool isDir;
    };

    void open(const char *dir = "/") {
        strncpy(dir_, dir, sizeof(dir_) - 1);
        dir_[sizeof(dir_) - 1] = '\0';
        cursor_ = 0;
        scroll_ = 0;
        selected_ = false;
        selectedPath_[0] = '\0';
        active_ = true;
        scan();
        dirty_ = true;
    }

    void close() { active_ = false; }
    bool isActive() const { return active_; }

    // Returns true when a file has been selected
    bool handleKey(uint8_t key) {
        if (!active_) return false;
        switch (key) {
            case 'w': case 'W':
                if (cursor_ > 0) { cursor_--; dirty_ = true; }
                if (cursor_ < scroll_) scroll_ = cursor_;
                break;
            case 's': case 'S':
                if (cursor_ < count_ - 1) { cursor_++; dirty_ = true; }
                if (cursor_ >= scroll_ + FB_VISIBLE_ROWS)
                    scroll_ = cursor_ - FB_VISIBLE_ROWS + 1;
                break;
            case 'k': case 'K': case '\r': case '\n':
                return selectCurrent();
            case 'l': case 'L': case 'a': case 'A':
                goUp();
                break;
            case 'd': case 'D':
                if (count_ > 0 && entries_[cursor_].isDir) selectCurrent();
                break;
        }
        return false;
    }

    bool isSelected()  const { return selected_; }
    const char *selectedPath() const { return selectedPath_; }
    bool isDirty()     const { return dirty_; }
    void clearDirty()        { dirty_ = false; }

    void render(TFT_eSPI &tft) {
        if (!dirty_) return;
        dirty_ = false;

        tft.fillScreen(TFT_BLACK);

        // Header
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.drawString("MonsterMesh ROM Browser", 4, 2);
        tft.setTextColor(0x528A, TFT_BLACK);
        tft.drawString(dir_, 4, 12);

        // File list
        int visible = min(FB_VISIBLE_ROWS, count_ - scroll_);
        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            int y = FB_LIST_Y + FB_HEADER_H + i * FB_ROW_HEIGHT;
            bool isCursor = (idx == cursor_);

            if (isCursor) {
                tft.fillRect(0, y, 320, FB_ROW_HEIGHT, 0x1082);  // dark blue highlight
            }

            if (entries_[idx].isDir) {
                tft.setTextColor(TFT_YELLOW, isCursor ? 0x1082 : TFT_BLACK);
                char buf[FB_MAX_NAME + 4];
                snprintf(buf, sizeof(buf), "[%s]", entries_[idx].name);
                tft.drawString(buf, 8, y + 2);
            } else {
                bool isGB = isGBFile(entries_[idx].name);
                uint16_t color = isGB ? TFT_GREEN : TFT_WHITE;
                tft.setTextColor(color, isCursor ? 0x1082 : TFT_BLACK);
                tft.drawString(entries_[idx].name, 8, y + 2);
            }
        }

        if (count_ == 0) {
            tft.setTextColor(0x528A, TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("(empty)", 160, 120);
            tft.setTextDatum(TL_DATUM);
        }

        // Footer
        tft.setTextColor(0x528A, TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("W/S:Nav  K:Open  L:Back  ALT:Exit", 4, 226);
    }

private:
    char dir_[FB_MAX_PATH] = "/";
    char selectedPath_[FB_MAX_PATH] = {};
    Entry entries_[FB_MAX_ENTRIES];
    int count_  = 0;
    int cursor_ = 0;
    int scroll_ = 0;
    bool selected_ = false;
    bool dirty_    = true;
    bool active_   = false;

    static bool isGBFile(const char *name) {
        size_t len = strlen(name);
        if (len >= 3 && strcasecmp(name + len - 3, ".gb") == 0) return true;
        if (len >= 4 && strcasecmp(name + len - 4, ".gbc") == 0) return true;
        return false;
    }

    void scan() {
        count_ = 0;

        // ".." entry if not at root
        if (strcmp(dir_, "/") != 0) {
            strncpy(entries_[count_].name, "..", sizeof(entries_[0].name) - 1);
            entries_[count_].isDir = true;
            count_++;
        }

        File root = SD.open(dir_);
        if (!root || !root.isDirectory()) {
            if (root) root.close();
            dirty_ = true;
            return;
        }

        // First pass: directories
        File entry = root.openNextFile();
        while (entry && count_ < FB_MAX_ENTRIES) {
            const char *name = entry.name();
            const char *slash = strrchr(name, '/');
            const char *fname = slash ? slash + 1 : name;
            if (fname[0] != '.' && entry.isDirectory()) {
                strncpy(entries_[count_].name, fname, FB_MAX_NAME - 1);
                entries_[count_].name[FB_MAX_NAME - 1] = '\0';
                entries_[count_].isDir = true;
                count_++;
            }
            entry.close();
            entry = root.openNextFile();
        }
        root.close();

        // Second pass: files
        root = SD.open(dir_);
        if (!root) { dirty_ = true; return; }
        entry = root.openNextFile();
        while (entry && count_ < FB_MAX_ENTRIES) {
            const char *name = entry.name();
            const char *slash = strrchr(name, '/');
            const char *fname = slash ? slash + 1 : name;
            if (fname[0] != '.' && !entry.isDirectory()) {
                strncpy(entries_[count_].name, fname, FB_MAX_NAME - 1);
                entries_[count_].name[FB_MAX_NAME - 1] = '\0';
                entries_[count_].isDir = false;
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
        if (entries_[cursor_].isDir) {
            char newDir[FB_MAX_PATH];
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
        // File selected
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
        if (slash && slash == dir_) dir_[1] = '\0';
        else if (slash) *slash = '\0';
        cursor_ = 0;
        scroll_ = 0;
        scan();
    }
};
