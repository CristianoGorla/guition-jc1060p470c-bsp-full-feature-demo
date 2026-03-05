/**
 * @file esp_hosted_wifi.h
 * @brief ESP-Hosted WiFi management with SDMMC slot arbitration support
 * 
 * Provides WiFi initialization and transport control functions for ESP32-C6
 * communication via ESP-Hosted SDIO protocol.
 * 
 * CRITICAL: Includes transport pause/resume API for safe SDMMC slot switching
 * to prevent race condition 0x108 during SD card mount.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#ifndef __ESP_HOSTED_WIFI_H__
#define __ESP_HOSTED_WIFI_H__

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ESP-Hosted SDIO transport (Phase B of bootstrap)
 * 
 * This function initializes only the SDIO transport layer for ESP-Hosted,
 * configuring SDMMC Slot 1 for communication with ESP32-C6.
 * 
 * Called by bootstrap_manager Phase B (WiFi Manager task).
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_hosted_init_transport(void);

/**
 * @brief Deinitialize ESP-Hosted WiFi transport
 * @return ESP_OK on success
 */
esp_err_t wifi_hosted_deinit_transport(void);

/**
 * @brief Initialize WiFi subsystem (complete initialization)
 * 
 * This is the legacy initialization function that performs both
 * transport init and WiFi stack setup.
 * 
 * For v1.1.0-dev, this will be refactored to call
 * wifi_hosted_init_transport() internally.
 */
void init_wifi(void);

/**
 * @brief Perform WiFi scan and optionally check for specific SSID
 * 
 * @param target_ssid SSID to search for (NULL to scan all)
 * @return true if scan successful (and SSID found if specified)
 */
bool do_wifi_scan_and_check(const char *target_ssid);

/**
 * @brief Connect to WiFi network
 * 
 * @param ssid Network SSID
 * @param password Network password
 */
void wifi_connect(const char *ssid, const char *password);

/**
 * @brief Wait for IP address assignment
 */
void wait_for_ip(void);

/**
 * @brief Check if device already has IP address
 * 
 * @return true if IP assigned
 */
bool check_if_already_has_ip(void);

/**
 * @brief Suspend ESP-Hosted transport for safe SDMMC slot switch
 * 
 * CRITICAL: Call this BEFORE sdmmc_host_deinit() when switching from
 * Slot 1 (WiFi) to Slot 0 (SD Card). Prevents race condition 0x108.
 * 
 * This function:
 * - Stops WiFi TX/RX operations to halt SDIO transactions
 * - Allows pending SDIO operations to complete gracefully
 * - Makes the bus IDLE for safe controller reinitialization
 * 
 * Must be paired with esp_hosted_resume_transport() after slot switch.
 * 
 * Example usage:
 * @code
 * esp_hosted_pause_transport();
 * vTaskDelay(pdMS_TO_TICKS(100));  // Allow pending ops to finish
 * sdmmc_host_deinit();             // Now safe to deinit
 * // ... reinit for Slot 0 ...
 * esp_hosted_resume_transport();
 * @endcode
 */
void esp_hosted_pause_transport(void);

/**
 * @brief Resume ESP-Hosted transport after SDMMC slot switch
 * 
 * Call this AFTER SD card mount completes to restore WiFi connectivity.
 * Controller will be on Slot 0 (SD), but WiFi driver state is preserved.
 * 
 * WiFi will automatically reinitialize Slot 1 communication as needed.
 * 
 * IMPORTANT: Always call this function even if SD mount fails to ensure
 * WiFi connectivity is restored.
 */
void esp_hosted_resume_transport(void);

/**
 * @brief Check if transport is currently paused
 * @return true if paused, false if active
 */
bool esp_hosted_is_transport_paused(void);

#ifdef __cplusplus
}
#endif

#endif // __ESP_HOSTED_WIFI_H__
