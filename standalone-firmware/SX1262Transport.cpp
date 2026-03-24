#include "SX1262Transport.h"
#include <esp_system.h>   // esp_efuse_mac_get_default()

// ── Global ISR shim ───────────────────────────────────────────────────────────
// Must be a free function with C linkage for RadioLib setDio1Action().
// Defined here (not in header) so IRAM placement is correct on Xtensa.
SX1262Transport *g_sx1262Transport = nullptr;

void IRAM_ATTR sx1262_radio_isr() {
    if (g_sx1262Transport) g_sx1262Transport->setFlag();
}

// ── Constructor ───────────────────────────────────────────────────────────────
SX1262Transport::SX1262Transport()
    : radio_{nullptr}
{
    g_sx1262Transport = this;
}

// ── begin() ───────────────────────────────────────────────────────────────────
bool SX1262Transport::begin() {
    // Create Module here (not in constructor) to avoid GPIO access before setup()
    radio_ = SX1262(new Module(PIN_LORA_CS, PIN_LORA_IRQ, PIN_LORA_RST, PIN_LORA_BUSY));
    radioMutex_ = xSemaphoreCreateMutex();

    int state = radio_.begin(
        LORA_FREQ,
        LORA_BW,
        LORA_SF,
        LORA_CR,
        LORA_SYNC_WORD,
        LORA_TX_POWER,
        LORA_PREAMBLE_LEN,
        LORA_TCXO_VOLTAGE
    );

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[RADIO] begin failed: %d\n", state);
        return false;
    }

    // Use DIO1 for receive-done interrupt
    radio_.setDio1Action(sx1262_radio_isr);
    startReceive();

    Serial.printf("[RADIO] SX1262 ready  %.1f MHz  SF%d  BW%.0f  sync=0x%02X\n",
                  (double)LORA_FREQ, LORA_SF, (double)LORA_BW, LORA_SYNC_WORD);
    return true;
}

// ── send() ────────────────────────────────────────────────────────────────────
bool SX1262Transport::send(const uint8_t *data, size_t len) {
    // Mutex: radio hardware shared between Core 0 (BattleShim) and Core 1 (Lobby)
    xSemaphoreTake(radioMutex_, portMAX_DELAY);

    radioFlag_ = false;
    int state = radio_.transmit(const_cast<uint8_t *>(data), len);
    startReceive();  // re-arm receive after Tx

    xSemaphoreGive(radioMutex_);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[RADIO] Tx error: %d\n", state);
        return false;
    }
    return true;
}

// ── available() ───────────────────────────────────────────────────────────────
bool SX1262Transport::available() {
    return radioFlag_;
}

// ── receive() ────────────────────────────────────────────────────────────────
bool SX1262Transport::receive(uint8_t *buf, size_t &len, size_t bufLen) {
    if (!radioFlag_) return false;

    xSemaphoreTake(radioMutex_, portMAX_DELAY);

    // Re-check flag under mutex (a send from another core may have cleared it)
    if (!radioFlag_) {
        xSemaphoreGive(radioMutex_);
        return false;
    }
    radioFlag_ = false;

    size_t pktLen = radio_.getPacketLength();
    if (pktLen == 0 || pktLen > bufLen) {
        radio_.readData(buf, 0);
        startReceive();
        xSemaphoreGive(radioMutex_);
        return false;
    }

    int state = radio_.readData(buf, pktLen);
    lastRssi_ = radio_.getRSSI();
    startReceive();

    xSemaphoreGive(radioMutex_);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[RADIO] Rx error: %d\n", state);
        return false;
    }

    len = pktLen;
    return true;
}

// ── startReceive() ────────────────────────────────────────────────────────────
void SX1262Transport::startReceive() {
    radioFlag_ = false;
    radio_.startReceive();
}

// ── RadioTransport::nodeId() ─────────────────────────────────────────────────
// Default implementation in the base class: use lower 32 bits of ESP32 MAC.
uint32_t RadioTransport::nodeId() const {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    return ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] <<  8) | mac[5];
}
