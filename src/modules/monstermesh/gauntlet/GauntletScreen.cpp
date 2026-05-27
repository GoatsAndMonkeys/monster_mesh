// SPDX-License-Identifier: MIT
// See GauntletScreen.h.

#include "GauntletScreen.h"
#include "GauntletModule.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GAUNTLET

#include <OLEDDisplay.h>

#if HAS_NETWORKING
#include "mqtt/MQTT.h"
#endif

#ifdef ARCH_ESP32
#include <WiFi.h>
#endif

void GauntletScreen::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                                int16_t x, int16_t y)
{
    (void)state;
    if (!display) return;

    display->setFont(ArialMT_Plain_10);

    if (!gauntletModule) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + 64, y + 26, "Gym offline");
        return;
    }

    const GauntletState &gs = gauntletModule->state();

    // ── Title bar ────────────────────────────────────────────────────────────
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(x + 64, y + 0, gs.gymName);
    display->drawHorizontalLine(x + 0, y + 11, 128);

    // ── Body ─────────────────────────────────────────────────────────────────
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    char line[40];

    if (gs.leader.nodeNum)
        snprintf(line, sizeof(line), "Ldr: %.16s", gs.leader.name);
    else
        snprintf(line, sizeof(line), "Open - claim it!");
    display->drawString(x + 2, y + 13, line);

    snprintf(line, sizeof(line), "Rank: %u trainers", (unsigned)gs.rosterSize);
    display->drawString(x + 2, y + 24, line);

    snprintf(line, sizeof(line), "Battles: %lu", (unsigned long)gs.totalBattles);
    display->drawString(x + 2, y + 35, line);

    // ── Network status (MQTT/WiFi) ───────────────────────────────────────────
    const char *netState = "n/a";
#if HAS_NETWORKING
    if (mqtt && mqtt->isConnectedDirectly())          netState = "MQTT online";
    else
#endif
#ifdef ARCH_ESP32
    if (WiFi.status() == WL_CONNECTED)                netState = "WiFi only";
    else                                              netState = "offline";
#else
    {}
#endif
    snprintf(line, sizeof(line), "%s", netState);
    display->drawString(x + 2, y + 46, line);

    // ── Badge name (bottom-right corner) ─────────────────────────────────────
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(x + 126, y + 53, gs.gymBadge);
}

#else // MESHTASTIC_EXCLUDE_GAUNTLET

#include <OLEDDisplay.h>
void GauntletScreen::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *,
                                int16_t x, int16_t y)
{
    if (!display) return;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(x + 64, y + 26, "Gauntlet disabled");
}

#endif // !MESHTASTIC_EXCLUDE_GAUNTLET
