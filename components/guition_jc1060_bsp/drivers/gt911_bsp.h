/**
 * @file gt911_bsp.h
 * @brief GT911 Capacitive Touch Controller Driver for BSP
 * 
 * Hardware Configuration:
 * - I2C Address: 0x14 (forced via reset sequence)
 * - Reset GPIO: 21
 * - Interrupt GPIO: 22
 * - Touch Points: Up to 5 simultaneous touches
 * - Resolution: 1024x600 (matches display)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_lcd_touch.h"
#include "esp_err.h"

/**
 * @brief Initialize GT911 touch controller
 * 
 * This function performs:
 * - Proper reset sequence to force I2C address to 0x14
 * - Touch controller initialization
 * - Interrupt configuration
 * 
 * @note Requires I2C bus to be initialized first (bsp_i2c_init)
 * @note CRITICAL: No I2C bus scan must occur before this function,
 *                 as it would interfere with the address selection sequence
 * 
 * @return Touch panel handle on success, NULL on failure
 */
esp_lcd_touch_handle_t bsp_touch_init(void);

#ifdef __cplusplus
}
#endif
