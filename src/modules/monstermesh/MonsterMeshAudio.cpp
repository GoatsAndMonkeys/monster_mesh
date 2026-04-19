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

    // Configure I2S for audio output
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = AUDIO_SAMPLE_RATE;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 8;
    i2s_config.dma_buf_len = 512;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;

    // Try I2S_NUM_0 first; fall back to I2S_NUM_1 if already claimed by AudioModule
    i2s_port_t port = I2S_NUM_0;
    esp_err_t err = i2s_driver_install(port, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] I2S_NUM_0 unavailable (%d), trying NUM_1\n", err);
        port = I2S_NUM_1;
        err = i2s_driver_install(port, &i2s_config, 0, NULL);
    }
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] i2s_driver_install failed: %d\n", err);
        return false;
    }
    port_ = port;

    // MAX98357A amp does not use MCLK — leave mck_io_num as NO_CHANGE.
    // DAC_I2S_MCLK (GPIO 21) is shared with the ES7210 mic LRCLK; driving it
    // as MCLK output causes a conflict and produces no audio.
    i2s_pin_config_t pin_config = {};
    pin_config.mck_io_num = I2S_PIN_NO_CHANGE;
    pin_config.bck_io_num = DAC_I2S_BCK;
    pin_config.ws_io_num = DAC_I2S_WS;
    pin_config.data_out_num = DAC_I2S_DOUT;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(port_, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] i2s_set_pin failed: %d\n", err);
        i2s_driver_uninstall(port_);
        return false;
    }

    i2s_zero_dma_buffer(port_);

    running_ = true;
    Serial.printf("[AUDIO] started on I2S_NUM_%d: %dHz, 16-bit stereo\n",
                  (int)port_, AUDIO_SAMPLE_RATE);
    return true;
}

void MonsterMeshAudio::stop() {
    if (!running_) return;
    running_ = false;
    i2s_zero_dma_buffer(port_);
    i2s_driver_uninstall(port_);
    instance_ = nullptr;
    Serial.println("[AUDIO] stopped");
}

void MonsterMeshAudio::processFrame() {
    if (!running_) return;

    // Generate one frame's worth of audio samples from the APU
    minigb_apu_audio_callback(&apuCtx_, sampleBuf_);

    if (muted_ || volume_ == 0) {
        memset(sampleBuf_, 0, sizeof(sampleBuf_));
    } else if (volume_ < 8) {
        uint8_t shift = 8 - volume_;
        for (unsigned i = 0; i < AUDIO_SAMPLES_TOTAL; i++) {
            sampleBuf_[i] >>= shift;
        }
    }

    size_t bytes_written = 0;
    i2s_write(port_, sampleBuf_, sizeof(sampleBuf_),
              &bytes_written, pdMS_TO_TICKS(20));
}
