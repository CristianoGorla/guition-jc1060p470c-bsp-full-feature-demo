/*
 * SDMMC Bus Arbiter
 * 
 * Manages exclusive access to SDMMC Slot 0 between:
 * - ESP-Hosted WiFi transport (SDIO slave mode)
 * - SD card filesystem (SD card host mode)
 * 
 * Hardware constraint: ESP32-P4 SDMMC controller cannot operate
 * both modes simultaneously on the same slot.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#ifndef __SDMMC_ARBITER_H__
#define __SDMMC_ARBITER_H__

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SDMMC Bus Operating Modes
 */
typedef enum {
    SDMMC_MODE_NONE = 0,      // Bus idle (no active mode)
    SDMMC_MODE_WIFI,          // ESP-Hosted WiFi (SDIO slave)
    SDMMC_MODE_SD_CARD        // SD card filesystem (SD host)
} sdmmc_bus_mode_t;

/**
 * SDMMC Arbiter State
 */
typedef struct {
    SemaphoreHandle_t mutex;              // Mutual exclusion lock
    sdmmc_bus_mode_t current_mode;        // Active bus mode
    sdmmc_card_t *sd_card;                // SD card handle (when in SD mode)
    bool wifi_transport_active;           // ESP-Hosted initialized flag
    uint32_t mode_switch_count;           // Debug counter
} sdmmc_arbiter_t;

/**
 * Initialize SDMMC arbiter
 * 
 * Creates mutex and loads persistent mode from NVS.
 * Must be called before bootstrap manager starts.
 * 
 * @return ESP_OK on success
 */
esp_err_t sdmmc_arbiter_init(void);

/**
 * Request WiFi mode (ESP-Hosted)
 * 
 * Switches bus to SDIO slave mode if not already active.
 * Blocks if SD card mode is active until released.
 * 
 * @param timeout_ms  Maximum wait time for mode switch
 * @return ESP_OK if WiFi mode granted
 *         ESP_ERR_TIMEOUT if SD card mode locked
 */
esp_err_t sdmmc_arbiter_request_wifi(uint32_t timeout_ms);

/**
 * Request SD card mode
 * 
 * Switches bus to SD card host mode if not already active.
 * Deinitializes ESP-Hosted transport if needed.
 * Blocks if WiFi mode is active until released.
 * 
 * @param timeout_ms  Maximum wait time for mode switch
 * @param[out] card   SD card handle (allocated by arbiter)
 * @return ESP_OK if SD mode granted
 *         ESP_ERR_TIMEOUT if WiFi mode locked
 */
esp_err_t sdmmc_arbiter_request_sd_card(uint32_t timeout_ms, sdmmc_card_t **card);

/**
 * Release WiFi mode
 * 
 * Deinitializes ESP-Hosted transport and releases bus.
 * Other tasks can now request SD card mode.
 * 
 * @return ESP_OK on success
 */
esp_err_t sdmmc_arbiter_release_wifi(void);

/**
 * Release SD card mode
 * 
 * Unmounts SD card and releases bus.
 * Other tasks can now request WiFi mode.
 * 
 * @return ESP_OK on success
 */
esp_err_t sdmmc_arbiter_release_sd_card(void);

/**
 * Get current bus mode (non-blocking)
 * 
 * @return Current SDMMC bus mode
 */
sdmmc_bus_mode_t sdmmc_arbiter_get_mode(void);

/**
 * Save current mode to NVS (persistent across reboots)
 * 
 * @return ESP_OK on success
 */
esp_err_t sdmmc_arbiter_save_mode(void);

/**
 * Load preferred mode from NVS
 * 
 * @param[out] mode  Loaded mode (SDMMC_MODE_WIFI if not set)
 * @return ESP_OK on success
 */
esp_err_t sdmmc_arbiter_load_mode(sdmmc_bus_mode_t *mode);

/**
 * Deinitialize arbiter
 */
void sdmmc_arbiter_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __SDMMC_ARBITER_H__ */
