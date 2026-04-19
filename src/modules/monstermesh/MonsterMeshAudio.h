#pragma once
#include <Arduino.h>
#include <driver/i2s.h>

// Use 16-bit signed samples for I2S output
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#define AUDIO_SAMPLE_RATE 32768
#include "minigb_apu.h"

// Free functions called by peanut_gb when ENABLE_SOUND=1
// These must be declared before peanut_gb.h is included.
#ifdef __cplusplus
extern "C" {
#endif
uint8_t audio_read(const uint16_t addr);
void    audio_write(const uint16_t addr, const uint8_t val);
#ifdef __cplusplus
}
#endif

class MonsterMeshAudio {
public:
    bool begin();
    void stop();

    // Call once per emulator frame to generate and queue audio samples
    void processFrame();

    // Volume: 0 = mute, 8 = max
    void setVolume(uint8_t vol) { volume_ = vol > 8 ? 8 : vol; }
    uint8_t volume() const { return volume_; }

    // Mute toggle
    void setMuted(bool m) { muted_ = m; }
    bool isMuted() const { return muted_; }

    // Access to APU context (used by audio_read/audio_write free functions)
    struct minigb_apu_ctx *apuCtx() { return &apuCtx_; }

    static MonsterMeshAudio *instance() { return instance_; }

private:
    struct minigb_apu_ctx apuCtx_;
    bool running_ = false;
    bool muted_ = false;
    uint8_t volume_ = 4;  // default mid volume
    i2s_port_t port_ = I2S_NUM_0;

    // Audio sample buffer (stereo interleaved)
    int16_t sampleBuf_[AUDIO_SAMPLES_TOTAL];

    static MonsterMeshAudio *instance_;
};
