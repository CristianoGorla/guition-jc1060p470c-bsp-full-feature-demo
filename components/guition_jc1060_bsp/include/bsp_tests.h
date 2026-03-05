/**
 * @file bsp_tests.h
 * @brief Hardware testing service for Guition BSP
 *
 * Provides optional hardware validation tests for development and debugging.
 * Tests are only compiled and executed when CONFIG_BSP_ENABLE_DEBUG_MODE is enabled.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run all enabled hardware tests
 *
 * Executes tests for all hardware peripherals that are:
 * 1. Enabled in hardware configuration (BSP_ENABLE_xxx)
 * 2. Have their specific test enabled (APP_ENABLE_xxx_TEST)
 * 3. Debug mode is active (BSP_ENABLE_DEBUG_MODE)
 *
 * Tests include:
 * - RTC: Read-only register test
 * - SD Card: Card info display
 * - WiFi: Network scan
 *
 * @return
 *      - ESP_OK if all enabled tests passed
 *      - ESP_ERR_NOT_SUPPORTED if debug mode is disabled
 *      - ESP_FAIL if any test failed
 *
 * @note This function does nothing if CONFIG_BSP_ENABLE_DEBUG_MODE is disabled
 * @note Call after bsp_board_init() and before entering main loop
 */
esp_err_t bsp_run_hardware_tests(void);

#ifdef __cplusplus
}
#endif
