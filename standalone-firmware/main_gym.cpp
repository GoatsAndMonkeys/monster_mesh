// ── PokeMesh Mesh Gym — Dedicated Tournament Coordinator Node ────────────────
// Standalone firmware for an ESP32 + SX1262 board (no display, no emulator).
// Receives tournament registrations and results over LoRa, manages brackets,
// broadcasts announcements and match assignments.
//
// Build with:  pio run -e mesh-gym
//
// This file is only compiled for the mesh-gym environment (see platformio.ini
// build_src_filter).

#include <Arduino.h>
#include "pins.h"
#include "SX1262Transport.h"
#include "TournamentCoordinator.h"
#include "BattlePacket.h"

static SX1262Transport  radio;
static TournamentCoordinator coordinator(radio);

// Receive buffer
static uint8_t rxBuf[BATTLELINK_MAX_PKT];

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[MeshGym] booting...");

    if (!radio.begin()) {
        Serial.println("[FATAL] SX1262 init failed");
        while (true) delay(1000);
    }
    Serial.println("[MeshGym] LoRa ready");

    // Auto-create a double-elimination tournament on boot
    coordinator.create(
        TournamentCoordinator::TournamentType::DOUBLE_ELIM,
        16,
        "MESH GYM"
    );

    Serial.println("[MeshGym] tournament created, accepting registrations");
    Serial.println("[MeshGym] Send 'S' over serial to start the tournament");
}

void loop() {
    uint32_t now = millis();

    // ── Receive packets ──────────────────────────────────────────────────────
    size_t len = 0;
    if (radio.receive(rxBuf, len, sizeof(rxBuf))) {
        if (len >= BATTLELINK_HDR_SIZE) {
            coordinator.handlePacket(rxBuf, len);
        }
    }

    // ── Coordinator tick (announcements, timeouts) ───────────────────────────
    coordinator.tick(now);

    // ── Serial commands (for headless operation) ─────────────────────────────
    if (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 'S': case 's':
                Serial.println("[MeshGym] starting tournament...");
                coordinator.startTournament();
                break;
            case 'I': case 'i':
                Serial.printf("[MeshGym] phase=%u players=%u round=%u matches=%u\n",
                              (unsigned)coordinator.phase(),
                              coordinator.playerCount(),
                              coordinator.currentRound(),
                              coordinator.matchCount());
                break;
        }
    }

    delay(10);  // yield
}
