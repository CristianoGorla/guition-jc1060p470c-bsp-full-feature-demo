#include "rtc_ntp_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
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
    ESP_LOGI(TAG, "[OK] NTP callback invoked! Time synchronized");
}

/**
 * @brief Ping test callback
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
    ESP_LOGW(TAG, "[PING] Request timeout for icmp_seq %d to %s", seqno, ipaddr_ntoa(&target_addr));
}

static void ping_end_cb(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted, received, total_time_ms;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    ESP_LOGI(TAG, "[PING] %d packets transmitted, %d received, time %dms",
             transmitted, received, total_time_ms);
}

/**
 * @brief Perform ping test to target
 */
esp_err_t ping_test(const char *target_host, int count)
{
    ESP_LOGI(TAG, "[PING] Testing connectivity to %s (%d packets)...", target_host, count);
    
    // Resolve target
    ip_addr_t target_addr;
    struct addrinfo hint = {0};
    struct addrinfo *res = NULL;
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_RAW;
    
    int err = getaddrinfo(target_host, NULL, &hint, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "[PING] DNS lookup failed for %s: %d", target_host, err);
        return ESP_FAIL;
    }
    
    struct in_addr addr4 = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
    freeaddrinfo(res);
    
    ESP_LOGI(TAG, "[PING] Resolved %s to %s", target_host, ipaddr_ntoa(&target_addr));
    
    // Configure ping
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = count;
    ping_config.interval_ms = 1000;
    ping_config.timeout_ms = 5000;
    
    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_success_cb,
        .on_ping_timeout = ping_timeout_cb,
        .on_ping_end = ping_end_cb,
        .cb_args = NULL
    };
    
    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, &cbs, &ping);
    esp_ping_start(ping);
    
    // Wait for ping to complete
    vTaskDelay(pdMS_TO_TICKS((count + 1) * 1000));
    
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    
    return ESP_OK;
}

/**
 * @brief Check DNS resolution for NTP server
 */
esp_err_t check_ntp_dns_resolution(const char *ntp_server)
{
    ESP_LOGI(TAG, "[DNS] Resolving NTP server: %s...", ntp_server);
    
    struct addrinfo hint = {0};
    struct addrinfo *res = NULL;
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_DGRAM;
    
    int64_t start_time = esp_timer_get_time();
    int err = getaddrinfo(ntp_server, NULL, &hint, &res);
    int64_t elapsed = (esp_timer_get_time() - start_time) / 1000;
    
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "[DNS] [FAIL] Failed to resolve %s (err: %d, took %lld ms)", 
                 ntp_server, err, elapsed);
        return ESP_FAIL;
    }
    
    struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "[DNS] [OK] Resolved %s to %s (took %lld ms)", 
             ntp_server, inet_ntoa(*addr), elapsed);
    
    freeaddrinfo(res);
    return ESP_OK;
}

esp_err_t sync_time_from_ntp(int timeout_sec)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   NTP Synchronization Test (Enhanced)");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "NTP Server: pool.ntp.org");
    ESP_LOGI(TAG, "Timezone: CET (UTC+1, DST auto)");
    ESP_LOGI(TAG, "Timeout: %d seconds", timeout_sec);
    ESP_LOGI(TAG, "Note: ESP-Hosted may take 30-45s for first NTP sync");
    ESP_LOGI(TAG, "");
    
    // Step 1: DNS resolution test
    ESP_LOGI(TAG, "[1/4] Testing DNS resolution...");
    esp_err_t dns_ret = check_ntp_dns_resolution("pool.ntp.org");
    if (dns_ret != ESP_OK) {
        ESP_LOGE(TAG, "DNS resolution failed! Check network/DNS settings");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "");
    
    // Step 2: Ping gateway
    ESP_LOGI(TAG, "[2/4] Ping test to gateway...");
    ping_test("192.168.188.1", 3);
    ESP_LOGI(TAG, "");
    
    // Step 3: Ping NTP server
    ESP_LOGI(TAG, "[3/4] Ping test to NTP server pool...");
    ping_test("pool.ntp.org", 3);
    ESP_LOGI(TAG, "");
    
    // Step 4: NTP sync
    ESP_LOGI(TAG, "[4/4] Starting NTP synchronization...");
    
    // Set timezone
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    // Configure SNTP - callback MUST be set before init
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    
    ESP_LOGI(TAG, "[NTP] SNTP configured, starting client...");
    esp_sntp_init();
    
    ESP_LOGI(TAG, "[NTP] Waiting for sync (timeout: %d seconds)...", timeout_sec);
    
    int retry = 0;
    const int max_retry = timeout_sec * 10; // Check every 100ms
    int last_status = -1;
    
    while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && 
           retry < max_retry) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
        
        // Log status changes
        sntp_sync_status_t status = esp_sntp_get_sync_status();
        if (status != last_status) {
            const char *status_str[] = {"RESET", "IN_PROGRESS", "COMPLETED"};
            ESP_LOGI(TAG, "[NTP] Status transition: %s (at T+%d.%ds)", 
                     status_str[status], retry / 10, retry % 10);
            last_status = status;
        }
        
        // Periodic heartbeat every 5s
        if (retry % 50 == 0 && retry > 0) {
            ESP_LOGI(TAG, "[NTP] Still waiting... (%d/%d seconds elapsed)", 
                     retry / 10, timeout_sec);
        }
    }
    
    sntp_sync_status_t final_status = esp_sntp_get_sync_status();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    
    if (final_status == SNTP_SYNC_STATUS_COMPLETED) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
        ESP_LOGI(TAG, "[OK] NTP SYNC SUCCESSFUL [OK]");
        ESP_LOGI(TAG, "Current time: %s CET", strftime_buf);
        ESP_LOGI(TAG, "Sync completed after %d.%d seconds", retry / 10, retry % 10);
        ESP_LOGI(TAG, "========================================");
        
        return ESP_OK;
    } else {
        const char *status_str[] = {"RESET", "IN_PROGRESS", "COMPLETED"};
        ESP_LOGE(TAG, "[FAIL] NTP SYNC FAILED [FAIL]");
        ESP_LOGE(TAG, "Final status: %s (after %d seconds)", 
                 status_str[final_status], timeout_sec);
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  - ESP-Hosted needs longer timeout (try 60s)");
        ESP_LOGE(TAG, "  - Firewall blocking UDP port 123");
        ESP_LOGE(TAG, "  - NTP server unreachable");
        ESP_LOGE(TAG, "========================================");
        
        return ESP_ERR_TIMEOUT;
    }
}
