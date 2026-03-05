/**
 * @file gt911_bsp.h
 * @brief GT911 Capacitive Touch Controller Driver for BSP
 * 
 * Hardware Configuration:
 * - I2C Address: 0x14 (forced via reset sequence)
 * - Reset GPIO: 21
 * - Interrupt GPIO: disabled at runtime (polling mode)
 * - Touch Points: Up to 5 simultaneous touches
 * - Resolution: 1024x600 (matches display)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_lcd_touch.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

/*
 * GT911 GPIO mapping
 * - Reset pin is actively used during initialization
 * - Interrupt line is disabled for runtime polling compatibility with LVGL
 */
#define BSP_GT911_RST_GPIO       ((gpio_num_t)CONFIG_BSP_PIN_TOUCH_RST)
#define BSP_GT911_INT_GPIO       GPIO_NUM_NC
#define BSP_GT911_RESET_INT_GPIO ((gpio_num_t)CONFIG_BSP_PIN_TOUCH_INT)

/**
 * @brief Initialize GT911 touch controller
 * 
 * This function performs:
 * - Proper reset sequence to force I2C address to 0x14
 * - Touch controller initialization
 * - Polling-mode touch configuration (LVGL compatible)
 * 
 * @note Requires I2C bus to be initialized first (bsp_i2c_init)
 * @note CRITICAL: No I2C bus scan must occur before this function,
 *                 as it would interfere with the address selection sequence
 * 
 * @return Touch panel handle on success, NULL on failure
 */
esp_lcd_touch_handle_t bsp_touch_init(void);

/**
 * @brief Re-execute GT911 reset sequence to restore I2C address
 * 
 * Use this function after I2C bus recovery to reconfigure GT911 address to 0x14.
 * 
 * WHEN TO USE:
 * - After I2C bus re-initialization (e.g., post-MIPI-DSI recovery)
 * - When GT911 stops responding on expected address
 * 
 * RESET SEQUENCE:
 * 1. INT = LOW
 * 2. RST = LOW (10ms)
 * 3. RST = HIGH (5ms)
 * 4. INT = INPUT with pullup
 * 5. Wait 50ms for GT911 init
 * 
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_reset(void);

#ifdef __cplusplus
}
#endif
