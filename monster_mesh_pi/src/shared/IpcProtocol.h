#pragma once
// ── MonsterMesh Pi IPC Protocol ──────────────────────────────────────────────
// Unix socket at /tmp/monstermesh.sock
// JSON messages, newline-delimited (\n terminated)
// Daemon is server (one connection at a time), Terminal is client

static constexpr const char *MMD_SOCK_PATH   = "/tmp/monstermesh.sock";
#ifdef __APPLE__
static constexpr const char *MMD_STATE_DIR   = "/tmp/monstermesh";
static constexpr const char *MMD_STATE_PATH  = "/tmp/monstermesh/daycare.dat";
#else
static constexpr const char *MMD_STATE_DIR   = "/var/lib/monstermesh";
static constexpr const char *MMD_STATE_PATH  = "/var/lib/monstermesh/daycare.dat";
#endif
static constexpr const char *MMD_CONFIG_PATH = "/etc/monstermesh/config.json";
// Default save directory. On macOS dev we use /tmp/mm-test-saves so the
// daemon Just Works without passing an explicit arg. On Linux/Pi we use
// the canonical RetroPie path.
// A ':'-separated list of directories the SaveWatcher watches (newest .sav/
// .srm across ALL of them wins). Covers Gen 1 (gb), Gen 2 (gbc) and Gen 3
// (gba) carts, whose libretro cores save next to the ROM.
#ifdef __APPLE__
static constexpr const char *RETROPIE_SAVE_DIR = "/tmp/mm-test-saves";
#else
static constexpr const char *RETROPIE_SAVE_DIR =
    "/home/pi/RetroPie/roms/gb:/home/pi/RetroPie/roms/gbc:/home/pi/RetroPie/roms/gba";
#endif
static constexpr const char *MMD_SERIAL_PORT_DEFAULT = "/dev/ttyUSB0";

// Terminal → Daemon commands ("cmd" field):
//   GET_PARTY             — request current party data
//   GET_STATUS            — request daycare status + neighbor list
//   CHALLENGE <node_id>   — send PvP challenge (node_id as uint32 in "node_id" field)
//   ACCEPT_CHALLENGE      — accept queued incoming challenge ("node_id" field)
//   DECLINE_CHALLENGE     — decline queued challenge
//   FORCE_BEACON          — immediately broadcast a daycare beacon
//   FORCE_EVENT           — immediately trigger a daycare event (for testing)
//   PING                  — keepalive

// Daemon → Terminal pushes ("type" field):
//   PARTY_UPDATE          — party data changed, includes "party" array and "count"
//   STATUS                — daycare status, "neighbors" count, "active" bool,
//                           "last_event" string, "last_event_xp" int
//   DAYCARE_EVENT         — new event, "text" string, "xp" int, "slot" int
//   CHALLENGE_RECEIVED    — incoming PvP challenge, "node_id" uint32, "trainer" string,
//                           "party" array (minimal party data)
//   ACHIEVEMENT           — "name" string, "description" string
//   MESH_SEND_OK          — packet sent to mesh OK
//   MESH_SEND_FAIL        — packet failed to send
//   PONG                  — reply to PING
//   ERROR                 — "message" string

// Party slot JSON format (used in PARTY_UPDATE and CHALLENGE_RECEIVED):
// { "dex": 25, "level": 30, "nickname": "PIKACHU",
//   "moves": [85, 9, 73, 0], "hp": 80, "max_hp": 80,
//   "exp": 27000, "total_xp_gained": 450 }
