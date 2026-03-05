#include "rtc_ntp_sync.h"
#include "rx8025t_bsp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
#include "esp_timer.h"        // For esp_timer_get_time()
#include "lwip/inet.h"
#include "lwip/netdb.h"       // For getaddrinfo()
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
#include "esp_netif.h"
#endif

static const char *TAG = "RTC_NTP";

#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
static int64_t test_start_time_us = 0;
static volatile bool ntp_callback_invoked = false;  // Flag to detect callback

/**
 * @brief Convert SNTP status to string
 */
static const char* sntp_status_to_str(sntp_sync_status_t status) {
    switch(status) {
        case SNTP_SYNC_STATUS_RESET:       return "RESET";
        case SNTP_SYNC_STATUS_COMPLETED:   return "COMPLETED";
        case SNTP_SYNC_STATUS_IN_PROGRESS: return "IN_PROGRESS";
        default:                            return "UNKNOWN";
    }
}

/**
 * @brief Test DNS resolution for NTP server
 */
static esp_err_t test_dns_resolution(const char *hostname)
{
    ESP_LOGI(TAG, "[DNS] Resolving %s...", hostname);
    int64_t start = esp_timer_get_time();
    
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *res;
    
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    int64_t elapsed_ms = (esp_timer_get_time() - start) / 1000;
    
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "[DNS] [FAIL] Resolution failed: error %d (took %lld ms)", 
                 err, elapsed_ms);
        return ESP_FAIL;
    }
    
    struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "[DNS] [OK] Resolved to %s (took %lld ms)", 
             inet_ntoa(*addr), elapsed_ms);
    
    freeaddrinfo(res);
    return ESP_OK;
}

/**
 * @brief Ping callback for statistics
 */
static void ping_success_cb(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    
    ESP_LOGI(TAG, "[PING] %d bytes from %s icmp_seq=%d ttl=%d time=%d ms",
             recv_len, ipaddr_ntoa(&target_addr), seqno, ttl, elapsed_time);
}

static void ping_timeout_cb(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    
    ESP_LOGW(TAG, "[PING] From %s icmp_seq=%d timeout",
             ipaddr_ntoa(&target_addr), seqno);
}

static void ping_end_cb(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted, received, total_time;
    
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time, sizeof(total_time));
    
    uint32_t loss = transmitted - received;
    ESP_LOGI(TAG, "[PING] %d packets transmitted, %d received, %d%% packet loss, time %dms",
             transmitted, received, (loss * 100) / transmitted, total_time);
}

/**
 * @brief Test ping to target IP
 */
static esp_err_t test_ping(const char *target_name, const char *target_ip)
{
    ESP_LOGI(TAG, "[PING] Testing connectivity to %s (%s)...", target_name, target_ip);
    
    ip_addr_t target_addr;
    if (!ipaddr_aton(target_ip, &target_addr)) {
        ESP_LOGE(TAG, "[PING] [FAIL] Invalid IP address: %s", target_ip);
        return ESP_FAIL;
    }
    
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = 3;
    ping_config.interval_ms = 1000;
    ping_config.timeout_ms = 2000;
    
    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_success_cb,
        .on_ping_timeout = ping_timeout_cb,
        .on_ping_end = ping_end_cb,
        .cb_args = NULL
    };
    
    esp_ping_handle_t ping;
    esp_err_t ret = esp_ping_new_session(&ping_config, &cbs, &ping);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[PING] [FAIL] Failed to create ping session: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_ping_start(ping);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[PING] [FAIL] Failed to start ping: %s", esp_err_to_name(ret));
        esp_ping_delete_session(ping);
        return ret;
    }
    
    // Wait for ping to complete (3 pings * 1s interval + overhead)
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    
    return ESP_OK;
}

/**
 * @brief Get gateway IP address
 */
static esp_err_t get_gateway_ip(char *ip_str, size_t len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "Failed to get netif handle");
        return ESP_FAIL;
    }
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP info");
        return ESP_FAIL;
    }
    
    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.gw));
    return ESP_OK;
}
#endif // CONFIG_APP_NTP_DEBUG_ENABLE

/**
 * @brief Callback called when NTP time sync completes
 */
static void time_sync_notification_cb(struct timeval *tv)
{
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
    ntp_callback_invoked = true;  // Set flag
    int64_t elapsed_ms = (esp_timer_get_time() - test_start_time_us) / 1000;
    ESP_LOGI(TAG, "[OK] NTP callback invoked! Time synchronized (T+%.1fs)", elapsed_ms / 1000.0);
#else
    ESP_LOGI(TAG, "NTP time synchronized!");
#endif
}

esp_err_t rtc_reset_to_default(void)
{
    ESP_LOGI(TAG, "Resetting RTC to default time (2000-01-01 00:00:00)...");
    
    bsp_rtc_time_t default_time = {
        .year = 0,      // RX8025T: 0 = year 2000
        .month = 1,     // January
        .day = 1,       // 1st
        .hour = 0,
        .minute = 0,
        .second = 0
    };
    
    esp_err_t ret = bsp_rtc_set_time(&default_time);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OK] RTC reset to: 2000-01-01 00:00:00");
    } else {
        ESP_LOGE(TAG, "[FAIL] RTC reset failed");
    }
    
    return ret;
}

esp_err_t sync_time_from_ntp(int timeout_sec)
{
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
    test_start_time_us = esp_timer_get_time();
    ntp_callback_invoked = false;  // Reset flag
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   Enhanced NTP Sync (ESP-Hosted)");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "NTP Server: pool.ntp.org");
    ESP_LOGI(TAG, "Timezone: CET (UTC+1, DST auto)");
    ESP_LOGI(TAG, "Timeout: %d seconds\n", timeout_sec);
    
    // Step 1: DNS Resolution Test
    ESP_LOGI(TAG, "[1/4] Testing DNS resolution...");
    if (test_dns_resolution("pool.ntp.org") != ESP_OK) {
        ESP_LOGE(TAG, "DNS resolution failed - check network connectivity\n");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "");
    
    // Step 2: Ping Gateway
    ESP_LOGI(TAG, "[2/4] Testing connectivity to gateway...");
    char gateway_ip[16];
    if (get_gateway_ip(gateway_ip, sizeof(gateway_ip)) == ESP_OK) {
        test_ping("Gateway", gateway_ip);
    } else {
        ESP_LOGW(TAG, "Could not determine gateway IP, skipping ping test");
    }
    ESP_LOGI(TAG, "");
    
    // Step 3: Ping NTP Server
    ESP_LOGI(TAG, "[3/4] Testing connectivity to NTP server...");
    // Use one of the NTP pool servers
    test_ping("NTP Server", "185.125.190.58"); // time.cloudflare.com as fallback
    ESP_LOGI(TAG, "");
    
    // Step 4: NTP Sync
    ESP_LOGI(TAG, "[4/4] Starting NTP synchronization...");
#else
    ESP_LOGI(TAG, "Starting NTP time synchronization...");
    ESP_LOGI(TAG, "NTP Server: pool.ntp.org");
    ESP_LOGI(TAG, "Timezone: CET (UTC+1, DST auto)");
#endif
    
    // Set timezone to Central European Time (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    // Configure SNTP - CRITICAL: callback must be set before init
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    
    // Wait for time sync
    ESP_LOGI(TAG, "Waiting for NTP sync (timeout: %d seconds)...", timeout_sec);
    
    int retry = 0;
    const int max_retry = timeout_sec * 10; // Check every 100ms
    sntp_sync_status_t last_status = SNTP_SYNC_STATUS_RESET;
    
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
    int heartbeat_counter = 0;
#endif
    
    // Wait loop - CRITICAL FIX: Trust callback as source of truth
    while (retry < max_retry) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
        
        sntp_sync_status_t status = esp_sntp_get_sync_status();
        
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
        // FIX: Exit immediately if callback was invoked (ignore status)
        if (ntp_callback_invoked) {
            int64_t elapsed_ms = (esp_timer_get_time() - test_start_time_us) / 1000;
            ESP_LOGI(TAG, "[NTP] Callback detected! Exiting wait loop (T+%.1fs, status=%s)", 
                     elapsed_ms / 1000.0, sntp_status_to_str(status));
            break;
        }
#endif
        
        // Fallback exit condition (if callback not invoked but status changed)
        if (status == SNTP_SYNC_STATUS_COMPLETED) {
            break;
        }
        
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
        // Detailed status logging
        if (status != last_status) {
            int64_t elapsed_ms = (esp_timer_get_time() - test_start_time_us) / 1000;
            ESP_LOGI(TAG, "[NTP] Status: %s (%d) at T+%.1fs", 
                     sntp_status_to_str(status), status, elapsed_ms / 1000.0);
            last_status = status;
        }
        
        // Heartbeat every 5 seconds
        heartbeat_counter++;
        if (heartbeat_counter >= 50) { // 50 * 100ms = 5s
            int64_t elapsed_ms = (esp_timer_get_time() - test_start_time_us) / 1000;
            ESP_LOGI(TAG, "[NTP] Waiting... %.1f/%d sec | Status: %s | Callback: %s", 
                     elapsed_ms / 1000.0, timeout_sec,
                     sntp_status_to_str(status),
                     ntp_callback_invoked ? "YES" : "NO");
            heartbeat_counter = 0;
        }
#else
        // Standard logging every 10 iterations (1 second)
        if (status != SNTP_SYNC_STATUS_RESET && retry % 10 == 0) {
            ESP_LOGI(TAG, "NTP status: %d (retry %d/%d)", status, retry, max_retry);
        }
#endif
    }
    
    // CRITICAL FIX: Trust callback as source of truth
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
    bool sync_successful = ntp_callback_invoked;
#else
    bool sync_successful = (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
#endif
    
    if (sync_successful) {
        // Get current time
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
        int64_t total_ms = (esp_timer_get_time() - test_start_time_us) / 1000;
        sntp_sync_status_t final_status = esp_sntp_get_sync_status();
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "   [OK] NTP SYNC SUCCESSFUL [OK]");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Current time: %s CET", strftime_buf);
        ESP_LOGI(TAG, "Sync completed after %.1f seconds", total_ms / 1000.0);
        ESP_LOGI(TAG, "Final status: %s (callback invoked: %s)", 
                 sntp_status_to_str(final_status),
                 ntp_callback_invoked ? "YES" : "NO");
        ESP_LOGI(TAG, "========================================\n");
#else
        ESP_LOGI(TAG, "[OK] NTP sync successful!");
        ESP_LOGI(TAG, "Current time: %s CET", strftime_buf);
#endif
        
        return ESP_OK;
    } else {
#ifdef CONFIG_APP_NTP_DEBUG_ENABLE
        sntp_sync_status_t final_status = esp_sntp_get_sync_status();
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "   [FAIL] NTP SYNC FAILED [FAIL]");
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "Final status: %s (%d)", sntp_status_to_str(final_status), final_status);
        ESP_LOGE(TAG, "Callback invoked: %s", ntp_callback_invoked ? "YES" : "NO");
        ESP_LOGE(TAG, "Timeout: %d seconds", timeout_sec);
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  - ESP-Hosted needs longer timeout (try 90-120s)");
        ESP_LOGE(TAG, "  - Firewall blocking NTP (UDP port 123)");
        ESP_LOGE(TAG, "  - Network congestion or packet loss");
        ESP_LOGE(TAG, "  - SNTP state machine stuck (callback=%s, status=%s)",
                 ntp_callback_invoked ? "invoked" : "not invoked",
                 sntp_status_to_str(final_status));
        ESP_LOGE(TAG, "========================================\n");
#else
        ESP_LOGE(TAG, "[FAIL] NTP sync failed after %d seconds", timeout_sec);
#endif
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
    bsp_rtc_time_t rtc_time = {
        .year = timeinfo.tm_year - 100,  // tm_year is years since 1900, RTC uses years since 2000
        .month = timeinfo.tm_mon + 1,    // tm_mon is 0-11, RTC uses 1-12
        .day = timeinfo.tm_mday,
        .hour = timeinfo.tm_hour,
        .minute = timeinfo.tm_min,
        .second = timeinfo.tm_sec
    };
    
    ESP_LOGI(TAG, "System time: 20%02d-%02d-%02d %02d:%02d:%02d",
             rtc_time.year, rtc_time.month, rtc_time.day,
             rtc_time.hour, rtc_time.minute, rtc_time.second);
    
    esp_err_t ret = bsp_rtc_set_time(&rtc_time);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OK] RTC updated successfully");
        
        // Verify by reading back
        bsp_rtc_time_t verify_time;
        if (bsp_rtc_get_time(&verify_time) == ESP_OK) {
            ESP_LOGI(TAG, "RTC readback: 20%02d-%02d-%02d %02d:%02d:%02d",
                     verify_time.year, verify_time.month, verify_time.day,
                     verify_time.hour, verify_time.minute, verify_time.second);
        }
    } else {
        ESP_LOGE(TAG, "[FAIL] RTC update failed");
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
    bsp_rtc_time_t current_time;
    ret = bsp_rtc_get_time(&current_time);
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
    bsp_rtc_time_t reset_time;
    bsp_rtc_get_time(&reset_time);
    ESP_LOGI(TAG, "RTC after reset: 20%02d-%02d-%02d %02d:%02d:%02d\n",
             reset_time.year, reset_time.month, reset_time.day,
             reset_time.hour, reset_time.minute, reset_time.second);
    
    // Step 3: Sync with NTP (90 seconds timeout for ESP-Hosted)
    ESP_LOGI(TAG, "Step 3/4: Synchronize with NTP server");
#ifdef CONFIG_APP_NTP_SYNC_TIMEOUT_SEC
    ret = sync_time_from_ntp(CONFIG_APP_NTP_SYNC_TIMEOUT_SEC);
#else
    ret = sync_time_from_ntp(90);  // 90 seconds for ESP-Hosted (was 60)
#endif
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
