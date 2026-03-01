#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_hosted_wifi.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "feature_flags.h"

static const char *TAG = "wifi_hosted";
static EventGroupHandle_t wifi_event_group;
const int IP_GOT_BIT = BIT0;

static bool transport_initialized = false;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(wifi_event_group, IP_GOT_BIT);
    }
}

esp_err_t wifi_hosted_init_transport(void)
{
    if (transport_initialized) {
        LOG_WIFI(TAG, "WiFi Hosted transport already initialized");
        return ESP_OK;
    }
    
    LOG_WIFI(TAG, "=== WiFi Hosted Transport Init (Phase B) ===");
    
    // Step 1: Initialize TCP/IP stack
    LOG_WIFI(TAG, "Initializing netif...");
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Create event loop
    LOG_WIFI(TAG, "Creating event loop...");
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means loop already exists (OK)
        ESP_LOGE(TAG, "event loop creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 3: Create default WiFi station netif
    LOG_WIFI(TAG, "Creating default WiFi STA netif...");
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        ESP_LOGE(TAG, "Failed to create WiFi STA netif");
        return ESP_FAIL;
    }
    
    // Step 4: Register IP event handler
    LOG_WIFI(TAG, "Registering IP event handler...");
    wifi_event_group = xEventGroupCreate();
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event handler registration failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    transport_initialized = true;
    LOG_WIFI(TAG, "✓ WiFi Hosted transport initialized\n");
    
    // NOTE: At this point, ESP-Hosted SDIO transport is active.
    // The SDMMC controller is configured for Slot 1 (C6 communication).
    // SD card (Slot 0) can now safely initialize without conflicts.
    
    return ESP_OK;
}

void init_wifi(void)
{
    LOG_WIFI(TAG, "=== WiFi Stack Initialization ===");
    
    // If transport not initialized yet, do it now
    if (!transport_initialized) {
        esp_err_t ret = wifi_hosted_init_transport();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Transport init failed, cannot proceed");
            return;
        }
    }
    
    // Initialize WiFi stack
    LOG_WIFI(TAG, "Initializing WiFi driver...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    LOG_WIFI(TAG, "Setting WiFi mode to STA...");
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set mode failed: %s", esp_err_to_name(ret));
        return;
    }
    
    LOG_WIFI(TAG, "Starting WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return;
    }
    
    LOG_WIFI(TAG, "✓ WiFi stack initialized\n");
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
