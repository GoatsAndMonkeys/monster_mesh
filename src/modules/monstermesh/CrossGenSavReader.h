#pragma once
// ── Cross-gen .sav dispatcher ───────────────────────────────────────────────
// Detects whether a raw save buffer is a Gen 3 (128KB GBA battery) or Gen 2
// (32KB GSC SRAM) save and parses the party through the matching reader.
//
// Detection order:
//   1. len >= 131072 and gen3SavLooksValid (section signature 0x08012025 in
//      either save block)                          -> gen3ReadParty, genOut=3
//   2. len >= 32768: gen2SavLooksValid as Crystal, then as Gold/Silver
//      (primary 16-bit LE byte-sum checksum)       -> gen2ReadParty, genOut=2
//   3. otherwise                                   -> 0, genOut=0
//
// A Gen 1 .sav is also 32KB but fails both GSC checksums — that is the
// desired signal for the caller to fall back to the existing Gen 1 path.

#include <stddef.h>
#include <stdint.h>
#include "Gen2SavReader.h"
#include "Gen3SavReader.h"

// Detect + parse a Gen 2/3 save buffer into ParsedMon[6].
// Returns party count (0-6) and sets genOut (2 or 3) on success.
// Returns 0 with genOut=0 if the buffer is not a valid Gen 2/3 save
// (caller falls back to the existing Gen 1 path).
inline uint8_t crossGenReadSavParty(const uint8_t *buf, size_t len,
                                    ParsedMon out[6], uint8_t &genOut)
{
    genOut = 0;
    if (!buf) return 0;

    // Gen 3: 128KB GBA battery save with signed sections.
    if (len >= 131072 && gen3SavLooksValid(buf)) {
        genOut = 3;
        return gen3ReadParty(buf, out);
    }

    // Gen 2: 32KB SRAM. Try Crystal first, then Gold/Silver.
    if (len >= 32768) {
        if (gen2SavLooksValid(buf, true)) {   // Crystal
            genOut = 2;
            return gen2ReadParty(buf, true, out);
        }
        if (gen2SavLooksValid(buf, false)) {  // Gold/Silver
            genOut = 2;
            return gen2ReadParty(buf, false, out);
        }
    }

    return 0;  // not a valid Gen 2/3 save — Gen 1 fallback
}
