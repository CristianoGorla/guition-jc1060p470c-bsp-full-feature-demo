#include "rtc_ntp_sync.h"
#include "rtc_rx8025t.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RTC_NTP";

/**
 * @brief Callback called when NTP time sync completes
 */
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synchronized!");
}

esp_err_t rtc_reset_to_default(void)
{
    ESP_LOGI(TAG, "Resetting RTC to default time (2000-01-01 00:00:00)...");
    
    rtc_time_t default_time = {
        .year = 0,      // RX8025T: 0 = year 2000
        .month = 1,     // January
        .day = 1,       // 1st
        .wday = 6,      // Saturday (2000-01-01 was a Saturday)
        .hour = 0,
        .minute = 0,
        .second = 0
    };
    
    esp_err_t ret = rtc_rx8025t_set_time(&default_time);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ RTC reset to: 2000-01-01 00:00:00");
    } else {
        ESP_LOGE(TAG, "✗ RTC reset failed");
    }
    
    return ret;
}

esp_err_t sync_time_from_ntp(int timeout_sec)
{
    ESP_LOGI(TAG, "Starting NTP time synchronization...");
    ESP_LOGI(TAG, "NTP Server: pool.ntp.org");
    ESP_LOGI(TAG, "Timezone: CET (UTC+1, DST auto)");
    
    // Set timezone to Central European Time (CET/CEST)
    // CET = UTC+1, CEST = UTC+2 (DST from last Sun Mar 2am to last Sun Oct 3am)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    
    // Wait for time sync
    ESP_LOGI(TAG, "Waiting for NTP sync (timeout: %d seconds)...", timeout_sec);
    
    int retry = 0;
    const int max_retry = timeout_sec * 2; // Check every 500ms
    
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < max_retry) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }
    
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        // Get current time
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
        ESP_LOGI(TAG, "✓ NTP sync successful!");
        ESP_LOGI(TAG, "Current time: %s CET", strftime_buf);
        
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "✗ NTP sync timeout after %d seconds", timeout_sec);
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t update_rtc_from_system_time(void)
{
    ESP_LOGI(TAG, "Updating RTC with system time...");
    
    // Get current system time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Convert to RTC format
    rtc_time_t rtc_time = {
        .year = timeinfo.tm_year - 100,  // tm_year is years since 1900, RTC uses years since 2000
        .month = timeinfo.tm_mon + 1,    // tm_mon is 0-11, RTC uses 1-12
        .day = timeinfo.tm_mday,
        .wday = timeinfo.tm_wday,        // Both use 0=Sunday
        .hour = timeinfo.tm_hour,
        .minute = timeinfo.tm_min,
        .second = timeinfo.tm_sec
    };
    
    ESP_LOGI(TAG, "System time: 20%02d-%02d-%02d %02d:%02d:%02d (wday=%d)",
             rtc_time.year, rtc_time.month, rtc_time.day,
             rtc_time.hour, rtc_time.minute, rtc_time.second, rtc_time.wday);
    
    esp_err_t ret = rtc_rx8025t_set_time(&rtc_time);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ RTC updated successfully");
        
        // Verify by reading back
        rtc_time_t verify_time;
        if (rtc_rx8025t_get_time(&verify_time) == ESP_OK) {
            ESP_LOGI(TAG, "RTC readback: 20%02d-%02d-%02d %02d:%02d:%02d",
                     verify_time.year, verify_time.month, verify_time.day,
                     verify_time.hour, verify_time.minute, verify_time.second);
        }
    } else {
        ESP_LOGE(TAG, "✗ RTC update failed");
    }
    
    return ret;
}

esp_err_t rtc_ntp_sync_test(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   RTC NTP Sync Test");
    ESP_LOGI(TAG, "========================================\n");
    
    esp_err_t ret;
    
    // Step 1: Read current RTC time
    ESP_LOGI(TAG, "Step 1/4: Read current RTC time");
    rtc_time_t current_time;
    ret = rtc_rx8025t_get_time(&current_time);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current RTC: 20%02d-%02d-%02d %02d:%02d:%02d\n",
                 current_time.year, current_time.month, current_time.day,
                 current_time.hour, current_time.minute, current_time.second);
    } else {
        ESP_LOGE(TAG, "Failed to read RTC\n");
        return ret;
    }
    
    // Step 2: Reset RTC to default
    ESP_LOGI(TAG, "Step 2/4: Reset RTC to default time");
    ret = rtc_reset_to_default();
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Verify reset
    rtc_time_t reset_time;
    rtc_rx8025t_get_time(&reset_time);
    ESP_LOGI(TAG, "RTC after reset: 20%02d-%02d-%02d %02d:%02d:%02d\n",
             reset_time.year, reset_time.month, reset_time.day,
             reset_time.hour, reset_time.minute, reset_time.second);
    
    // Step 3: Sync with NTP
    ESP_LOGI(TAG, "Step 3/4: Synchronize with NTP server");
    ret = sync_time_from_ntp(10);  // 10 second timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NTP sync failed\n");
        return ret;
    }
    ESP_LOGI(TAG, "");
    
    // Step 4: Update RTC with NTP time
    ESP_LOGI(TAG, "Step 4/4: Update RTC with NTP time");
    ret = update_rtc_from_system_time();
    if (ret != ESP_OK) {
        return ret;
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   RTC NTP Sync Test Complete");
    ESP_LOGI(TAG, "========================================\n");
    
    return ESP_OK;
}
