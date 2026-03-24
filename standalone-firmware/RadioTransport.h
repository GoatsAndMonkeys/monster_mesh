#pragma once
#include <Arduino.h>

// ── Abstract LoRa transport interface ────────────────────────────────────────
// Keeps BattleShim independent of the specific radio chip.
// SX1262Transport (firmware/src/) is the concrete implementation for T-Deck.

class RadioTransport {
public:
    virtual ~RadioTransport() = default;

    // Initialise radio hardware. Returns true on success.
    virtual bool begin() = 0;

    // Transmit len bytes. Blocks until transmission completes (or fails).
    // Returns true on success. len must be ≤ BATTLELINK_MAX_PKT (200).
    virtual bool send(const uint8_t *data, size_t len) = 0;

    // Returns true if a received packet is waiting (non-blocking check).
    virtual bool available() = 0;

    // Read the oldest pending packet into buf (up to bufLen bytes).
    // Sets len to the actual packet length.
    // Returns true on success. Caller should call startReceive() is not needed
    // — the implementation re-arms receive internally.
    virtual bool receive(uint8_t *buf, size_t &len, size_t bufLen) = 0;

    // RSSI of the last received packet (dBm). Used by lobby for signal display.
    virtual int16_t lastRssi() const { return 0; }

    // 32-bit unique ID for this node (used to determine master/slave role).
    // Default implementation uses ESP32 chip ID.
    virtual uint32_t nodeId() const;
};
