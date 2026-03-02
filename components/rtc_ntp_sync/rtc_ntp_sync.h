#ifndef RTC_NTP_SYNC_H
#define RTC_NTP_SYNC_H

#include "esp_err.h"

/**
 * @brief Synchronize system time from NTP server with enhanced debugging
 * 
 * @param timeout_sec Timeout in seconds (45-60s recommended for ESP-Hosted)
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on failure
 */
esp_err_t sync_time_from_ntp(int timeout_sec);

/**
 * @brief Check DNS resolution for NTP server
 * 
 * @param ntp_server NTP server hostname
 * @return esp_err_t ESP_OK if resolved successfully
 */
esp_err_t check_ntp_dns_resolution(const char *ntp_server);

/**
 * @brief Perform ping test to target host
 * 
 * @param target_host Target hostname or IP
 * @param count Number of ping packets
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ping_test(const char *target_host, int count);

#endif // RTC_NTP_SYNC_H
