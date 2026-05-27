// SPDX-License-Identifier: MIT
//
// GauntletMQTT — publishes the gym's public state to the Meshtastic MQTT
// broker so a central dashboard can aggregate all gym nodes.
//
// Reuses the existing Meshtastic `mqtt` global (no second WiFi client).
// Publish-only: cross-gym aggregation happens at the broker / dashboard side,
// not on-device. Each gym node owns its own retained `state` topic.
//
// Topics (retained=true on state; retained=false on event):
//   msh-gym/<nodeHex>/state  — retained snapshot (published on every change)
//   msh-gym/<nodeHex>/event  — non-retained one-shot events
//
// Both default-disabled. Enable per-build with -DGAUNTLET_MQTT_ENABLE=1
// (the user's MQTT module config still has to be ON for anything to flow).

#pragma once
#include "GauntletData.h"

// Publish a full snapshot of the gym state to msh-gym/<nodeHex>/state.
// No-op if Meshtastic MQTT isn't connected, gauntlet MQTT is disabled, or
// the firmware was built without networking.
//
// Returns true on a successful enqueue/publish, false if it was skipped.
bool gauntletMQTTPublishState(const GauntletState &s);

// Publish a one-shot non-retained event ("leader_change", "roster", "battle").
// payloadJson must be a valid JSON object literal — it is wrapped into the
// envelope `{"type":"<eventType>","gym":"<gymName>","ts":<unix>,"data":<payloadJson>}`.
bool gauntletMQTTPublishEvent(const GauntletState &s,
                               const char *eventType,
                               const char *payloadJson);
