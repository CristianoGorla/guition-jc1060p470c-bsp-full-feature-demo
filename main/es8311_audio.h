#ifndef ES8311_AUDIO_H
#define ES8311_AUDIO_H

#include "driver/i2c_master.h"

/*
 * ES8311 Audio Codec Driver
 * 
 * Hardware Configuration (Guition JC1060P470C):
 * - I2C Address: 0x18 (7-bit)
 * - I2C Bus: GPIO7 (SDA), GPIO8 (SCL) - shared with GT911 and RX8025T
 * - PA Power: GPIO11 (NS4150B amplifier enable, active HIGH)
 * 
 * This is a minimal I2C-only driver for ES8311 initialization.
 * For full audio functionality, consider using:
 *   idf.py add-dependency "espressif/esp_codec_dev^1.5.4"
 * 
 * Official BSP component provides:
 * - I2S data interface setup
 * - Volume control
 * - Sample rate configuration
 * - Microphone/Speaker routing
 * - PA power management
 * 
 * References:
 * - https://components.espressif.com/components/espressif/es8311
 * - https://components.espressif.com/components/espressif/esp_codec_dev
 * - Datasheet: http://www.everest-semi.com/pdf/ES8311%20PB.pdf
 */

#define ES8311_I2C_ADDR    0x18
#define ES8311_CHIP_ID_REG 0xFD
#define ES8311_RESET_REG   0x00
#define ES8311_SYSTEM_REG  0x0D

/**
 * @brief Initialize ES8311 audio codec (I2C only, minimal setup)
 * 
 * This function:
 * 1. Validates device presence by reading chip ID (direct init, no probe)
 * 2. Performs soft reset
 * 3. Sets codec to power-down mode (safe state)
 * 
 * For audio playback/recording, additional setup required:
 * - I2S peripheral configuration
 * - PA power pin (GPIO11) control
 * - Clock configuration (MCLK)
 * 
 * @param bus_handle I2C master bus handle
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t es8311_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Read ES8311 chip ID register
 * 
 * @param bus_handle I2C master bus handle
 * @param chip_id Output: chip ID (expected 0x83)
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t es8311_read_chip_id(i2c_master_bus_handle_t bus_handle, uint8_t *chip_id);

#endif // ES8311_AUDIO_H
