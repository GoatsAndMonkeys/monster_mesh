// SPDX-License-Identifier: MIT
//
// SAV cache — mirrors the Gen 1 .sav on the SD card into Meshtastic's
// LittleFS so the SAV can be read without touching the shared SPI bus
// after boot. Canonical source of truth at runtime is an in-RAM copy
// owned by MonsterMeshModule; this module only handles the disk side.
//
// Layout (all under /monstermesh/):
//   sav.bin       — 32KB raw Gen 1 SRAM dump
//   last_rom.txt  — SD-relative ROM path (e.g. "/pokemon.gb")
//
// Mirrors the LordSave.cpp pattern exactly.

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

static constexpr size_t SAV_CACHE_SIZE = 0x8000; // 32KB Gen 1 SRAM

// Read cached SAV + last-ROM path from LittleFS.
//
//   buf        — destination for SAV bytes, must be >= SAV_CACHE_SIZE
//   n          — size of buf (asserted >= SAV_CACHE_SIZE)
//   lastRomOut — destination for last ROM path (null-terminated)
//   lastRomLen — size of lastRomOut
//
// Returns true if the 32KB SAV was read successfully. lastRomOut may be
// empty even on success if no last_rom.txt exists yet.
bool savCacheLoad(uint8_t *buf, size_t n, char *lastRomOut, size_t lastRomLen);

// Write SAV + last-ROM path to LittleFS.
//
//   buf     — SAV bytes, must be SAV_CACHE_SIZE bytes
//   n       — size of buf (asserted == SAV_CACHE_SIZE)
//   lastRom — SD-relative path, null-terminated (may be nullptr/empty to skip)
//
// Returns true on success. Creates /monstermesh/ if missing.
bool savCacheStore(const uint8_t *buf, size_t n, const char *lastRom);
