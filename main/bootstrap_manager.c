/*
 * Bootstrap Manager Implementation - Three-Phase Init
 * 
 * CORRECT v1.0.0-beta sequence restored:
 * Phase A: Power → Phase B: SD Card → Phase C: WiFi
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

// External functions
extern esp_err_t sd_card_mount_safe(sdmmc_card_t **card);

static const char *TAG = "BOOTSTRAP";

/**
 * Phase A: Power Management Task
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
        ESP_LOGW(TAG, "[Phase A]   C6_IO9 strapping pin not mapped");
    }
    
    // Step 2: Wait for power rail stabilization
    ESP_LOGI(TAG, "[Phase A] Waiting %dms for rail stabilization...", BOOTSTRAP_POWER_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOTSTRAP_POWER_STABILIZATION_MS));
    
    // Step 3: Power-on sequence
    ESP_LOGI(TAG, "[Phase A] Power-on sequence starting...");
    
    // SD card first
    gpio_set_level(GPIO_SD_POWER_EN, 1);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (SD_POWER_EN) → HIGH (SD powered)", GPIO_SD_POWER_EN);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // C6 stays in reset for now (WiFi will release it in Phase C)
    
    // Step 4: Signal completion
    ESP_LOGI(TAG, "[Phase A] ✓ POWER_READY");
    xEventGroupSetBits(manager->event_group, BOOTSTRAP_POWER_READY_BIT);
    
    vTaskDelete(NULL);
}

/**
 * Phase B: SD Manager Task (v1.0.0-beta order: SD BEFORE WiFi)
 */
static void bootstrap_sd_manager_task(void *arg)
{
    bootstrap_manager_t *manager = (bootstrap_manager_t *)arg;
    
    ESP_LOGI(TAG, "[Phase B] SD Manager waiting for POWER_READY...");
    
    // Wait for Phase A
    EventBits_t bits = xEventGroupWaitBits(
        manager->event_group,
        BOOTSTRAP_POWER_READY_BIT | BOOTSTRAP_FAILURE_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY
    );
    
    if (bits & BOOTSTRAP_FAILURE_BIT) {
        ESP_LOGE(TAG, "[Phase B] Phase A failed, aborting");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase B] Starting SD card mount...");
    
    // Mount SD card (controller not yet initialized by WiFi)
    esp_err_t ret = sd_card_mount_safe(&manager->sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase B] SD mount failed: %s", esp_err_to_name(ret));
        xEventGroupSetBits(manager->event_group, BOOTSTRAP_FAILURE_BIT);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase B] ✓ SD_READY");
    xEventGroupSetBits(manager->event_group, BOOTSTRAP_SD_READY_BIT);
    
    vTaskDelete(NULL);
}

/**
 * Phase C: WiFi Hosted Manager Task (v1.0.0-beta order: WiFi AFTER SD)
 */
static void bootstrap_wifi_manager_task(void *arg)
{
    bootstrap_manager_t *manager = (bootstrap_manager_t *)arg;
    
    ESP_LOGI(TAG, "[Phase C] WiFi Manager waiting for SD_READY...");
    
    // Wait for Phase B (SD mount complete)
    EventBits_t bits = xEventGroupWaitBits(
        manager->event_group,
        BOOTSTRAP_SD_READY_BIT | BOOTSTRAP_FAILURE_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY
    );
    
    if (bits & BOOTSTRAP_FAILURE_BIT) {
        ESP_LOGE(TAG, "[Phase C] Phase B failed, aborting");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase C] Starting WiFi Hosted init...");
    
    // Now release C6 from reset
    if (GPIO_C6_IO9_STRAPPING >= 0) {
        if (gpio_get_level(GPIO_C6_IO9_STRAPPING) != 1) {
            ESP_LOGE(TAG, "[Phase C] CRITICAL: C6_IO9 not high!");
            xEventGroupSetBits(manager->event_group, BOOTSTRAP_FAILURE_BIT);
            vTaskDelete(NULL);
            return;
        }
    }
    
    gpio_set_level(GPIO_C6_CHIP_PU, 1);
    ESP_LOGI(TAG, "[Phase C]   GPIO%d (C6_CHIP_PU) → HIGH (C6 released)", GPIO_C6_CHIP_PU);
    
    // Configure GPIO6 for handshake
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_C6_IO2_HANDSHAKE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Wait for C6 firmware boot
    ESP_LOGI(TAG, "[Phase C] Waiting for C6 firmware ready (GPIO%d)...", GPIO_C6_IO2_HANDSHAKE);
    int timeout_count = 0;
    while (timeout_count < (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout_count++;
    }
    
    if (timeout_count >= (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        ESP_LOGW(TAG, "[Phase C] C6 handshake timeout (proceeding anyway)");
    }
    
    // Request WiFi mode from arbiter
    ESP_LOGI(TAG, "[Phase C] Requesting WiFi mode from arbiter...");
    esp_err_t ret = sdmmc_arbiter_request_wifi(10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase C] WiFi mode request failed: %s", esp_err_to_name(ret));
        xEventGroupSetBits(manager->event_group, BOOTSTRAP_FAILURE_BIT);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase C] ✓ HOSTED_READY");
    xEventGroupSetBits(manager->event_group, BOOTSTRAP_HOSTED_READY_BIT);
    
    vTaskDelete(NULL);
}

/**
 * Detect warm boot
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
    ESP_LOGW(TAG, "Forcing complete power-down...");
    
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    io_conf.pin_bit_mask = (1ULL << GPIO_C6_CHIP_PU);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_C6_CHIP_PU, 0);
    ESP_LOGW(TAG, "  GPIO%d (C6_CHIP_PU) → LOW", GPIO_C6_CHIP_PU);
    
    io_conf.pin_bit_mask = (1ULL << GPIO_SD_POWER_EN);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_SD_POWER_EN, 0);
    ESP_LOGW(TAG, "  GPIO%d (SD_POWER_EN) → LOW", GPIO_SD_POWER_EN);
    
    ESP_LOGW(TAG, "  Waiting %dms for capacitor discharge...", BOOTSTRAP_HARD_RESET_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOTSTRAP_HARD_RESET_DELAY_MS));
    
    ESP_LOGW(TAG, "Hard reset complete");
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
    ESP_LOGI(TAG, "  Bootstrap Manager v1.1.0-fixed");
    ESP_LOGI(TAG, "  Three-Phase: Power → SD → WiFi");
    ESP_LOGI(TAG, "========================================");
    
    manager->boot_timestamp_ms = esp_timer_get_time() / 1000;
    
    // Initialize arbiter (for runtime switching API)
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
    
    // Spawn Phase A task (Power)
    BaseType_t task_ret = xTaskCreate(
        bootstrap_power_manager_task,
        "bootstrap_power",
        4096, manager,
        BOOTSTRAP_POWER_TASK_PRIORITY,
        &manager->power_task_handle
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    // Spawn Phase B task (SD - before WiFi!)
    task_ret = xTaskCreate(
        bootstrap_sd_manager_task,
        "bootstrap_sd",
        4096, manager,
        BOOTSTRAP_SD_TASK_PRIORITY,
        &manager->sd_task_handle
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SD task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    // Spawn Phase C task (WiFi - after SD!)
    task_ret = xTaskCreate(
        bootstrap_wifi_manager_task,
        "bootstrap_wifi",
        4096, manager,
        BOOTSTRAP_WIFI_TASK_PRIORITY,
        &manager->wifi_task_handle
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Three-phase bootstrap tasks spawned");
    ESP_LOGI(TAG, "  Phase A: Power (priority %d)", BOOTSTRAP_POWER_TASK_PRIORITY);
    ESP_LOGI(TAG, "  Phase B: SD (priority %d)", BOOTSTRAP_SD_TASK_PRIORITY);
    ESP_LOGI(TAG, "  Phase C: WiFi (priority %d)", BOOTSTRAP_WIFI_TASK_PRIORITY);
    
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
                                       BOOTSTRAP_SD_READY_BIT |
                                       BOOTSTRAP_HOSTED_READY_BIT;
    
    EventBits_t bits = xEventGroupWaitBits(
        manager->event_group,
        ALL_READY_BITS,
        pdFALSE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms)
    );
    
    // Check for failure
    bits = xEventGroupGetBits(manager->event_group);
    if (bits & BOOTSTRAP_FAILURE_BIT) {
        ESP_LOGE(TAG, "Bootstrap FAILED!");
        return ESP_FAIL;
    }
    
    // Check if all three phases completed
    if ((bits & ALL_READY_BITS) == ALL_READY_BITS) {
        uint32_t elapsed_ms = (esp_timer_get_time() / 1000) - manager->boot_timestamp_ms;
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  Bootstrap COMPLETE (%u ms)", elapsed_ms);
        ESP_LOGI(TAG, "  Phase A: Power ✓");
        ESP_LOGI(TAG, "  Phase B: SD card ✓");
        ESP_LOGI(TAG, "  Phase C: WiFi ✓");
        ESP_LOGI(TAG, "========================================");
        return ESP_OK;
    }
    
    // Timeout
    ESP_LOGE(TAG, "Bootstrap timeout!");
    if (!(bits & BOOTSTRAP_POWER_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase A (Power) did not complete");
    }
    if (!(bits & BOOTSTRAP_SD_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase B (SD) did not complete");
    }
    if (!(bits & BOOTSTRAP_HOSTED_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase C (WiFi) did not complete");
    }
    
    return ESP_ERR_TIMEOUT;
}

/**
 * Get SD card handle
 */
sdmmc_card_t* bootstrap_manager_get_sd_card(bootstrap_manager_t *manager)
{
    if (!manager) {
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
