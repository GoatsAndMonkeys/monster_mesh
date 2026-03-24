#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

// ── Game Boy button bit positions (matches Peanut-GB joypad byte layout) ──────
#define GB_BTN_A        (1 << 0)
#define GB_BTN_B        (1 << 1)
#define GB_BTN_SELECT   (1 << 2)
#define GB_BTN_START    (1 << 3)
#define GB_BTN_RIGHT    (1 << 4)
#define GB_BTN_LEFT     (1 << 5)
#define GB_BTN_UP       (1 << 6)
#define GB_BTN_DOWN     (1 << 7)

// ── T-Deck → Game Boy input mapping ───────────────────────────────────────────
//
//  Trackball up/down  → viewport scroll (peek outside the 2× letterbox)
//  Trackball click    → re-center viewport
//  Trackball left/right → reserved
//
//  W / A / S / D     → D-pad up / left / down / right
//  K                 → A button  (right thumb, home row)
//  L                 → B button
//  Enter             → Start
//  Space / Backspace → Select

// ── ISR-written viewport state (defined in InputMap.cpp) ─────────────────────
// Declared extern so InputMap methods can read them.
// DRAM_ATTR + defined in .cpp = correct literal-pool placement for IRAM ISRs.
extern DRAM_ATTR volatile int8_t  g_viewportDelta;
extern DRAM_ATTR volatile bool    g_reCenterView;

// Free-function ISRs (defined in InputMap.cpp, registered by InputMap::begin())
void IRAM_ATTR inputmap_isr_tbUp();
void IRAM_ATTR inputmap_isr_tbDown();
void IRAM_ATTR inputmap_isr_tbLeft();
void IRAM_ATTR inputmap_isr_tbRight();
void IRAM_ATTR inputmap_isr_tbPress();

class InputMap {
public:
    // Current Game Boy button state. Bit set = button held (active-high).
    // Inverted to active-low when written to gb_->direct.joypad in runFrame().
    volatile uint8_t state = 0;

    void begin() {
        pinMode(PIN_TB_UP,    INPUT_PULLUP);
        pinMode(PIN_TB_DOWN,  INPUT_PULLUP);
        pinMode(PIN_TB_LEFT,  INPUT_PULLUP);
        pinMode(PIN_TB_RIGHT, INPUT_PULLUP);
        pinMode(PIN_TB_PRESS, INPUT_PULLUP);

        attachInterrupt(PIN_TB_UP,    inputmap_isr_tbUp,    FALLING);
        attachInterrupt(PIN_TB_DOWN,  inputmap_isr_tbDown,  FALLING);
        attachInterrupt(PIN_TB_LEFT,  inputmap_isr_tbLeft,  FALLING);
        attachInterrupt(PIN_TB_RIGHT, inputmap_isr_tbRight, FALLING);
        attachInterrupt(PIN_TB_PRESS, inputmap_isr_tbPress, FALLING);
    }

    // Poll I2C keyboard (~60 Hz). Call from background task.
    void pollKeyboard() {
        Wire.requestFrom((uint8_t)KB_I2C_ADDR, (uint8_t)1);
        if (!Wire.available()) return;
        uint8_t key = Wire.read();
        if (key == 0) {
            noInterrupts();
            state   &= ~kbMask_;
            kbMask_  = 0;
            interrupts();
        } else {
            applyKey(key);
        }
    }

    // Consume viewport delta accumulated by trackball ISRs since last call.
    int8_t consumeViewportDelta() {
        noInterrupts();
        int8_t d = g_viewportDelta;
        g_viewportDelta = 0;
        interrupts();
        return d;
    }

    // Consume re-center flag set by trackball press ISR.
    bool consumeReCenter() {
        noInterrupts();
        bool r = g_reCenterView;
        g_reCenterView = false;
        interrupts();
        return r;
    }

    // Consume debug-overlay toggle flag set when Tab (0x09) is pressed.
    bool consumeDebugToggle() {
        noInterrupts();
        bool r = debugToggle_;
        debugToggle_ = false;
        interrupts();
        return r;
    }

    // Consume lobby toggle flag set when 'P' is pressed.
    bool consumeLobbyToggle() {
        noInterrupts();
        bool r = lobbyToggle_;
        lobbyToggle_ = false;
        interrupts();
        return r;
    }

    // Consume tournament toggle flag set when 'T' is pressed.
    bool consumeTournamentToggle() {
        noInterrupts();
        bool r = tournamentToggle_;
        tournamentToggle_ = false;
        interrupts();
        return r;
    }

    // Consume mute toggle flag set when 'M' is pressed.
    bool consumeMuteToggle() {
        noInterrupts();
        bool r = muteToggle_;
        muteToggle_ = false;
        interrupts();
        return r;
    }

    // When lobby capture is active, game keys are redirected to lobbyKey_.
    void setLobbyCapture(bool on) { lobbyCapture_ = on; }

    // Consume a lobby key (WSKLP). Returns 0 if none pending.
    uint8_t consumeLobbyKey() {
        noInterrupts();
        uint8_t k = lobbyKey_;
        lobbyKey_ = 0;
        interrupts();
        return k;
    }

private:
    uint8_t          kbMask_           = 0;
    volatile bool    debugToggle_      = false;
    volatile bool    lobbyToggle_      = false;
    volatile bool    tournamentToggle_ = false;
    volatile bool    muteToggle_       = false;
    volatile bool    lobbyCapture_     = false;
    volatile uint8_t lobbyKey_         = 0;

    void applyKey(uint8_t ascii) {
        // ── UI keys (not forwarded to Game Boy) ──────────────────────────────
        if (ascii == 0x09) {  // Tab → debug overlay
            noInterrupts();
            debugToggle_ = true;
            interrupts();
            return;
        }
        if (ascii == 'p' || ascii == 'P') {  // P → lobby toggle
            noInterrupts();
            lobbyToggle_ = true;
            interrupts();
            return;
        }
        if (ascii == 't' || ascii == 'T') {  // T → tournament toggle
            noInterrupts();
            tournamentToggle_ = true;
            interrupts();
            return;
        }
        if (ascii == 'm' || ascii == 'M') {  // M → mute toggle
            noInterrupts();
            muteToggle_ = true;
            interrupts();
            return;
        }

        // ── Lobby key capture mode ──────────────────────────────────────────
        if (lobbyCapture_) {
            // Forward WASD/KL to lobby overlay
            if (ascii == 'w' || ascii == 'W' ||
                ascii == 's' || ascii == 'S' ||
                ascii == 'k' || ascii == 'K' ||
                ascii == 'l' || ascii == 'L') {
                noInterrupts();
                lobbyKey_ = ascii;
                interrupts();
                return;
            }
        }

        // ── Game Boy button mapping ───────────────────────────────────────────
        uint8_t bit = 0;
        switch (ascii) {
            case 'w': case 'W': bit = GB_BTN_UP;     break;
            case 's': case 'S': bit = GB_BTN_DOWN;   break;
            case 'a': case 'A': bit = GB_BTN_LEFT;   break;
            case 'd': case 'D': bit = GB_BTN_RIGHT;  break;
            case 'k': case 'K': bit = GB_BTN_A;      break;
            case 'l': case 'L': bit = GB_BTN_B;      break;
            case '\r': case '\n':   bit = GB_BTN_START;  break;
            case ' ':
            case 0x08:              bit = GB_BTN_SELECT; break;
            default: return;
        }
        noInterrupts();
        state   |= bit;
        kbMask_ |= bit;
        interrupts();
    }
};
