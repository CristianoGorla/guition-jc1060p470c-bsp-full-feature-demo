/**
 * @file sd_card_manager.h
 * @brief SD Card manager for bootstrap system
 * 
 * Provides safe SD card mounting that reuses SDMMC host initialized
 * by WiFi ESP-Hosted (Phase B), preventing bus conflicts.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#ifndef __SD_CARD_MANAGER_H__
#define __SD_CARD_MANAGER_H__

#include "esp_err.h"
#include "sdmmc_cmd.h"

/**
 * @brief Mount SD card filesystem safely (Phase C of bootstrap)
 * 
 * This function mounts the SD card FAT filesystem while reusing the
 * SDMMC host controller already initialized by WiFi ESP-Hosted in Phase B.
 * 
 * Key differences from standard mount:
 * - Uses dummy host init/deinit functions
 * - Assumes SDMMC controller is already configured
 * - Configures Slot 0 pins but skips host initialization
 * - Enables internal pull-ups on SDMMC data lines
 * 
 * Prerequisites:
 * - bootstrap_manager Phase B must be complete (HOSTED_READY event)
 * - SDMMC Slot 1 must be active (ESP-Hosted SDIO)
 * 
 * @param[out] out_card Pointer to receive SD card handle
 * @return ESP_OK on success, error code otherwise
 * 
 * @note This function is only safe to call after WiFi Hosted transport
 *       is initialized. Calling it before will cause initialization errors.
 */
esp_err_t sd_card_mount_safe(sdmmc_card_t **out_card);

/**
 * @brief Unmount SD card filesystem
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_unmount(void);

/**
 * @brief Check if SD card is mounted
 * 
 * @return true if mounted, false otherwise
 */
bool sd_card_is_mounted(void);

/**
 * @brief Get SD card handle
 * 
 * @return Pointer to SD card handle, or NULL if not mounted
 */
sdmmc_card_t* sd_card_get_handle(void);

#endif // __SD_CARD_MANAGER_H__
