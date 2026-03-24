#include "InputMap.h"

// ── ISR-written globals ───────────────────────────────────────────────────────
// Defined here (not in the header) so the linker can correctly place the
// literal pool for IRAM functions in the same section as the code.
// Declared extern in InputMap.h so InputMap methods can read them.
DRAM_ATTR volatile int8_t  g_viewportDelta = 0;
DRAM_ATTR volatile bool    g_reCenterView  = false;

// ── Trackball ISRs ────────────────────────────────────────────────────────────
// Defined in .cpp (not inline in header) — required for correct IRAM literal
// pool placement on Xtensa. Bodies are intentionally minimal.

void IRAM_ATTR inputmap_isr_tbUp()    { g_viewportDelta--; }
void IRAM_ATTR inputmap_isr_tbDown()  { g_viewportDelta++; }
void IRAM_ATTR inputmap_isr_tbLeft()  { }
void IRAM_ATTR inputmap_isr_tbRight() { }
void IRAM_ATTR inputmap_isr_tbPress() { g_reCenterView = true; }
