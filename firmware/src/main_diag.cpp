// Diagnostic build — LovyanGFX display + ESP-IDF SD on shared SPI2
#include <Arduino.h>
#include "Display.h"
#include "pins.h"

// ESP-IDF SD card (bypass broken Arduino SPIClass)
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static LGFX_TDeck tft;
static sdmmc_card_t *sdCard = nullptr;

// No-op: bus already initialized by LovyanGFX
static esp_err_t spi_noop(void) { return ESP_OK; }

static bool mountSD() {
    // LovyanGFX already initialized SPI2_HOST bus.
    // We just add an SD device to the existing bus.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 400;  // start slow for init
    host.init = &spi_noop;    // don't re-init bus

    sdspi_device_config_t devCfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    devCfg.host_id = SPI2_HOST;
    devCfg.gpio_cs = (gpio_num_t)PIN_SD_CS;

    esp_vfs_fat_sdmmc_mount_config_t mountCfg = {};
    mountCfg.format_if_mount_failed = false;
    mountCfg.max_files = 5;
    mountCfg.allocation_unit_size = 16 * 1024;

    Serial.printf("[SD] Mounting on SPI2 (CS=%d)...\n", PIN_SD_CS);
    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sd", &host, &devCfg, &mountCfg, &sdCard);
    Serial.printf("[SD] Result: %s (0x%x)\n", esp_err_to_name(ret), ret);

    if (ret == ESP_OK) {
        sdmmc_card_print_info(stdout, sdCard);
        return true;
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[DIAG] 1: Serial OK");

    pinMode(PIN_KB_POWERON, OUTPUT);
    digitalWrite(PIN_KB_POWERON, HIGH);
    Serial.println("[DIAG] 2: KB power OK");

    // Deselect LoRa so it doesn't interfere on shared bus
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);

    // ── Display ─────────────────────────────────────────────────
    tft.init();
    tft.setRotation(1);
    tft.setBrightness(200);
    tft.fillScreen(TFT_BLACK);
    tft.endWrite();  // release SPI2 bus
    Serial.println("[DIAG] 3: TFT OK");

    // ── SD card ─────────────────────────────────────────────────
    Serial.println("[DIAG] 4: SD init...");
    bool sdOK = mountSD();
    Serial.printf("[DIAG] 4: SD = %s\n", sdOK ? "OK" : "FAILED");

    // ── Draw status ─────────────────────────────────────────────
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("MonsterMesh", DISP_W / 2, DISP_H / 2 - 30);
    tft.setTextSize(1);
    tft.drawString("v0.0.10 mesh-pre-alpha", DISP_W / 2, DISP_H / 2);

    if (sdOK) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("SD: OK", DISP_W / 2, DISP_H / 2 + 20);

        FILE *f = fopen("/sd/pokemon.gb", "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fclose(f);
            char buf[40];
            snprintf(buf, sizeof(buf), "ROM: %ld KB", sz / 1024);
            tft.drawString(buf, DISP_W / 2, DISP_H / 2 + 35);
            Serial.printf("[DIAG] ROM: %ld bytes\n", sz);
        } else {
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString("ROM: missing", DISP_W / 2, DISP_H / 2 + 35);
        }
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("SD: FAILED", DISP_W / 2, DISP_H / 2 + 20);
    }
    tft.endWrite();

    Serial.println("[DIAG] === DONE ===");
}

void loop() {
    delay(1000);
    Serial.println("[DIAG] alive");
}
