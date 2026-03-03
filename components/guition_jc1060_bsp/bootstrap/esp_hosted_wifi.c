#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_hosted_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_hosted";
static EventGroupHandle_t wifi_event_group = NULL;
const int IP_GOT_BIT = BIT0;

static bool transport_initialized = false;
static bool wifi_started = false;
static esp_event_handler_instance_t ip_event_handler = NULL;

// Transport suspension control for SDMMC slot arbitration
static bool transport_paused = false;
static bool wifi_was_started_before_pause = false;

/* C6 firmware boot delay after BSP Phase A release */
#define C6_FIRMWARE_BOOT_DELAY_MS 2500

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(wifi_event_group, IP_GOT_BIT);
    }
}

/**
 * @brief Suspend ESP-Hosted transport for safe SDMMC slot switch
 * 
 * CRITICAL: Call this BEFORE sdmmc_host_deinit() when switching from 
 * Slot 1 (WiFi) to Slot 0 (SD Card). Prevents race condition 0x108.
 * 
 * Mechanism:
 * - Fully deinitializes WiFi driver to stop ALL SDIO activity
 * - Kills ESP-Hosted RX/TX tasks and frees SDMMC resources
 * - Makes bus completely IDLE for safe controller reinitialization
 * 
 * Must be paired with esp_hosted_resume_transport() after slot switch.
 */
void esp_hosted_pause_transport(void)
{
    if (transport_paused) {
        ESP_LOGW(TAG, "Transport already paused");
        return;
    }
    
    if (!wifi_started) {
        ESP_LOGW(TAG, "WiFi not started, transport pause not needed");
        return;
    }
    
    ESP_LOGI(TAG, "[TRANSPORT] Suspending for SDMMC slot arbitration...");
    
    // Save WiFi state
    wifi_was_started_before_pause = wifi_started;
    
    // AGGRESSIVE FIX: Fully deinitialize WiFi to kill ESP-Hosted tasks
    // This is the ONLY way to guarantee SDIO bus is completely IDLE
    
    ESP_LOGI(TAG, "[TRANSPORT] Stopping WiFi...");
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi stop failed: %s (continuing)", esp_err_to_name(ret));
    }
    
    // Allow WiFi stop event to propagate
    vTaskDelay(pdMS_TO_TICKS(50));
    
    ESP_LOGI(TAG, "[TRANSPORT] Deinitializing WiFi driver...");
    ret = esp_wifi_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi deinit failed: %s (continuing)", esp_err_to_name(ret));
    }
    
    wifi_started = false;
    transport_paused = true;
    
    // CRITICAL: Wait for ESP-Hosted tasks to fully terminate
    // ESP-Hosted deinit triggers async task cleanup - we MUST wait
    ESP_LOGI(TAG, "[TRANSPORT] Waiting for ESP-Hosted tasks to terminate...");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    ESP_LOGI(TAG, "[TRANSPORT] WiFi transport suspended, bus IDLE");
}

/**
 * @brief Resume ESP-Hosted transport after SDMMC slot switch
 * 
 * Call this AFTER SD card mount completes to restore WiFi connectivity.
 * Controller will be on Slot 0 (SD), WiFi will reinitialize on Slot 1.
 */
void esp_hosted_resume_transport(void)
{
    if (!transport_paused) {
        ESP_LOGD(TAG, "Transport not paused, resume not needed");
        return;
    }
    
    ESP_LOGI(TAG, "[TRANSPORT] Resuming WiFi after slot switch...");
    
    // Reinitialize WiFi - ESP-Hosted will reinit Slot 1 from scratch
    ESP_LOGI(TAG, "[TRANSPORT] Reinitializing WiFi driver...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi reinit failed: %s", esp_err_to_name(ret));
        transport_paused = false;
        return;
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set mode failed: %s", esp_err_to_name(ret));
        transport_paused = false;
        return;
    }
    
    // Restart WiFi if it was running before pause
    if (wifi_was_started_before_pause) {
        ESP_LOGI(TAG, "[TRANSPORT] Starting WiFi...");
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi restart failed: %s", esp_err_to_name(ret));
        } else {
            wifi_started = true;
        }
    }
    
    transport_paused = false;
    wifi_was_started_before_pause = false;
    ESP_LOGI(TAG, "[TRANSPORT] WiFi transport resumed on Slot 1");
}

/**
 * @brief Check if transport is currently paused
 */
bool esp_hosted_is_transport_paused(void)
{
    return transport_paused;
}

esp_err_t wifi_hosted_init_transport(void)
{
    if (transport_initialized)
    {
        ESP_LOGI(TAG, "WiFi Hosted transport already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "=== WiFi Hosted Transport Init (Phase B) ===");

    // Step 1: Initialize TCP/IP stack
    ESP_LOGI(TAG, "Initializing netif...");
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Create event loop
    ESP_LOGI(TAG, "Creating event loop...");
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        // ESP_ERR_INVALID_STATE means loop already exists (OK)
        ESP_LOGE(TAG, "event loop creation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 3: Create default WiFi station netif
    ESP_LOGI(TAG, "Creating default WiFi STA netif...");
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif)
    {
        ESP_LOGE(TAG, "Failed to create WiFi STA netif");
        return ESP_FAIL;
    }

    // Step 4: Register IP event handler
    ESP_LOGI(TAG, "Registering IP event handler...");
    if (!wifi_event_group)
    {
        wifi_event_group = xEventGroupCreate();
    }
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &event_handler, NULL, &ip_event_handler);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Event handler registration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    transport_initialized = true;
    ESP_LOGI(TAG, "✓ WiFi Hosted transport initialized\n");

    // NOTE: At this point, ESP-Hosted SDIO transport is active.
    // The SDMMC controller is configured for Slot 1 (C6 communication).
    // SD card (Slot 0) can now safely initialize without conflicts.

    return ESP_OK;
}

esp_err_t wifi_hosted_deinit_transport(void)
{
    ESP_LOGI(TAG, "=== WiFi Hosted Transport Deinit ===");

    if (!transport_initialized)
    {
        ESP_LOGI(TAG, "Transport not initialized, nothing to deinit");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;

    // Stop WiFi if started
    if (wifi_started)
    {
        ESP_LOGI(TAG, "Stopping WiFi...");
        ret = esp_wifi_stop();
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "WiFi stop failed: %s", esp_err_to_name(ret));
        }

        ESP_LOGI(TAG, "Deinitializing WiFi driver...");
        ret = esp_wifi_deinit();
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "WiFi deinit failed: %s", esp_err_to_name(ret));
        }
        wifi_started = false;
    }

    // Unregister event handler
    if (ip_event_handler)
    {
        ESP_LOGI(TAG, "Unregistering event handler...");
        ret = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Event handler unregister failed: %s", esp_err_to_name(ret));
        }
        ip_event_handler = NULL;
    }

    // Destroy event group
    if (wifi_event_group)
    {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }

    // Note: We don't destroy netif or event loop as they may be used by other components

    transport_initialized = false;
    ESP_LOGI(TAG, "✓ WiFi Hosted transport deinitialized\n");

    return ESP_OK;
}

void init_wifi(void)
{
    ESP_LOGI(TAG, "=== WiFi Stack Initialization ===");

    // If transport not initialized yet, do it now
    if (!transport_initialized)
    {
        esp_err_t ret = wifi_hosted_init_transport();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Transport init failed, cannot proceed");
            return;
        }
    }

    // CRITICAL FIX: Wait for C6 firmware to boot after BSP Phase A release
    //
    // Timeline:
    //   T+1549ms: BSP Phase A releases C6 from reset (GPIO54 HIGH)
    //   T+2130ms: init_wifi() called
    //   T+2130ms: Wait here for C6 firmware boot (2500ms)
    //   T+4630ms: C6 firmware fully booted and SDIO slave ready
    //   T+4630ms: esp_wifi_init() safely initializes ESP-Hosted transport
    //
    // Without this delay:
    //   - esp_wifi_init() resets C6 again via GPIO54
    //   - ESP-Hosted immediately tries SDMMC Slot 1 init
    //   - C6 not ready → timeout 0x107 → init fails
    //
    // C6 boot time breakdown:
    // - Bootloader: ~300ms
    // - App startup: ~500ms
    // - SDIO slave init: ~700ms
    // - State machine sync: ~500ms
    // Total: ~2000ms (using 2500ms for safety margin)
    //
    ESP_LOGI(TAG, "Waiting %dms for C6 firmware boot (BSP released C6 at ~T+1549ms)...", C6_FIRMWARE_BOOT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(C6_FIRMWARE_BOOT_DELAY_MS));
    ESP_LOGI(TAG, "C6 firmware should be ready, proceeding with WiFi init");

    // Initialize WiFi stack (this triggers ESP-Hosted SDIO init)
    ESP_LOGI(TAG, "Initializing WiFi driver...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Setting WiFi mode to STA...");
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Set mode failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Starting WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return;
    }

    wifi_started = true;
    ESP_LOGI(TAG, "✓ WiFi stack initialized\n");
}

bool check_if_already_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    return (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0);
}

bool do_wifi_scan_and_check(const char *target)
{
    wifi_scan_config_t sc = {.ssid = (uint8_t *)target};
    esp_wifi_scan_start(&sc, true);
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    return count > 0;
}

void wifi_connect(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1] = '\0';

    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.password[sizeof(cfg.sta.password) - 1] = '\0';
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
}

void wait_for_ip(void)
{
    xEventGroupWaitBits(wifi_event_group, IP_GOT_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
}
