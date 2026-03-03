/**
 * @file jd9165_bsp.h
 * @brief JD9165 MIPI-DSI Display Driver for BSP
 * 
 * Hardware Configuration:
 * - Display: 1024x600 RGB565
 * - Interface: MIPI DSI 2-lane @ 750 Mbps
 * - Pixel Clock: 51.2 MHz (effective)
 * - Backlight: PWM on GPIO 23
 * - Reset: GPIO 0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_lcd_panel_ops.h"
#include "esp_err.h"

/**
 * @brief Initialize JD9165 display driver
 * 
 * This function initializes the complete display subsystem:
 * - MIPI DSI PHY power (LDO channel 3 @ 2.5V)
 * - 2-lane DSI bus @ 750 Mbps
 * - Display panel with init sequence
 * - Backlight PWM controller
 * 
 * @return LCD panel handle on success, NULL on failure
 */
esp_lcd_panel_handle_t bsp_display_init(void);

/**
 * @brief Get the LCD panel handle
 * 
 * Returns the handle to the initialized LCD panel.
 * This allows applications to use LCD panel operations directly.
 * 
 * @return LCD panel handle if display is initialized, NULL otherwise
 */
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void);

/**
 * @brief Set display backlight brightness
 * 
 * @param brightness_percent Brightness level 0-100%
 * @return ESP_OK on success
 */
esp_err_t bsp_display_set_brightness(uint8_t brightness_percent);

#ifdef __cplusplus
}
#endif
