#ifndef RTC_NTP_SYNC_H
#define RTC_NTP_SYNC_H

#include "esp_err.h"
#include "rx8025t_bsp.h"

/**
 * @brief Reset RTC to default time (2000-01-01 00:00:00)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t rtc_reset_to_default(void);

/**
 * @brief Synchronize system time with NTP server
 *
 * Requires active WiFi connection.
 * Uses pool.ntp.org by default.
 *
 * @param timeout_sec Timeout in seconds (default: 10)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sync_time_from_ntp(int timeout_sec);

/**
 * @brief Update RTC with current system time
 *
 * Reads system time (from NTP or manual set) and writes to RTC.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t update_rtc_from_system_time(void);

/**
 * @brief Complete NTP sync workflow: Reset RTC → NTP sync → Update RTC
 *
 * This performs a complete RTC synchronization test:
 * 1. Reset RTC to default time (2000-01-01 00:00:00)
 * 2. Sync system time with NTP server
 * 3. Update RTC with NTP time
 *
 * Requires active WiFi connection.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t rtc_ntp_sync_test(void);

#endif // RTC_NTP_SYNC_H
