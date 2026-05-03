// SPDX-License-Identifier: MIT
//
// MonsterMeshTerminal — minimal LVGL text terminal.
//
// First pass: command parser + scrolling output. Battle/SAV/network commands
// will land in subsequent iterations. The class owns its LVGL panel; the
// module just toggles open()/close() and forwards keypresses.

#pragma once

#include <Arduino.h>
#include "PokemonData.h"

// Forward-declare LVGL types so the header doesn't need lvgl.h.
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

class MonsterMeshTerminal {
  public:
    MonsterMeshTerminal() = default;

    // Build the LVGL hierarchy under `parent`. Idempotent — second call
    // unhides the existing panel.
    void open(lv_obj_t *parent);
    // Hide the panel (does not destroy LVGL objects).
    void close();

    bool isOpen() const { return open_; }

    // Forward a typed character. 0x0D / '\n' submits the line. 0x08 deletes.
    void onKey(uint8_t key);

    // LV_EVENT_READY (Enter pressed in the textarea, e.g. via virtual keyboard
    // or hardware keyboard) hands us the full line in one call.
    void onSubmit(const char *line);

    // Push the player's party (read from emulator WRAM by the module on
    // terminal entry). Decoded nicknames live in party_.nicknames in Gen1
    // charset; we ASCII-decode them in the `party` command.
    void setParty(const Gen1Party &p);
    bool hasParty() const { return partyLoaded_; }

  private:
    void println(const char *s);
    void prompt();
    void executeLine(const char *line);
    void clearOutput();
    void showParty();  // print party listing or "no party" hint

    lv_obj_t *panel_   = nullptr;
    lv_obj_t *output_  = nullptr;  // scrolling lv_obj container of labels
    lv_obj_t *input_   = nullptr;  // single-line lv_textarea
    bool      open_    = false;

    // Pending input buffer (we accumulate keys and execute on enter).
    char inbuf_[128] = {};
    uint8_t inlen_   = 0;

    Gen1Party party_ = {};
    bool partyLoaded_ = false;
};
