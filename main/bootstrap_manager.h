/*
 * Bootstrap Manager for Guition JC1060P470C
 * 
 * Three-phase initialization strategy (v1.0.0-beta restored):
 * - Phase A: Power management (C6 + SD isolation)
 * - Phase B: WiFi Hosted initialization (SDIO via arbiter)
 * - Phase C: SD card mount (automatic after WiFi ready)
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#ifndef __BOOTSTRAP_MANAGER_H__
#define __BOOTSTRAP_MANAGER_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Event Group Bits for Bootstrap Coordination
 */
#define BOOTSTRAP_POWER_READY_BIT    (1 << 0)  // Phase A complete: Power rails stable
#define BOOTSTRAP_HOSTED_READY_BIT   (1 << 1)  // Phase B complete: WiFi transport active
#define BOOTSTRAP_SD_READY_BIT       (1 << 2)  // Phase C complete: SD card mounted
#define BOOTSTRAP_FAILURE_BIT        (1 << 3)  // Critical failure detected

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
 * Task Priorities
 */
#define BOOTSTRAP_POWER_TASK_PRIORITY       (configMAX_PRIORITIES - 1)  // Highest
#define BOOTSTRAP_WIFI_TASK_PRIORITY        (configMAX_PRIORITIES - 2)
#define BOOTSTRAP_SD_TASK_PRIORITY          (configMAX_PRIORITIES - 3)

/**
 * Bootstrap Manager State
 */
typedef struct {
    EventGroupHandle_t event_group;        // Coordination event group
    TaskHandle_t power_task_handle;        // Phase A task
    TaskHandle_t wifi_task_handle;         // Phase B task
    TaskHandle_t sd_task_handle;           // Phase C task
    sdmmc_card_t *sd_card;                 // SD card handle (Phase C)
    bool warm_boot_detected;               // True if software reset
    uint32_t boot_timestamp_ms;            // Boot start time
} bootstrap_manager_t;

/**
 * Initialize bootstrap manager and start three-phase boot sequence
 * 
 * This function:
 * 1. Initializes SDMMC arbiter
 * 2. Detects warm boot vs cold boot
 * 3. Performs hard reset if needed (warm boot)
 * 4. Spawns three coordinated tasks:
 *    - Phase A: Power Management (GPIO isolation + power sequencing)
 *    - Phase B: WiFi Hosted (requests WiFi mode from arbiter)
 *    - Phase C: SD Manager (mounts SD card after WiFi ready)
 * 
 * @param[out] manager  Pointer to bootstrap manager structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bootstrap_manager_init(bootstrap_manager_t *manager);

/**
 * Wait for bootstrap completion with timeout
 * 
 * @param manager     Bootstrap manager instance
 * @param timeout_ms  Maximum wait time in milliseconds
 * @return ESP_OK if all three phases completed successfully
 *         ESP_ERR_TIMEOUT if timeout occurred
 *         ESP_FAIL if critical failure detected
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
