/*
 * I2C Test Utilities - Targeted peripheral testing
 * Documented in: I2C_MIPI_DSI_CONFLICT.md
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check I2C GPIO state (detects MIPI-DSI conflict)
 * 
 * Tests if SDA and SCL lines are HIGH with pullups enabled.
 * Used to detect if MIPI-DSI display has disrupted I2C bus.
 * 
 * See: docs/I2C_MIPI_DSI_CONFLICT.md
 * 
 * @param context Context string for logging (e.g. "after display init")
 * @return true if both SDA and SCL are HIGH (healthy)
 */
bool i2c_check_gpio_state(const char *context);

/**
 * @brief Re-initialize I2C bus after disruption
 * 
 * Recovery sequence:
 *   1. Delete existing I2C bus
 *   2. Reset GPIO pins
 *   3. Re-create I2C bus with Kconfig settings
 *   4. Verify GPIO state
 * 
 * See: docs/I2C_MIPI_DSI_CONFLICT.md
 * 
 * @param bus_handle Pointer to I2C bus handle
 * @return ESP_OK on success
 */
esp_err_t i2c_reinit_bus(i2c_master_bus_handle_t *bus_handle);

/**
 * @brief Test all configured I2C peripherals
 * 
 * Tests only peripherals enabled in Kconfig:
 *   - GT911 Touch (0x14) if CONFIG_BSP_ENABLE_TOUCH
 *   - ES8311 Audio (0x18) if CONFIG_BSP_ENABLE_AUDIO
 *   - RX8025T RTC (0x32) if CONFIG_BSP_ENABLE_RTC
 * 
 * Uses targeted probing, NOT generic scan (safe for production).
 * 
 * @param bus_handle I2C bus handle
 */
void i2c_test_peripherals(i2c_master_bus_handle_t bus_handle);

#ifdef __cplusplus
}
#endif
