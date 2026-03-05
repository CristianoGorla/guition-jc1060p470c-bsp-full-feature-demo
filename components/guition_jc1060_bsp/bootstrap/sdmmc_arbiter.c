/*
 * SDMMC Bus Arbiter Implementation
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "sdmmc_arbiter.h"
#include "esp_log.h"
#include "bsp_log_panel.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

static const char *TAG = BSP_LOG_TAG;

#define LOG_UNIT "ARBITER"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) BSP_LOGE_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
static const char *NVS_NAMESPACE = "sdmmc_arbiter";
static const char *NVS_MODE_KEY = "mode";

// Global arbiter instance
static sdmmc_arbiter_t g_arbiter = {
    .mutex = NULL,
    .current_mode = SDMMC_MODE_NONE,
    .sd_card = NULL,
    .wifi_transport_active = false,
    .mode_switch_count = 0
};

// External functions from wifi_hosted.c and sd_card_manager.c
extern esp_err_t wifi_hosted_init_transport(void);
extern esp_err_t wifi_hosted_deinit_transport(void);
extern esp_err_t sd_card_mount_safe(sdmmc_card_t **card);
extern esp_err_t sd_card_unmount_safe(void);

/**
 * Initialize SDMMC arbiter
 */
esp_err_t sdmmc_arbiter_init(void)
{
    LOGI( "Initializing SDMMC bus arbiter...");
    
    // Create mutex for mutual exclusion
    g_arbiter.mutex = xSemaphoreCreateMutex();
    if (!g_arbiter.mutex) {
        LOGE( "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Load persistent mode from NVS
    sdmmc_bus_mode_t saved_mode = SDMMC_MODE_WIFI;  // Default to WiFi
    esp_err_t ret = sdmmc_arbiter_load_mode(&saved_mode);
    if (ret == ESP_OK) {
        LOGI( "Loaded persistent mode: %s", 
                 saved_mode == SDMMC_MODE_WIFI ? "WiFi" : "SD Card");
    } else {
        LOGI( "No persistent mode found, defaulting to WiFi");
    }
    
    g_arbiter.current_mode = SDMMC_MODE_NONE;  // Start idle
    g_arbiter.mode_switch_count = 0;
    
    LOGI( "[OK] SDMMC arbiter initialized");
    return ESP_OK;
}

/**
 * Request WiFi mode
 */
esp_err_t sdmmc_arbiter_request_wifi(uint32_t timeout_ms)
{
    LOGI( "Requesting WiFi mode...");
    
    // Acquire mutex
    if (xSemaphoreTake(g_arbiter.mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        LOGE( "Timeout waiting for bus access");
        return ESP_ERR_TIMEOUT;
    }
    
    // If already in WiFi mode, just return
    if (g_arbiter.current_mode == SDMMC_MODE_WIFI) {
        LOGI( "Already in WiFi mode");
        xSemaphoreGive(g_arbiter.mutex);
        return ESP_OK;
    }
    
    // If SD card mode is active, deinitialize it
    if (g_arbiter.current_mode == SDMMC_MODE_SD_CARD) {
        LOGI( "Switching from SD card to WiFi mode...");
        esp_err_t ret = sd_card_unmount_safe();
        if (ret != ESP_OK) {
            LOGE( "Failed to unmount SD card: %s", esp_err_to_name(ret));
            xSemaphoreGive(g_arbiter.mutex);
            return ret;
        }
        g_arbiter.sd_card = NULL;
        
        // Deinitialize SDMMC host (releases slot)
        LOGI( "Deinitializing SDMMC host...");
        sdmmc_host_deinit();
        vTaskDelay(pdMS_TO_TICKS(200));  // Bus settling time
    }
    
    // Initialize WiFi transport (ESP-Hosted)
    LOGI( "Initializing ESP-Hosted transport...");
    esp_err_t ret = wifi_hosted_init_transport();
    if (ret != ESP_OK) {
        LOGE( "Failed to initialize WiFi transport: %s", esp_err_to_name(ret));
        xSemaphoreGive(g_arbiter.mutex);
        return ret;
    }
    
    // Wait for SDIO link stabilization
    LOGI( "Waiting for SDIO link stabilization (5s)...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    g_arbiter.current_mode = SDMMC_MODE_WIFI;
    g_arbiter.wifi_transport_active = true;
    g_arbiter.mode_switch_count++;
    
    LOGI( "[OK] WiFi mode active (switch #%u)", g_arbiter.mode_switch_count);
    
    xSemaphoreGive(g_arbiter.mutex);
    return ESP_OK;
}

/**
 * Request SD card mode
 */
esp_err_t sdmmc_arbiter_request_sd_card(uint32_t timeout_ms, sdmmc_card_t **card)
{
    LOGI( "Requesting SD card mode...");
    
    // Acquire mutex
    if (xSemaphoreTake(g_arbiter.mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        LOGE( "Timeout waiting for bus access");
        return ESP_ERR_TIMEOUT;
    }
    
    // If already in SD card mode, just return
    if (g_arbiter.current_mode == SDMMC_MODE_SD_CARD) {
        LOGI( "Already in SD card mode");
        if (card) *card = g_arbiter.sd_card;
        xSemaphoreGive(g_arbiter.mutex);
        return ESP_OK;
    }
    
    // If WiFi mode is active, deinitialize it
    if (g_arbiter.current_mode == SDMMC_MODE_WIFI) {
        LOGI( "Switching from WiFi to SD card mode...");
        
        // Step 1: Deinit WiFi transport (releases SDIO slave)
        esp_err_t ret = wifi_hosted_deinit_transport();
        if (ret != ESP_OK) {
            LOGW( "WiFi deinit warning: %s (continuing)", esp_err_to_name(ret));
        }
        g_arbiter.wifi_transport_active = false;
        
        // Step 2: Deinitialize SDMMC host (critical for clean switch)
        LOGI( "Deinitializing SDMMC host (releases Slot 0)...");
        sdmmc_host_deinit();
        vTaskDelay(pdMS_TO_TICKS(200));  // Bus settling time
        
        // Step 3: Reinitialize SDMMC host for SD card mode
        LOGI( "Reinitializing SDMMC host for SD card mode...");
        ret = sdmmc_host_init();
        if (ret != ESP_OK) {
            LOGE( "Failed to reinit SDMMC host: %s", esp_err_to_name(ret));
            xSemaphoreGive(g_arbiter.mutex);
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Controller stabilization
    }
    
    // Initialize SD card
    LOGI( "Mounting SD card...");
    esp_err_t ret = sd_card_mount_safe(&g_arbiter.sd_card);
    if (ret != ESP_OK) {
        LOGE( "Failed to mount SD card: %s", esp_err_to_name(ret));
        xSemaphoreGive(g_arbiter.mutex);
        return ret;
    }
    
    g_arbiter.current_mode = SDMMC_MODE_SD_CARD;
    g_arbiter.mode_switch_count++;
    
    if (card) *card = g_arbiter.sd_card;
    
    LOGI( "[OK] SD card mode active (switch #%u)", g_arbiter.mode_switch_count);
    
    xSemaphoreGive(g_arbiter.mutex);
    return ESP_OK;
}

/**
 * Release WiFi mode
 */
esp_err_t sdmmc_arbiter_release_wifi(void)
{
    LOGI( "Releasing WiFi mode...");
    
    if (xSemaphoreTake(g_arbiter.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGE( "Timeout acquiring mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (g_arbiter.current_mode != SDMMC_MODE_WIFI) {
        LOGW( "Not in WiFi mode, nothing to release");
        xSemaphoreGive(g_arbiter.mutex);
        return ESP_OK;
    }
    
    // Deinitialize WiFi transport
    esp_err_t ret = wifi_hosted_deinit_transport();
    if (ret != ESP_OK) {
        LOGW( "WiFi deinit warning: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize SDMMC host
    sdmmc_host_deinit();
    
    g_arbiter.current_mode = SDMMC_MODE_NONE;
    g_arbiter.wifi_transport_active = false;
    
    LOGI( "[OK] WiFi mode released");
    
    xSemaphoreGive(g_arbiter.mutex);
    return ESP_OK;
}

/**
 * Release SD card mode
 */
esp_err_t sdmmc_arbiter_release_sd_card(void)
{
    LOGI( "Releasing SD card mode...");
    
    if (xSemaphoreTake(g_arbiter.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGE( "Timeout acquiring mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (g_arbiter.current_mode != SDMMC_MODE_SD_CARD) {
        LOGW( "Not in SD card mode, nothing to release");
        xSemaphoreGive(g_arbiter.mutex);
        return ESP_OK;
    }
    
    // Unmount SD card
    esp_err_t ret = sd_card_unmount_safe();
    if (ret != ESP_OK) {
        LOGW( "SD unmount warning: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize SDMMC host
    sdmmc_host_deinit();
    
    g_arbiter.current_mode = SDMMC_MODE_NONE;
    g_arbiter.sd_card = NULL;
    
    LOGI( "[OK] SD card mode released");
    
    xSemaphoreGive(g_arbiter.mutex);
    return ESP_OK;
}

/**
 * Get current bus mode
 */
sdmmc_bus_mode_t sdmmc_arbiter_get_mode(void)
{
    return g_arbiter.current_mode;
}

/**
 * Save current mode to NVS
 */
esp_err_t sdmmc_arbiter_save_mode(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        LOGE( "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_set_u8(nvs_handle, NVS_MODE_KEY, (uint8_t)g_arbiter.current_mode);
    if (ret != ESP_OK) {
        LOGE( "Failed to write NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (ret == ESP_OK) {
        LOGI( "Saved mode to NVS: %d", g_arbiter.current_mode);
    }
    
    return ret;
}

/**
 * Load preferred mode from NVS
 */
esp_err_t sdmmc_arbiter_load_mode(sdmmc_bus_mode_t *mode)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;  // No saved mode
    }
    
    uint8_t stored_mode = 0;
    ret = nvs_get_u8(nvs_handle, NVS_MODE_KEY, &stored_mode);
    nvs_close(nvs_handle);
    
    if (ret == ESP_OK) {
        *mode = (sdmmc_bus_mode_t)stored_mode;
    }
    
    return ret;
}

/**
 * Deinitialize arbiter
 */
void sdmmc_arbiter_deinit(void)
{
    if (g_arbiter.mutex) {
        // Release current mode
        if (g_arbiter.current_mode == SDMMC_MODE_WIFI) {
            sdmmc_arbiter_release_wifi();
        } else if (g_arbiter.current_mode == SDMMC_MODE_SD_CARD) {
            sdmmc_arbiter_release_sd_card();
        }
        
        vSemaphoreDelete(g_arbiter.mutex);
        g_arbiter.mutex = NULL;
    }
    
    LOGI( "SDMMC arbiter deinitialized");
}
