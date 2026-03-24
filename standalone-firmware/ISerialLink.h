#pragma once
#include <Arduino.h>
#include "peanut_gb.h"

// ── Abstract serial link interface ────────────────────────────────────────────
// EmulatorApp holds an optional pointer to one of these.
// The default (nullptr) stub returns GB_SERIAL_RX_NO_CONNECTION on every Rx.
// BattleShim implements this interface and routes bytes over LoRa.

class ISerialLink {
public:
    virtual ~ISerialLink() = default;

    // Called by EmulatorApp when the game writes to SB (serial buffer) and
    // asserts SC (starts a transfer). byte is the value the game is sending.
    virtual void onSerialTx(uint8_t byte) = 0;

    // Called by EmulatorApp when the game expects to receive a byte from the
    // link cable partner. Fill `out` with the received byte and return true.
    // Return false if no byte is available (→ emulator returns NO_CONNECTION,
    // game retries on the next frame).
    virtual bool onSerialRx(uint8_t &out) = 0;
};
