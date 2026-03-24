#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

#include "pins.h"
#include "InputMap.h"
#include "EmulatorApp.h"
#include "DebugOverlay.h"
#include "StatusOverlay.h"
#include "PokemonData.h"
#include "SX1262Transport.h"
#include "BattleShim.h"
#include "Lobby.h"
#include "LobbyOverlay.h"
#include "AlertDriver.h"
#include "TournamentCoordinator.h"
#include "TournamentClient.h"
#include "TournamentOverlay.h"

// ── Global singletons ─────────────────────────────────────────────────────────
static TFT_eSPI         tft;
static InputMap         input;
static EmulatorApp      emu(tft, input);
static SX1262Transport  radio;
static BattleShim       shim(radio);
static DebugOverlay     dbg(tft, emu, &shim);
static StatusOverlay    status(tft, &shim);
static Lobby            lobby(radio, emu);
static LobbyOverlay     lobbyUI(tft, lobby);
static AlertDriver      alert;
static TournamentCoordinator tournamentCoord(radio);
static TournamentClient      tournamentClient(radio);
static TournamentOverlay     tournamentUI(tft, tournamentCoord, tournamentClient, lobby);

// ── FreeRTOS task handles ─────────────────────────────────────────────────────
static TaskHandle_t emuTaskHandle    = nullptr;
static TaskHandle_t kbPollTaskHandle = nullptr;

// ────────────────────────────────────────────────────────────────────────────
//  Emulator task — Core 1, priority 5 (~60 fps)
// ────────────────────────────────────────────────────────────────────────────
static void emuTask(void *) {
    const TickType_t framePeriod = pdMS_TO_TICKS(16);
    TickType_t lastWake = xTaskGetTickCount();
    uint8_t prevBattle = 0;    // track wIsInBattle for auto-save
    uint16_t opponentElo = 0;  // stash opponent ELO for result recording

    while (true) {
        emu.runFrame();

        // ── Auto-save on battle end + ELO update ───────────────────────
        uint8_t curBattle = emu.readWRAM(Gen1::wIsInBattle);

        // Entering a link battle: stash opponent's ELO from lobby
        if (prevBattle == 0 && curBattle == 2 && opponentElo == 0) {
            // wBattleType==2 is link battle. Try to find opponent in peer table.
            uint16_t sid = shim.sessionId();
            if (sid != 0) {
                for (uint8_t i = 0; i < lobby.peerCount(); i++) {
                    // Match by checking if this peer's chipId XORed with ours
                    // produces the current session ID
                    uint32_t myId = radio.nodeId();
                    uint16_t testSid = (uint16_t)(myId ^ lobby.peer(i).chipId);
                    if (testSid == sid) {
                        opponentElo = lobby.peer(i).elo;
                        break;
                    }
                }
            }
        }

        if (prevBattle != 0 && curBattle == 0) {
            emu.save();
            Serial.println("[EMU] post-battle save");

            // Record ELO if this was a link battle with a known opponent
            if (opponentElo > 0) {
                // Check if player's party has any alive mons → win
                uint8_t partyCount = emu.readWRAM(Gen1::wPartyCount);
                bool won = false;
                for (uint8_t i = 0; i < partyCount && i < 6; i++) {
                    uint16_t hp = ((uint16_t)emu.readWRAM(Gen1::wPartyMons + i * 44 + 1) << 8) |
                                  emu.readWRAM(Gen1::wPartyMons + i * 44 + 2);
                    if (hp > 0) { won = true; break; }
                }
                lobby.recordResult(won, opponentElo);
                opponentElo = 0;
            }
        }
        prevBattle = curBattle;

        // ── Lobby toggle (P key) ────────────────────────────────────────
        if (input.consumeLobbyToggle()) {
            if (lobby.isOpen()) {
                lobby.close();
                input.setLobbyCapture(false);
            } else {
                lobby.open();
                input.setLobbyCapture(true);
            }
        }

        // ── Lobby key input ─────────────────────────────────────────────
        uint8_t lk = input.consumeLobbyKey();
        if (lk) lobbyUI.handleKey(lk);

        // ── Mute toggle (M key) ───────────────────────────────────────
        if (input.consumeMuteToggle()) {
            alert.toggleMute();
            Serial.printf("[ALERT] muted=%d\n", alert.isMuted());
        }

        // ── Tournament toggle (T key) ────────────────────────────────
        if (input.consumeTournamentToggle()) {
            if (tournamentUI.isActive()) {
                tournamentUI.close();
                input.setLobbyCapture(false);
            } else {
                tournamentUI.open();
                input.setLobbyCapture(true);
                // Close lobby if open
                if (lobby.isOpen()) lobby.close();
            }
        }

        // ── Tournament key input ─────────────────────────────────────
        if (tournamentUI.isActive()) {
            uint8_t tk = input.consumeLobbyKey();
            if (tk) tournamentUI.handleKey(tk);
        }

        // ── Lobby tick (beacons + timeouts) ─────────────────────────────
        lobby.tick(millis());

        // ── Tournament tick ──────────────────────────────────────────
        tournamentCoord.tick(millis());

        // ── Alert driver tick ────────────────────────────────────────────
        alert.tick(millis());

        // ── Overlays ─────────────────────────────────────────────────────
        if (input.consumeDebugToggle()) {
            dbg.toggle();
        }

        if (tournamentUI.isActive()) {
            tournamentUI.render();
        } else if (lobbyUI.isActive()) {
            lobbyUI.render();
        } else {
            dbg.render();
            status.render();
        }

        vTaskDelayUntil(&lastWake, framePeriod);
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  Keyboard poll task — Core 0, priority 1
// ────────────────────────────────────────────────────────────────────────────
static void kbPollTask(void *) {
    while (true) {
        input.pollKeyboard();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  Fatal error — red screen, halt
// ────────────────────────────────────────────────────────────────────────────
static void fatalError(const char *msg) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("ERROR", DISP_W / 2, DISP_H / 2 - 20);
    tft.setTextSize(1);
    tft.drawString(msg, DISP_W / 2, DISP_H / 2 + 10);
    Serial.printf("[FATAL] %s\n", msg);
    while (true) vTaskDelay(portMAX_DELAY);
}

// ────────────────────────────────────────────────────────────────────────────
//  setup()
// ────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[PokeMesh] booting...");

    // ── Keyboard power enable ─────────────────────────────────────────────
    pinMode(PIN_KB_POWERON, OUTPUT);
    digitalWrite(PIN_KB_POWERON, HIGH);
    delay(100);

    // ── I2C (keyboard) ────────────────────────────────────────────────────
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // ── Display (init first so error screens always work) ─────────────────
    // TFT_eSPI manages its own SPI peripheral via -D build flags.
    // Do NOT call SPI.begin() before tft.init() — it conflicts on ESP32-S3.
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    Serial.println("[BOOT] TFT OK");

    // ── SPI bus for SD (share TFT's SPI pins) ────────────────────────────
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SD_CS);

    // ── SD card ───────────────────────────────────────────────────────────
    if (!SD.begin(PIN_SD_CS, SPI)) {
        fatalError("SD card missing");
    }
    Serial.println("[BOOT] SD OK");

    // ── LittleFS (save files) ─────────────────────────────────────────────
    if (!LittleFS.begin(true)) {
        Serial.println("[WARN] LittleFS mount failed — saves disabled");
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("PokeMesh", DISP_W / 2, DISP_H / 2 - 20);
    tft.setTextSize(1);
    tft.drawString("Loading...", DISP_W / 2, DISP_H / 2 + 10);

    // ── Trackball interrupts ──────────────────────────────────────────────
    input.begin();

    // ── Load lobby stats (must be after LittleFS mount) ────────────────────
    lobby.loadStats();

    // ── Alert driver (piezo buzzer on GPIO 14) ──────────────────────────────
    alert.begin();
    lobby.setAlertDriver(&alert);

    // ── LoRa radio ────────────────────────────────────────────────────────
    if (!radio.begin()) {
        // Non-fatal: emulator works without radio; battle link just won't work.
        Serial.println("[WARN] SX1262 init failed — battle link disabled");
    } else {
        // Wire BattleShim into the emulator's serial callbacks
        shim.begin();
        shim.setLobby(&lobby);
        shim.setTournamentCoord(&tournamentCoord);
        shim.setTournamentClient(&tournamentClient);
        lobby.setShim(&shim);
        status.setLobby(&lobby);
        emu.setSerialLink(&shim);
        Serial.println("[BOOT] LoRa + BattleShim + Lobby ready");
    }

    // ── Start emulator ────────────────────────────────────────────────────
    if (!emu.begin("/pokemon.gb")) {
        fatalError("/pokemon.gb not found on SD");
    }

    // ── Launch tasks ──────────────────────────────────────────────────────
    xTaskCreatePinnedToCore(emuTask,    "emu",  8192, nullptr, 5, &emuTaskHandle,    1);
    xTaskCreatePinnedToCore(kbPollTask, "kb",   2048, nullptr, 1, &kbPollTaskHandle, 0);

    Serial.println("[PokeMesh] running  (Tab=debug  P=lobby  T=tournament  M=mute)");
}

// ────────────────────────────────────────────────────────────────────────────
//  loop() — minimal; all real work is in FreeRTOS tasks
// ────────────────────────────────────────────────────────────────────────────
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
