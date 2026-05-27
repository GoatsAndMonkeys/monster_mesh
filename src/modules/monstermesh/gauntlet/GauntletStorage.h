// SPDX-License-Identifier: MIT
//
// GauntletStorage — FSCom-backed persistence for the gym ladder. Mirrors the
// LordSave pattern (single fixed-size record, atomic rewrite).

#pragma once
#include "GauntletData.h"

// ── Public API ────────────────────────────────────────────────────────────────

// Zero a state and stamp magic/version + identifying fields.
void gauntletInitDefaults(GauntletState &s,
                          const char *gymName, const char *badge,
                          uint32_t nodeNum);

// Read /monstermesh/gauntlet.dat. Returns true on a fully-validated load,
// false otherwise (in which case `s` is reinitialised to defaults).
bool gauntletLoad(GauntletState &s,
                  const char *defaultName, const char *defaultBadge,
                  uint32_t nodeNum);

// Atomic-ish write. Returns true on success.
bool gauntletSave(const GauntletState &s);
