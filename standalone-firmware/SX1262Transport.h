#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include <freertos/semphr.h>
#include "RadioTransport.h"
#include "pins.h"

// ── LoRa radio settings ───────────────────────────────────────────────────────
// These are the default values. Adjust for your region:
//   EU-868: change LORA_FREQ to 868.0
//   US-915: 915.0 (default)
#ifndef LORA_FREQ
  #define LORA_FREQ        915.0f  // MHz
#endif
#define LORA_BW            125.0f  // kHz bandwidth
#define LORA_SF            7       // spreading factor 7 — fast, ~5 km range
#define LORA_CR            5       // coding rate 4/5
#define LORA_SYNC_WORD     0x12    // PokeMesh custom sync word (≠ Meshtastic 0x2B)
#define LORA_TX_POWER      22      // dBm (SX1262 max)
#define LORA_PREAMBLE_LEN  8       // symbols
#define LORA_TCXO_VOLTAGE  1.8f    // V — T-Deck SX1262 module uses TCXO

class SX1262Transport : public RadioTransport {
public:
    SX1262Transport();

    bool begin() override;
    bool send(const uint8_t *data, size_t len) override;
    bool available() override;
    bool receive(uint8_t *buf, size_t &len, size_t bufLen) override;
    int16_t lastRssi() const override { return lastRssi_; }

    // Re-arm receive mode. Called automatically after send() and receive().
    void startReceive();

    // Called by DIO1 ISR — marks a packet as available.
    void setFlag() { radioFlag_ = true; }

private:
    SX1262 radio_{nullptr};
    volatile bool radioFlag_ = false;
    int16_t lastRssi_ = 0;
    SemaphoreHandle_t radioMutex_ = nullptr;  // protects radio_ from cross-core access
};

// ── Global ISR shim ───────────────────────────────────────────────────────────
// Defined in SX1262Transport.cpp alongside the singleton pointer.
// Must be declared here so the .cpp translation unit can find it at link time.
extern SX1262Transport *g_sx1262Transport;
void IRAM_ATTR sx1262_radio_isr();
