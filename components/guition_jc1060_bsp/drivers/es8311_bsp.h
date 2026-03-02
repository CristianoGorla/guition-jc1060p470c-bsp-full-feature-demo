/**
 * @file es8311_bsp.h
 * @brief ES8311 Audio Codec + NS4150 Power Amplifier Driver for BSP
 * 
 * Hardware Configuration:
 * - Codec: ES8311 (I2C address 0x18)
 * - Amplifier: NS4150 (controlled via GPIO 11)
 * - I2S Interface:
 *   - MCLK: GPIO 9
 *   - SCLK: GPIO 13  (I2S_BCK)
 *   - LRCK: GPIO 12  (I2S_WS)
 *   - DSDIN: GPIO 10 (I2S_DOUT - to codec)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Audio configuration structure
 */
typedef struct {
    uint32_t sample_rate;    /* Sample rate in Hz (e.g., 44100, 48000) */
    uint8_t bits_per_sample; /* Bits per sample (16, 24, or 32) */
    bool enable_pa;          /* Enable NS4150 power amplifier */
} bsp_audio_config_t;

/**
 * @brief Default audio configuration
 */
#define BSP_AUDIO_DEFAULT_CONFIG() { \
    .sample_rate = 48000, \
    .bits_per_sample = 16, \
    .enable_pa = true \
}

/**
 * @brief Initialize ES8311 audio codec and NS4150 amplifier
 * 
 * This function configures:
 * - I2S interface for audio data
 * - ES8311 codec via I2C
 * - NS4150 power amplifier enable pin
 * 
 * @param config Audio configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t bsp_audio_init(const bsp_audio_config_t *config);

/**
 * @brief Enable/disable NS4150 power amplifier
 * 
 * @param enable true to enable PA, false to disable
 * @return ESP_OK on success
 */
esp_err_t bsp_audio_set_pa_enable(bool enable);

/**
 * @brief Set audio codec volume
 * 
 * @param volume Volume level 0-100%
 * @return ESP_OK on success
 */
esp_err_t bsp_audio_set_volume(uint8_t volume);

#ifdef __cplusplus
}
#endif
