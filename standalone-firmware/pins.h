#pragma once
// LilyGO T-Deck pin assignments
// Source: variants/esp32s3/t-deck/variant.h from Meshtastic firmware

// ── SPI bus (shared: display + SD card + LoRa) ────────────────
#define PIN_SPI_MOSI    41
#define PIN_SPI_MISO    38
#define PIN_SPI_SCK     40

// ── ST7789V display ───────────────────────────────────────────
#define PIN_TFT_CS      12
#define PIN_TFT_DC      11   // data/command
#define PIN_TFT_RST     -1   // no hardware reset pin
#define PIN_TFT_BL      42   // backlight

// ── SD card ───────────────────────────────────────────────────
#define PIN_SD_CS       39

// ── SX1262 LoRa (Phase 3+) ────────────────────────────────────
#define PIN_LORA_CS     9
#define PIN_LORA_RST    17
#define PIN_LORA_IRQ    45   // DIO1
#define PIN_LORA_BUSY   13   // DIO2

// ── I2C keyboard ──────────────────────────────────────────────
#define PIN_I2C_SDA     18
#define PIN_I2C_SCL     8
#define PIN_KB_POWERON  10   // must be HIGH before keyboard responds
#define KB_I2C_ADDR     0x55

// ── Trackball (interrupt-driven, FALLING edge) ────────────────
#define PIN_TB_UP       3
#define PIN_TB_DOWN     15
#define PIN_TB_LEFT     1
#define PIN_TB_RIGHT    2
#define PIN_TB_PRESS    0    // click

// ── Audio I2S (Phase 6) ───────────────────────────────────────
#define PIN_I2S_BCK     7
#define PIN_I2S_WS      5
#define PIN_I2S_DOUT    6

// ── Buzzer (external piezo on GPIO 14) ────────────────────────
#define PIN_BUZZER      14

// ── Battery ADC ───────────────────────────────────────────────
#define PIN_BATTERY     4

// ── LEDC channel assignments ──────────────────────────────────
// Channel 0: TFT backlight (EmulatorApp)
// Channel 1: piezo buzzer  (AlertDriver)
#define LEDC_CH_BACKLIGHT  0
#define LEDC_CH_BUZZER     1
