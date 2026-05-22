#include "MonsterMeshAudio.h"
#include "variant.h"

MonsterMeshAudio *MonsterMeshAudio::instance_ = nullptr;

// ── Free functions for peanut_gb ─────────────────────────────────────────────
// peanut_gb calls these when ENABLE_SOUND=1

uint8_t audio_read(const uint16_t addr) {
    MonsterMeshAudio *a = MonsterMeshAudio::instance();
    if (a) return minigb_apu_audio_read(a->apuCtx(), addr);
    return 0xFF;
}

void audio_write(const uint16_t addr, const uint8_t val) {
    MonsterMeshAudio *a = MonsterMeshAudio::instance();
    if (a) minigb_apu_audio_write(a->apuCtx(), addr, val);
}

// ── MonsterMeshAudio implementation ──────────────────────────────────────────

bool MonsterMeshAudio::begin() {
    if (running_) return true;
    instance_ = this;

    // Initialize APU
    memset(&apuCtx_, 0, sizeof(apuCtx_));
    minigb_apu_audio_init(&apuCtx_);

    // Tear down any pre-existing I2S driver. Meshtastic's notification
    // AudioThread uses I2S_NUM_1 routed to the SAME GPIO pins as us
    // (DAC_I2S_BCK/WS/DOUT). When a DM ringtone plays, the ESP32 GPIO
    // matrix gets repointed at port 1; our subsequent port 0 install
    // succeeds but the pins still drive port 1 → silent emu after
    // terminal use. Uninstalling BOTH ports frees the pins so our
    // i2s_set_pin claim below actually takes effect.
    //
    // NOTE: do NOT add i2s_stop / vTaskDelay around these — that combo
    // crashed the device on ROM-load-after-battle on b387 (see logs:
    // device reset right after "save loaded" line, before any further
    // emulator log). driver_uninstall is sufficient.
    i2s_driver_uninstall(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_1);

    // Configure I2S for audio output
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = AUDIO_SAMPLE_RATE;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 16;
    i2s_config.dma_buf_len = 1024;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] i2s_driver_install failed: %d\n", err);
        return false;
    }

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = DAC_I2S_BCK;
    pin_config.ws_io_num = DAC_I2S_WS;
    pin_config.data_out_num = DAC_I2S_DOUT;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;
#ifdef DAC_I2S_MCLK
    pin_config.mck_io_num = DAC_I2S_MCLK;
#endif

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] i2s_set_pin failed: %d\n", err);
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }

    i2s_zero_dma_buffer(I2S_NUM_0);

    running_ = true;
    // Defensive: ensure the freshly-started audio is unmuted. The mic-button
    // toggleMute() path (RAW-mode kb poll) can fire on an early bus glitch
    // right at emulator start, which used to leave the player muted with no
    // obvious indication.
    muted_ = false;
    Serial.printf("[AUDIO] started: %dHz, 16-bit stereo, vol=%u mute=0\n",
                  AUDIO_SAMPLE_RATE, (unsigned)volume_);
    return true;
}

void MonsterMeshAudio::stop() {
    if (!running_) return;
    running_ = false;
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_0);
    instance_ = nullptr;
    Serial.println("[AUDIO] stopped");
}

void MonsterMeshAudio::processFrame() {
    if (!running_) return;

    // Generate one frame's worth of audio samples from the APU
    minigb_apu_audio_callback(&apuCtx_, sampleBuf_);

    if (muted_ || volume_ == 0) {
        // Write silence
        memset(sampleBuf_, 0, sizeof(sampleBuf_));
    } else if (volume_ < 8) {
        // Apply volume by right-shifting (volume 8 = full, 4 = half, 1 = 1/8)
        uint8_t shift = 8 - volume_;
        for (unsigned i = 0; i < AUDIO_SAMPLES_TOTAL; i++) {
            sampleBuf_[i] >>= shift;
        }
    }

    // Write to I2S DMA — non-blocking with short timeout to avoid stalling emulator
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, sampleBuf_, sizeof(sampleBuf_),
              &bytes_written, pdMS_TO_TICKS(20));
}
