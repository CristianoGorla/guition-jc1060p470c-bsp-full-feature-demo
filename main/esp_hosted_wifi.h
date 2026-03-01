#ifndef __ESP_HOSTED_WIFI_H__
#define __ESP_HOSTED_WIFI_H__
#include <stdbool.h>
#include "esp_err.h"

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

#endif
