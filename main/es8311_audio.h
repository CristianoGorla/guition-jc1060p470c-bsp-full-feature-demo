#ifndef ES8311_AUDIO_H
#define ES8311_AUDIO_H

#include "driver/i2c_master.h"
#include "esp_err.h"

// ES8311 I2C Address
#define ES8311_I2C_ADDR 0x18

// ES8311 Register Map (basic)
#define ES8311_RESET_REG        0x00
#define ES8311_CLK_MANAGER_REG  0x01
#define ES8311_SYSTEM_REG       0x0D
#define ES8311_CHIP_ID_REG      0xFD

/**
 * @brief Initialize ES8311 audio codec
 * 
 * @param bus_handle I2C master bus handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t es8311_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Read ES8311 chip ID
 * 
 * @param bus_handle I2C master bus handle
 * @param chip_id Pointer to store chip ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t es8311_read_chip_id(i2c_master_bus_handle_t bus_handle, uint8_t *chip_id);

#endif // ES8311_AUDIO_H
