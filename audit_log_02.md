# Audit Log 02

Date: 2026-03-24
Branch: `dev_tc`
Reviewer: Claude (second pass — full source review of every file in repo)

## Scope

Repo review to answer:

1. Why it is not working with Meshtastic
2. Why it will not work with the LilyGO T-Deck

## Summary

The repo has two separate paths:

- `meshtastic-module/` for patching into Meshtastic firmware
- `standalone-firmware/` for a direct RadioLib-based standalone image

Both paths currently have blocking issues. The Meshtastic path is pinned to an old upstream layout and contains a broken initialization path. The standalone path is not configured to build the real T-Deck application and, even if fixed, is not Meshtastic-compatible because it uses its own raw LoRa protocol. Additionally, both paths share protocol-level bugs in matchmaking and battle timeout logic, and the standalone firmware has a hardware peripheral conflict and a thread-safety issue.

## Findings — Original (all verified)

### 1. Meshtastic module initialization is broken — VERIFIED

Files:

- `meshtastic-module/MonsterMeshModule.h:61`
- `meshtastic-module/MonsterMeshModule.cpp:53-137`
- `meshtastic-module/MonsterMeshModule.cpp:227-272`
- `meshtastic-module/MeshtasticTransport.h:22-25`

The module puts the real mesh/emulator setup inside `MonsterMeshModule::setup()`, including:

- `transport_.begin()`
- `transport_.setNodeId(nodeDB->getNodeNum())`
- `shim_.begin()`
- `lobby_.loadStats()`
- `emu_.setSerialLink(&shim_)`

But Meshtastic does not call `MeshModule::setup()` for loaded modules in the upstream trees I checked. The code path that does run is `runOnce()`, and that path does not perform the missing transport/shim setup.

Impact:

- `MeshtasticTransport` never allocates its RX queue
- incoming mesh packets get dropped
- node ID is never set for pairing/session logic
- the emulator is not wired to the battle shim
- the lobby/shim path cannot work reliably

This is the most direct reason the Meshtastic integration does not work.

Additional note (second-pass): See also finding 8 — if `setup()` IS called by some Meshtastic version, the module double-initializes because `setup()` never sets `setupDone_`.

### 2. The Meshtastic integration is pinned to an outdated upstream shape — VERIFIED

Files:

- `README.md:13-18`
- `patches/Modules.patch`
- `patches/kbI2cBase.patch`

The README says the integration is based on Meshtastic `v2.7.15` and says to build the `t-deck` env.

What I verified:

- `patches/Modules.patch` applies to official Meshtastic tag `v2.7.15.567b8ea`
- `patches/kbI2cBase.patch` also applies there
- `patches/Modules.patch` does not apply to current Meshtastic `master`
- `patches/Modules.patch` does not apply to current Meshtastic `develop`

Impact:

- On modern Meshtastic, this repo does not even register the module without rebasing the patch
- The instructions are locked to an older firmware layout
- The current T-Deck UI firmware target upstream is `t-deck-tft`, not bare `t-deck`

### 3. The T-Deck-specific Meshtastic display/input path is unfinished — VERIFIED

Files:

- `meshtastic-module/MonsterMeshModule.cpp:295-297`
- `meshtastic-module/MonsterMeshModule.cpp:404-420`

The T-Deck/TFT path has two problems:

- `installKeyboardHook()` is empty
- `pollKeyboard()` is empty

Also, the scanline renderer expects this external symbol:

- `extern lgfx::LGFX_Device *getLovyanGfx();`

I did not find that symbol in Meshtastic `v2.7.15.567b8ea` or current upstream.

Impact:

- no complete verified TFT keyboard hook
- no confirmed render hook for blitting emulator scanlines onto the T-Deck display

So even on the pinned Meshtastic version, the T-Deck-specific UI path is incomplete.

Additional note (second-pass): The non-TFT path (`handleInputEvent` via InputBroker, `drawFrame` for OLED) is structurally complete. The keyboard input routing through `handleKeyPress` works (WASD/KL/Enter/Space mapped to Game Boy buttons, Ctrl+E toggles emulator, P opens lobby, Tab toggles debug). The issue is specific to the TFT display and LVGL keyboard integration.

### 4. The standalone `t-deck` target is not the actual firmware — VERIFIED

Files:

- `standalone-firmware/platformio.ini:72-80`
- `standalone-firmware/boot_test.cpp`
- `standalone-firmware/main.cpp`

The default standalone env is `t-deck`, but that env is currently configured to build only:

- `boot_test.cpp`

The real app source is not being built, and the required libraries are commented out:

- `TFT_eSPI`
- `RadioLib`

Impact:

- building `standalone-firmware` as-is does not produce the game firmware
- it produces only a backlight blink test
- the actual application in `main.cpp` is excluded

This is a separate reason it will not work on a T-Deck in its current state.

### 5. The standalone radio protocol is not Meshtastic-compatible — VERIFIED

Files:

- `standalone-firmware/SX1262Transport.h:12-21`
- `standalone-firmware/SX1262Transport.cpp`

The standalone image uses direct RadioLib SX1262 access and hard-codes:

- custom sync word `0x12`

The repo comment itself notes this differs from Meshtastic.

Impact:

- the standalone image cannot interoperate with Meshtastic nodes
- it only talks to another node running the same custom PokeMesh radio protocol

So if the expectation was "flash the standalone image and have it work with Meshtastic", that will not happen.

### 6. Matchmaking packets are logically broken in both implementations — VERIFIED

Files:

- `meshtastic-module/MonsterMeshLobby.cpp:182-219`
- `meshtastic-module/MonsterMeshLobby.cpp:221-252`
- `standalone-firmware/Lobby.cpp:289-326`
- `standalone-firmware/Lobby.cpp:328-388`
- `standalone-firmware/BattlePacket.h:36-40`

`sendChallenge(targetId)`, `sendAccept(targetId)`, and `sendReject(targetId)` all ignore `targetId` in both the Meshtastic and standalone lobby implementations. All three functions declare `targetId` as a parameter but only serialize `transport_.nodeId()` (the sender's chip ID). The target is never included in the packet.

At the same time, the packet definitions document these as unicast-style lobby messages.

Impact:

- a challenge is not actually addressed to a specific peer
- any listening node in browsing state can treat the challenge as meant for itself
- accept/reject semantics are ambiguous and fragile

Clarification (second-pass): The accept and reject handlers partially work despite this bug because `handleAcceptPkt` checks `fromId != challengeTarget_` and `handleRejectPkt` does the same. So accepts/rejects from unexpected senders are dropped. The real problem is the challenge path: `handleChallenge` transitions ANY node in BROWSING state to INCOMING for any challenge it hears. With 3+ nodes, a challenge intended for one peer will be received by all peers in BROWSING state.

This is a protocol bug independent of the build issues.

### 7. The standalone custom T-Deck variant is not wired in and contains wrong defaults — VERIFIED

Files:

- `standalone-firmware/platformio.ini:38-39`
- `standalone-firmware/variants/tdeck/pins_arduino.h`

The config comments say the standalone firmware uses a custom board variant under `variants/tdeck/`, but `platformio.ini` does not actually set:

- `board_build.variant`
- `board_build.variants_dir`

Also, the local `pins_arduino.h` still carries `esp32s3box`-style defaults, including:

- `SDA = 41`
- `SCL = 40`
- `MOSI = 11`
- `MISO = 13`
- `SCK = 12`

Those are not the T-Deck defaults. Compare with `standalone-firmware/pins.h` which has the correct values:

- `PIN_I2C_SDA = 18`
- `PIN_I2C_SCL = 8`
- `PIN_SPI_MOSI = 41`
- `PIN_SPI_MISO = 38`
- `PIN_SPI_SCK = 40`

Impact:

- the custom variant is likely ignored entirely (no `board_build.variant` set)
- if it were used, the pin defaults are still wrong for the T-Deck

## Findings — New (second-pass)

### 8. Meshtastic module double-initializes if `setup()` is called

Files:

- `meshtastic-module/MonsterMeshModule.cpp:53-137` (`setup()`)
- `meshtastic-module/MonsterMeshModule.cpp:227-267` (`runOnce()` lazy init)

`setup()` performs full initialization (transport, shim, lobby, emulator, FreeRTOS task, InputBroker subscription) but never sets `setupDone_ = true`.

`runOnce()` checks `if (!setupDone_)` and, after an 8-second delay, performs its own partial initialization: SD mount, emulator begin, FreeRTOS task creation, keyboard hook, and InputBroker subscription.

If a Meshtastic version DOES call `setup()`, then `runOnce()` will still enter the lazy-init block and:

- mount SD a second time (with `spiLock`, potentially conflicting with the first mount)
- call `emu_.begin()` a second time (double PSRAM allocation for ROM + `gb_s`)
- create a second emulator FreeRTOS task on Core 1
- call `inputObserver_.observe(inputBroker)` a second time (duplicate event delivery)

Impact:

- the module's initialization is broken in both directions: if `setup()` doesn't run, transport/shim/lobby are uninitialized (finding 1); if `setup()` does run, everything gets double-initialized
- the double FreeRTOS task would cause two emulator frames per 16ms tick, corrupting emulator state

### 9. Standalone LEDC channel 0 conflict between display backlight and piezo buzzer

Files:

- `standalone-firmware/EmulatorApp.cpp:50-52`
- `standalone-firmware/AlertDriver.h:13-16`
- `standalone-firmware/main.cpp:238,258`

Both `EmulatorApp::begin()` and `AlertDriver::begin()` configure LEDC channel 0:

```
EmulatorApp::begin():
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);   // backlight on channel 0
    ledcWrite(0, 200);

AlertDriver::begin():
    ledcSetup(0, 1000, 8);
    ledcAttachPin(PIN_BUZZER, 0);    // buzzer on channel 0
    ledcWriteTone(0, 0);
```

In `main.cpp`, `alert.begin()` runs at line 238 and `emu.begin()` runs at line 258. So emu.begin() reconfigures channel 0 last, attaching the backlight. But both `PIN_TFT_BL` (42) and `PIN_BUZZER` (14) remain attached to the same LEDC channel.

Impact:

- when `AlertDriver` plays a tone via `ledcWriteTone(0, freq)`, it changes the frequency of channel 0, which also drives the backlight pin — the screen will flicker or dim during alerts
- when `EmulatorApp` sets backlight brightness via `ledcWrite(0, 200)`, it also drives the buzzer pin
- the buzzer and backlight interfere with each other continuously

Fix: AlertDriver should use a different LEDC channel (e.g., channel 1).

### 10. Standalone Lobby has a cross-core race condition

Files:

- `standalone-firmware/BattleShim.cpp:106-167` (`radioTaskLoop()` on Core 0)
- `standalone-firmware/BattleShim.cpp:326-331` (lobby packet forwarding)
- `standalone-firmware/Lobby.cpp:41-73` (`tick()` on Core 1)
- `standalone-firmware/Lobby.cpp:77-106` (navigation methods on Core 1)

In the standalone firmware, `BattleShim::radioTaskLoop()` runs on Core 0 and forwards received lobby packets to `Lobby::handlePacket()`. Meanwhile, `Lobby::tick()` and all UI navigation methods (`navigateUp()`, `navigateDown()`, `selectPeer()`, `rejectIncoming()`) run on Core 1 from `emuTask`.

`Lobby::state_` is declared `volatile`, but there is no mutex or other synchronization protecting compound accesses to `state_`, `peers_[]`, `peerCount_`, `cursor_`, `challengeFrom_`, or `challengeTarget_`.

Concrete race example:

1. Core 1 (`tick()`): reads `peerCount_` as 3, begins iterating `peers_[]`
2. Core 0 (`handleBeacon()`): calls `addOrUpdatePeer()` which modifies `peers_[]` or increments `peerCount_`
3. Core 1: reads partially-written peer entry → corrupt name, ELO, or species data

Impact:

- peer table corruption (garbled names, wrong ELO values)
- challenge directed at wrong peer if `cursor_` and `peerCount_` are inconsistent
- potential crash if `peerCount_` is read mid-update

Note: The Meshtastic module does NOT have this bug because `MonsterMeshBattleShim::processIncoming()` is called from `tick()` within `emuTaskLoop()`, so all lobby access is single-threaded on Core 1.

### 11. Battle timeout is falsely reset by unrelated lobby packets

Files:

- `meshtastic-module/MonsterMeshBattleShim.cpp:123-131`
- `standalone-firmware/BattleShim.cpp:112-119`

In both implementations, `processIncoming()` / `radioTaskLoop()` updates `lastPacketMs_` on every received packet, regardless of type:

```cpp
// Meshtastic module (MonsterMeshBattleShim.cpp:129)
if (transport_.receive(buf, len, sizeof(buf))) {
    handlePacket(buf, len);
    lastPacketMs_ = millis();
}

// Standalone (BattleShim.cpp:118)
if (transport_.receive(buf, len, sizeof(buf))) {
    handlePacket(buf, len);
    lastPacketMs_ = millis();
}
```

The battle timeout check (`lastPacketMs_` + `BATTLE_TIMEOUT_MS`) is meant to detect when a battle opponent disconnects. But `lastPacketMs_` is reset by ANY packet, including lobby beacons from unrelated nodes.

Impact:

- if any other node is beaconing (every 2 minutes), the 30-second battle timeout will never fire
- a disconnected battle opponent will not be detected as long as other lobby traffic exists
- the session will hang indefinitely instead of timing out and resetting to IDLE

Fix: `lastPacketMs_` should only be updated for packets that match the current `sessionId_`, not for lobby or other unrelated traffic.

## Verification Notes

What I verified directly:

- current repo contents (every file read and cross-referenced)
- current branch name
- all line numbers referenced in original audit (confirmed accurate)
- patch applicability against official Meshtastic tag `v2.7.15.567b8ea`
- patch applicability against current Meshtastic `master`
- patch applicability against current Meshtastic `develop`
- cross-referenced `variants/tdeck/pins_arduino.h` defaults against `standalone-firmware/pins.h` (confirmed mismatch)
- traced LEDC channel usage across EmulatorApp and AlertDriver (confirmed conflict)
- traced cross-core data flow for Lobby in standalone firmware (confirmed race)
- traced `lastPacketMs_` update path in both BattleShim implementations (confirmed indiscriminate reset)

Current upstream references checked during the audit:

- Meshtastic `master`: `163c54877c5b437a2b1e9a89448cd6be2fe8d7a6`
- Meshtastic `develop`: `e14b8d385a39075973e127bb0f68190dc9ee8fbb`

Limitations:

- `pio` / `platformio` is not installed in this environment, so I could not run a full local build
- the findings above are based on source inspection plus upstream patch validation

## Bottom Line

Why it is not working with Meshtastic:

- the module initialization path is broken (finding 1), and double-initializes if setup() is called (finding 8)
- the repo depends on an old Meshtastic layout and stale module-registration patch (finding 2)
- the T-Deck render/input integration is incomplete (finding 3)

Why it will not work with the LilyGO T-Deck:

- the standalone `t-deck` build is currently only a boot/backlight test (finding 4)
- the standalone custom board variant is not properly wired and has incorrect defaults (finding 7)
- the standalone transport is a custom LoRa protocol, not Meshtastic (finding 5)
- the backlight and buzzer share LEDC channel 0 (finding 9)

Protocol bugs present in both paths:

- matchmaking challenges are broadcast without a target, causing ambiguity with 3+ nodes (finding 6)
- battle timeout never fires while lobby beacons are being received (finding 11)
- standalone lobby state is accessed from two cores without synchronization (finding 10)
