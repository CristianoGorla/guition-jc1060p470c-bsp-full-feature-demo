/**
 * @file bsp_heartbeat.h
 * @brief System heartbeat monitoring service for Guition BSP
 *
 * Provides periodic logging of system uptime and memory statistics.
 * Useful for debugging hardware responsiveness and detecting memory leaks.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start heartbeat monitoring task
 *
 * Creates a low-priority background task that logs system stats periodically.
 * Interval and priority are configured via Kconfig (BSP_HEARTBEAT_*).
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NO_MEM if task creation fails
 *      - ESP_ERR_INVALID_STATE if already started
 *      - ESP_ERR_NOT_SUPPORTED if CONFIG_BSP_ENABLE_HEARTBEAT is disabled
 *
 * @note Only functional if CONFIG_BSP_ENABLE_HEARTBEAT is enabled in menuconfig
 * @note Call after bsp_board_init() but before entering main loop
 */
esp_err_t bsp_heartbeat_start(void);

/**
 * @brief Stop heartbeat monitoring task
 *
 * Gracefully stops and deletes the heartbeat task.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if not running
 *      - ESP_ERR_NOT_SUPPORTED if CONFIG_BSP_ENABLE_HEARTBEAT is disabled
 *
 * @note Rarely needed - typically heartbeat runs for entire application lifetime
 */
esp_err_t bsp_heartbeat_stop(void);

/**
 * @brief Check if heartbeat is currently running
 *
 * @return true if heartbeat task is active, false otherwise
 */
bool bsp_heartbeat_is_running(void);

#ifdef __cplusplus
}
#endif
