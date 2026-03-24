#pragma once
#include <Arduino.h>
#include <atomic>
#include "pins.h"

// ── AlertDriver ──────────────────────────────────────────────────────────────
// Non-blocking tone sequencer using ESP32-S3 LEDC PWM on a piezo buzzer.
// Thread-safe: play*() can be called from any core (sets atomic pending alert).
// tick() must be called every frame from emuTask (Core 1) to drive note output.

class AlertDriver {
public:
    void begin() {
        ledcSetup(LEDC_CH_BUZZER, 1000, 8);            // default 1kHz, 8-bit res
        ledcAttachPin(PIN_BUZZER, LEDC_CH_BUZZER);     // attach buzzer pin
        ledcWriteTone(LEDC_CH_BUZZER, 0);              // silence
    }

    // ── Trigger alerts (safe from any core) ──────────────────────────────────
    void playChallenge() { pendingAlert_.store(static_cast<uint8_t>(Alert::CHALLENGE)); }
    void playAccepted()  { pendingAlert_.store(static_cast<uint8_t>(Alert::ACCEPTED)); }
    void playRejected()  { pendingAlert_.store(static_cast<uint8_t>(Alert::REJECTED)); }
    void silence()       { pendingAlert_.store(static_cast<uint8_t>(Alert::SILENCE)); }

    // ── Mute toggle ──────────────────────────────────────────────────────────
    void toggleMute() { muted_ = !muted_; }
    bool isMuted() const { return muted_; }

    // ── Frame tick (call from Core 1 emuTask) ────────────────────────────────
    void tick(uint32_t now) {
        // Check for new alert request
        Alert pending = static_cast<Alert>(pendingAlert_.exchange(static_cast<uint8_t>(Alert::NONE)));
        if (pending == Alert::SILENCE) {
            stopTone();
            seqLen_ = 0;
            return;
        }
        if (pending != Alert::NONE && !muted_) {
            startSequence(pending, now);
        }

        // Drive active sequence
        if (seqLen_ == 0) return;

        if (now - noteStartMs_ >= seq_[seqIdx_].durationMs) {
            seqIdx_++;
            if (seqIdx_ >= seqLen_) {
                stopTone();
                seqLen_ = 0;
                return;
            }
            playNote(seq_[seqIdx_].freqHz);
            noteStartMs_ = now;
        }
    }

private:
    struct Note {
        uint16_t freqHz;
        uint16_t durationMs;
    };

    enum class Alert : uint8_t {
        NONE, CHALLENGE, ACCEPTED, REJECTED, SILENCE
    };

    std::atomic<uint8_t> pendingAlert_{static_cast<uint8_t>(Alert::NONE)};
    bool               muted_ = false;

    // Active sequence state (Core 1 only — no mutex needed)
    Note     seq_[4]     = {};
    uint8_t  seqLen_     = 0;
    uint8_t  seqIdx_     = 0;
    uint32_t noteStartMs_ = 0;

    void startSequence(Alert alert, uint32_t now) {
        static const Note SEQ_CHALLENGE[] = {
            {880,  80}, {0, 40}, {1320, 120}   // ascending chirp
        };
        static const Note SEQ_ACCEPTED[] = {
            {660,  60}, {880, 60}, {1320, 100}  // happy arpeggio
        };
        static const Note SEQ_REJECTED[] = {
            {440, 100}, {330, 150}              // descending boop
        };

        const Note *src = nullptr;
        uint8_t len = 0;
        switch (alert) {
            case Alert::CHALLENGE:
                src = SEQ_CHALLENGE; len = sizeof(SEQ_CHALLENGE) / sizeof(Note); break;
            case Alert::ACCEPTED:
                src = SEQ_ACCEPTED;  len = sizeof(SEQ_ACCEPTED) / sizeof(Note);  break;
            case Alert::REJECTED:
                src = SEQ_REJECTED;  len = sizeof(SEQ_REJECTED) / sizeof(Note);  break;
            default: return;
        }
        memcpy(seq_, src, len * sizeof(Note));
        seqLen_ = len;
        seqIdx_ = 0;
        noteStartMs_ = now;
        playNote(seq_[0].freqHz);
    }

    void playNote(uint16_t freqHz) {
        if (freqHz == 0) {
            ledcWriteTone(LEDC_CH_BUZZER, 0);
        } else {
            ledcWriteTone(LEDC_CH_BUZZER, freqHz);
        }
    }

    void stopTone() {
        ledcWriteTone(LEDC_CH_BUZZER, 0);
    }
};

