#pragma once
// MonsterMesh Serial-quiet flag
//
// Gates every direct Serial.print*/println* call in the MonsterMesh module
// files. When true, MonsterMesh stops writing to the USB-CDC serial line
// so the Meshtastic stream-framing protocol isn't corrupted and external
// tools (`meshtastic --port ...`, phone admin, etc.) can drive the device
// cleanly.
//
// Pattern: files include this header and use `MMSer.println(...)` instead
// of `Serial.println(...)`. The macro expands to a guarded call that
// short-circuits when quiet mode is on.

#include <Arduino.h>

extern bool g_mmSerialQuiet;
void mmSerialSetQuiet(bool on);
void mmSerialLoadPersisted();

// Use like: MMSer.printf("..."), MMSer.println("..."), etc.
#define MMSer if (!g_mmSerialQuiet) Serial
