/*
 * Bootstrap Manager for Guition JC1060P470C
 * 
 * Sequential initialization (v1.0.0-beta proven approach):
 * - Phase A: Power management (C6 + SD isolation)
 * - Phase B: SD card mount (BEFORE WiFi)
 * - Phase C: WiFi Hosted initialization (AFTER SD)
 * 
 * No FreeRTOS tasks - simple blocking sequential execution.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#ifndef __BOOTSTRAP_MANAGER_H__
#define __BOOTSTRAP_MANAGER_H__

#include "esp_err.h"
#include "sdmmc_cmd.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * GPIO Pin Definitions (Board-Specific)
 */
#define GPIO_C6_CHIP_PU              54  // ESP32-C6 reset (active HIGH)
#define GPIO_SD_POWER_EN             36  // SD card power via MOSFET Q1 (active HIGH)
#define GPIO_C6_IO9_STRAPPING        (-1) // TODO: Map P4 GPIO connected to C6_IO9
#define GPIO_C6_IO2_HANDSHAKE        6   // C6 data ready signal

/**
 * Timing Constants (milliseconds)
 */
#define BOOTSTRAP_POWER_STABILIZATION_MS    100   // VDD rail stabilization
#define BOOTSTRAP_C6_BOOT_TIMEOUT_MS        5000  // Max time for C6 firmware load
#define BOOTSTRAP_WIFI_LINK_STABILIZATION_MS 5000 // ESP-Hosted SDIO link establishment
#define BOOTSTRAP_HARD_RESET_DELAY_MS       500   // Capacitor discharge time

/**
 * Bootstrap Manager State
 */
typedef struct {
    sdmmc_card_t *sd_card;                 // SD card handle (Phase B)
    bool warm_boot_detected;               // True if software reset
    uint32_t boot_timestamp_ms;            // Boot start time
} bootstrap_manager_t;

/**
 * Initialize bootstrap manager and execute three-phase boot sequence
 * 
 * This function executes sequentially (blocking):
 * 1. Initializes SDMMC arbiter
 * 2. Detects warm boot vs cold boot
 * 3. Performs hard reset if needed (warm boot)
 * 4. Executes three phases in order:
 *    - Phase A: Power Management (GPIO isolation + power sequencing)
 *    - Phase B: SD Manager (mounts SD card BEFORE WiFi)
 *    - Phase C: WiFi Hosted (starts after SD ready)
 * 
 * Returns when all three phases complete or on first error.
 * 
 * @param[out] manager  Pointer to bootstrap manager structure
 * @return ESP_OK if all phases completed successfully
 *         Error code on first failure
 */
esp_err_t bootstrap_manager_init(bootstrap_manager_t *manager);

/**
 * Wait for bootstrap completion (dummy for sequential init)
 * 
 * Sequential init completes in bootstrap_manager_init(), so this
 * function always returns immediately.
 * 
 * @param manager     Bootstrap manager instance
 * @param timeout_ms  Ignored (kept for API compatibility)
 * @return ESP_OK (always succeeds)
 */
esp_err_t bootstrap_manager_wait(bootstrap_manager_t *manager, uint32_t timeout_ms);

/**
 * Get SD card handle after bootstrap
 * 
 * @param manager  Bootstrap manager instance
 * @return Pointer to SD card structure, or NULL if not ready
 */
sdmmc_card_t* bootstrap_manager_get_sd_card(bootstrap_manager_t *manager);

/**
 * Clean up bootstrap manager resources
 * 
 * @param manager  Bootstrap manager instance
 */
void bootstrap_manager_deinit(bootstrap_manager_t *manager);

/**
 * Perform hard reset cycle (warm boot recovery)
 * 
 * Forces complete power cycle:
 * 1. C6_CHIP_PU → LOW (hold C6 in reset)
 * 2. SD_POWER_EN → LOW (cut SD card power)
 * 3. Wait 500ms (discharge capacitors)
 * 4. Ready for clean initialization
 * 
 * Call this BEFORE bootstrap_manager_init() if warm boot is suspected.
 */
void bootstrap_hard_reset(void);

/**
 * Check if current boot is warm boot (software reset)
 * 
 * Detection methods:
 * - RTC memory retention
 * - Reset reason (SW reset, watchdog, etc.)
 * - GPIO state analysis
 * 
 * @return true if warm boot detected
 */
bool bootstrap_is_warm_boot(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOTSTRAP_MANAGER_H__ */
