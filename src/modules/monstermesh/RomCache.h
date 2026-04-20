// SPDX-License-Identifier: MIT
//
// ROM cache — mirrors the most-recently-launched GB ROM from the SD card
// into Meshtastic's LittleFS so subsequent launches don't touch the shared
// SPI bus at all. LittleFS on the ESP32-S3 is on internal flash, so reads
// don't contend with the radio/TFT.
//
// Layout (all under /monstermesh/):
//   rom.bin       — raw ROM image, up to ROM_CACHE_MAX bytes
//   rom_path.txt  — SD-relative path of the cached ROM (used as the cache key)

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Cap on cached ROM size. Gen 1 is 1 MB, Gen 2 is 2 MB; cap at 2 MB.
static constexpr size_t ROM_CACHE_MAX = 2 * 1024 * 1024;

// True if LittleFS has a cached ROM for `romPath` (exact-match compare).
// On success, reads up to `bufLen` bytes into `buf` and writes the actual
// size to *outSize. Returns false if no cache, size mismatch, or `romPath`
// doesn't match the cache-key file.
bool romCacheLoad(const char *romPath,
                  uint8_t *buf, size_t bufLen, size_t *outSize);

// Write (or overwrite) the cache with this ROM. `romPath` is the SD-side
// path that will serve as the cache key on next boot.
bool romCacheStore(const char *romPath, const uint8_t *buf, size_t size);
