/*
 * Bootstrap Manager Implementation with SDMMC Arbiter
 * 
 * Two-phase coordinated initialization + on-demand SD access.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bootstrap_manager.h"
#include "sdmmc_arbiter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

static const char *TAG = "BOOTSTRAP";

/**
 * Phase A: Power Management Task
 * 
 * Responsibilities:
 * 1. Force GPIO isolation (C6 reset low, SD power off)
 * 2. Protect C6 strapping pins (IO9 high for SPI boot)
 * 3. Wait for rail stabilization
 * 4. Power-on sequence (SD first, then C6)
 * 5. Signal POWER_READY
 */
static void bootstrap_power_manager_task(void *arg)
{
    bootstrap_manager_t *manager = (bootstrap_manager_t *)arg;
    
    ESP_LOGI(TAG, "[Phase A] Power Manager starting...");
    
    // Step 1: GPIO isolation (pre-initialization guard)
    ESP_LOGI(TAG, "[Phase A] Forcing GPIO isolation...");
    
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    // C6 reset (hold in reset)
    io_conf.pin_bit_mask = (1ULL << GPIO_C6_CHIP_PU);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_C6_CHIP_PU, 0);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (C6_CHIP_PU) → LOW (C6 in reset)", GPIO_C6_CHIP_PU);
    
    // SD power (cut power)
    io_conf.pin_bit_mask = (1ULL << GPIO_SD_POWER_EN);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_SD_POWER_EN, 0);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (SD_POWER_EN) → LOW (SD unpowered)", GPIO_SD_POWER_EN);
    
    // C6_IO9 strapping protection (force high for SPI boot)
    if (GPIO_C6_IO9_STRAPPING >= 0) {
        io_conf.pin_bit_mask = (1ULL << GPIO_C6_IO9_STRAPPING);
        gpio_config(&io_conf);
        gpio_set_level(GPIO_C6_IO9_STRAPPING, 1);
        ESP_LOGI(TAG, "[Phase A]   GPIO%d (C6_IO9) → HIGH (SPI boot mode)", GPIO_C6_IO9_STRAPPING);
    } else {
        ESP_LOGW(TAG, "[Phase A]   C6_IO9 strapping pin not mapped (check schematic)");
    }
    
    // Step 2: Wait for power rail stabilization
    ESP_LOGI(TAG, "[Phase A] Waiting %dms for rail stabilization...", BOOTSTRAP_POWER_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOTSTRAP_POWER_STABILIZATION_MS));
    
    // Step 3: Power-on sequence
    ESP_LOGI(TAG, "[Phase A] Power-on sequence starting...");
    
    // SD card first (no dependencies)
    gpio_set_level(GPIO_SD_POWER_EN, 1);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (SD_POWER_EN) → HIGH (SD powered)", GPIO_SD_POWER_EN);
    vTaskDelay(pdMS_TO_TICKS(50)); // SD power-on settling
    
    // C6 release from reset (start firmware boot)
    if (GPIO_C6_IO9_STRAPPING >= 0) {
        ESP_LOGI(TAG, "[Phase A]   Verifying C6_IO9 is HIGH before C6 release...");
        if (gpio_get_level(GPIO_C6_IO9_STRAPPING) != 1) {
            ESP_LOGE(TAG, "[Phase A]   CRITICAL: C6_IO9 not high! C6 may boot in wrong mode!");
            xEventGroupSetBits(manager->event_group, BOOTSTRAP_FAILURE_BIT);
            vTaskDelete(NULL);
            return;
        }
    }
    
    gpio_set_level(GPIO_C6_CHIP_PU, 1);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (C6_CHIP_PU) → HIGH (C6 released from reset)", GPIO_C6_CHIP_PU);
    
    // Step 4: Signal completion
    ESP_LOGI(TAG, "[Phase A] Power-on complete, signaling POWER_READY");
    xEventGroupSetBits(manager->event_group, BOOTSTRAP_POWER_READY_BIT);
    
    ESP_LOGI(TAG, "[Phase A] Power Manager task exiting");
    vTaskDelete(NULL);
}

/**
 * Phase B: WiFi Hosted Manager Task (with SDMMC Arbiter)
 * 
 * Responsibilities:
 * 1. Wait for POWER_READY event
 * 2. Monitor C6 handshake (GPIO6 toggle)
 * 3. Request WiFi mode from SDMMC arbiter
 * 4. Arbiter initializes ESP-Hosted and configures SDMMC for SDIO
 * 5. Signal HOSTED_READY
 */
static void bootstrap_wifi_manager_task(void *arg)
{
    bootstrap_manager_t *manager = (bootstrap_manager_t *)arg;
    
    ESP_LOGI(TAG, "[Phase B] WiFi Manager waiting for POWER_READY...");
    
    // Wait for Phase A completion
    EventBits_t bits = xEventGroupWaitBits(
        manager->event_group,
        BOOTSTRAP_POWER_READY_BIT | BOOTSTRAP_FAILURE_BIT,
        pdFALSE,  // Don't clear on exit
        pdFALSE,  // Wait for any bit
        portMAX_DELAY
    );
    
    if (bits & BOOTSTRAP_FAILURE_BIT) {
        ESP_LOGE(TAG, "[Phase B] Phase A failed, aborting");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase B] POWER_READY received, starting WiFi Hosted init...");
    
    // Configure GPIO6 as input for C6 handshake monitoring
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_C6_IO2_HANDSHAKE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Wait for C6 firmware boot
    ESP_LOGI(TAG, "[Phase B] Waiting for C6 firmware ready signal (GPIO%d)...", GPIO_C6_IO2_HANDSHAKE);
    int timeout_count = 0;
    while (timeout_count < (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout_count++;
    }
    
    if (timeout_count >= (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        ESP_LOGW(TAG, "[Phase B] C6 handshake timeout (proceeding anyway)");
    } else {
        ESP_LOGI(TAG, "[Phase B] C6 ready signal detected");
    }
    
    // Request WiFi mode from SDMMC arbiter
    ESP_LOGI(TAG, "[Phase B] Requesting WiFi mode from SDMMC arbiter...");
    esp_err_t ret = sdmmc_arbiter_request_wifi(10000);  // 10s timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase B] Arbiter WiFi mode request failed: %s", esp_err_to_name(ret));
        xEventGroupSetBits(manager->event_group, BOOTSTRAP_FAILURE_BIT);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase B] WiFi mode granted by arbiter (SDMMC configured for SDIO)");
    
    // Signal completion
    ESP_LOGI(TAG, "[Phase B] Signaling HOSTED_READY");
    xEventGroupSetBits(manager->event_group, BOOTSTRAP_HOSTED_READY_BIT);
    
    ESP_LOGI(TAG, "[Phase B] WiFi Manager task exiting");
    vTaskDelete(NULL);
}

/**
 * Detect warm boot (software reset vs cold boot)
 */
bool bootstrap_is_warm_boot(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "Cold boot detected (power-on reset)");
            return false;
            
        case ESP_RST_SW:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_DEEPSLEEP:
        case ESP_RST_BROWNOUT:
            ESP_LOGW(TAG, "Warm boot detected (reset reason: %d)", reason);
            return true;
            
        default:
            ESP_LOGW(TAG, "Unknown reset reason: %d (treating as warm boot)", reason);
            return true;
    }
}

/**
 * Perform hard reset cycle
 */
void bootstrap_hard_reset(void)
{
    ESP_LOGW(TAG, "=== HARD RESET CYCLE ===");
    ESP_LOGW(TAG, "Forcing complete power-down to clear hardware state...");
    
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    // Force C6 into reset
    io_conf.pin_bit_mask = (1ULL << GPIO_C6_CHIP_PU);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_C6_CHIP_PU, 0);
    ESP_LOGW(TAG, "  GPIO%d (C6_CHIP_PU) → LOW", GPIO_C6_CHIP_PU);
    
    // Cut SD power
    io_conf.pin_bit_mask = (1ULL << GPIO_SD_POWER_EN);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_SD_POWER_EN, 0);
    ESP_LOGW(TAG, "  GPIO%d (SD_POWER_EN) → LOW", GPIO_SD_POWER_EN);
    
    // Wait for capacitor discharge
    ESP_LOGW(TAG, "  Waiting %dms for capacitor discharge...", BOOTSTRAP_HARD_RESET_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOTSTRAP_HARD_RESET_DELAY_MS));
    
    ESP_LOGW(TAG, "Hard reset complete, ready for clean init");
}

/**
 * Initialize bootstrap manager
 */
esp_err_t bootstrap_manager_init(bootstrap_manager_t *manager)
{
    if (!manager) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bootstrap Manager v1.2.0-arbiter");
    ESP_LOGI(TAG, "  Two-Phase Init + SDMMC Arbiter");
    ESP_LOGI(TAG, "========================================");
    
    // Record boot timestamp
    manager->boot_timestamp_ms = esp_timer_get_time() / 1000;
    
    // Initialize SDMMC arbiter FIRST (before any SDMMC operations)
    ESP_LOGI(TAG, "Initializing SDMMC arbiter...");
    esp_err_t ret = sdmmc_arbiter_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Arbiter init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Detect warm boot
    manager->warm_boot_detected = bootstrap_is_warm_boot();
    
    if (manager->warm_boot_detected) {
        ESP_LOGW(TAG, "Warm boot detected, performing hard reset...");
        bootstrap_hard_reset();
    }
    
    // Create event group
    manager->event_group = xEventGroupCreate();
    if (!manager->event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }
    
    // Spawn Phase A task (highest priority)
    BaseType_t task_ret = xTaskCreate(
        bootstrap_power_manager_task,
        "bootstrap_power",
        4096,
        manager,
        BOOTSTRAP_POWER_TASK_PRIORITY,
        &manager->power_task_handle
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power manager task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    // Spawn Phase B task
    task_ret = xTaskCreate(
        bootstrap_wifi_manager_task,
        "bootstrap_wifi",
        4096,
        manager,
        BOOTSTRAP_WIFI_TASK_PRIORITY,
        &manager->wifi_task_handle
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi manager task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    // Note: Phase C (SD manager) removed - SD access via arbiter API
    
    ESP_LOGI(TAG, "Two-phase bootstrap tasks spawned");
    ESP_LOGI(TAG, "  Phase A: Power Manager (priority %d)", BOOTSTRAP_POWER_TASK_PRIORITY);
    ESP_LOGI(TAG, "  Phase B: WiFi Manager (priority %d)", BOOTSTRAP_WIFI_TASK_PRIORITY);
    ESP_LOGI(TAG, "  Note: SD card available via sdmmc_arbiter_request_sd_card()");
    
    return ESP_OK;
}

/**
 * Wait for bootstrap completion
 */
esp_err_t bootstrap_manager_wait(bootstrap_manager_t *manager, uint32_t timeout_ms)
{
    if (!manager || !manager->event_group) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Waiting for bootstrap completion (timeout: %ums)...", timeout_ms);
    
    const EventBits_t ALL_READY_BITS = BOOTSTRAP_POWER_READY_BIT | 
                                       BOOTSTRAP_HOSTED_READY_BIT;
    
    EventBits_t bits = xEventGroupWaitBits(
        manager->event_group,
        ALL_READY_BITS,
        pdFALSE,
        pdTRUE,  // Wait for ALL bits
        pdMS_TO_TICKS(timeout_ms)
    );
    
    // Check for failure
    bits = xEventGroupGetBits(manager->event_group);
    if (bits & BOOTSTRAP_FAILURE_BIT) {
        ESP_LOGE(TAG, "Bootstrap FAILED!");
        return ESP_FAIL;
    }
    
    // Check if both phases completed
    if ((bits & ALL_READY_BITS) == ALL_READY_BITS) {
        uint32_t elapsed_ms = (esp_timer_get_time() / 1000) - manager->boot_timestamp_ms;
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  Bootstrap COMPLETE (%u ms)", elapsed_ms);
        ESP_LOGI(TAG, "  WiFi: READY (SDMMC in SDIO mode)");
        ESP_LOGI(TAG, "  SD card: Available on-demand via arbiter");
        ESP_LOGI(TAG, "========================================");
        return ESP_OK;
    }
    
    // Timeout
    ESP_LOGE(TAG, "Bootstrap timeout!");
    if (!(bits & BOOTSTRAP_POWER_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase A (Power) did not complete");
    }
    if (!(bits & BOOTSTRAP_HOSTED_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase B (WiFi Hosted) did not complete");
    }
    
    return ESP_ERR_TIMEOUT;
}

/**
 * Get SD card handle (via arbiter)
 */
sdmmc_card_t* bootstrap_manager_get_sd_card(bootstrap_manager_t *manager)
{
    if (!manager) {
        return NULL;
    }
    
    // Check current mode
    if (sdmmc_arbiter_get_mode() == SDMMC_MODE_SD_CARD) {
        return manager->sd_card;
    }
    
    // Request SD mode from arbiter
    ESP_LOGI(TAG, "Switching to SD card mode...");
    esp_err_t ret = sdmmc_arbiter_request_sd_card(5000, &manager->sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch to SD mode: %s", esp_err_to_name(ret));
        return NULL;
    }
    
    return manager->sd_card;
}

/**
 * Clean up
 */
void bootstrap_manager_deinit(bootstrap_manager_t *manager)
{
    if (!manager) {
        return;
    }
    
    if (manager->event_group) {
        vEventGroupDelete(manager->event_group);
        manager->event_group = NULL;
    }
    
    sdmmc_arbiter_deinit();
    
    ESP_LOGI(TAG, "Bootstrap manager deinitialized");
}
