/*
 * Bootstrap Manager Implementation
 * 
 * Three-phase coordinated initialization with FreeRTOS event groups.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bootstrap_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

static const char *TAG = "BOOTSTRAP";

/* External functions (to be implemented in wifi_hosted.c and sd_card_manager.c) */
extern esp_err_t wifi_hosted_init_transport(void);
extern esp_err_t sd_card_mount_safe(sdmmc_card_t **card);

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
    // Only configure if GPIO is valid (not -1)
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
 * Phase B: WiFi Hosted Manager Task
 * 
 * Responsibilities:
 * 1. Wait for POWER_READY event
 * 2. Monitor C6 handshake (GPIO6 toggle)
 * 3. Initialize ESP-Hosted transport (SDIO)
 * 4. Wait for "Transport active" confirmation + link stabilization
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
    
    // Wait for C6 firmware boot (poll GPIO6 or use timeout)
    ESP_LOGI(TAG, "[Phase B] Waiting for C6 firmware ready signal (GPIO%d)...", GPIO_C6_IO2_HANDSHAKE);
    int timeout_count = 0;
    while (timeout_count < (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        // C6 firmware typically toggles or holds this pin high when ready
        // For now, just wait for timeout (proper handshake TBD)
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout_count++;
        
        // Early exit if C6 signals ready (implementation-specific)
        // int level = gpio_get_level(GPIO_C6_IO2_HANDSHAKE);
        // if (level == 1) break;
    }
    
    if (timeout_count >= (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        ESP_LOGW(TAG, "[Phase B] C6 handshake timeout (proceeding anyway)");
    } else {
        ESP_LOGI(TAG, "[Phase B] C6 ready signal detected");
    }
    
    // Initialize ESP-Hosted transport
    ESP_LOGI(TAG, "[Phase B] Initializing ESP-Hosted SDIO transport...");
    esp_err_t ret = wifi_hosted_init_transport();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase B] WiFi Hosted init failed: %s", esp_err_to_name(ret));
        xEventGroupSetBits(manager->event_group, BOOTSTRAP_FAILURE_BIT);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase B] WiFi Hosted transport configured");
    
    // CRITICAL FIX: Wait for ESP-Hosted link to stabilize
    // The transport init returns ESP_OK when configuration is done,
    // but SDIO link needs time to establish proper communication.
    // Without this delay, "ESP-Hosted link not yet up" error occurs.
    // Increased from 2s to 5s based on observed "link not yet up" at +22s.
    ESP_LOGI(TAG, "[Phase B] Waiting %dms for SDIO link stabilization...", 
             BOOTSTRAP_WIFI_LINK_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOTSTRAP_WIFI_LINK_STABILIZATION_MS));
    
    ESP_LOGI(TAG, "[Phase B] WiFi Hosted transport active and stable");
    
    // Signal completion
    ESP_LOGI(TAG, "[Phase B] Signaling HOSTED_READY");
    xEventGroupSetBits(manager->event_group, BOOTSTRAP_HOSTED_READY_BIT);
    
    ESP_LOGI(TAG, "[Phase B] WiFi Manager task exiting");
    vTaskDelete(NULL);
}

/**
 * Phase C: SD Manager Task
 * 
 * Responsibilities:
 * 1. Wait for HOSTED_READY event (SDMMC bus is safe)
 * 2. Enable pull-ups on SDMMC pins (prevent floating)
 * 3. Mount SD filesystem (reuses SDMMC host from WiFi)
 * 4. Signal SD_READY
 */
static void bootstrap_sd_manager_task(void *arg)
{
    bootstrap_manager_t *manager = (bootstrap_manager_t *)arg;
    
    ESP_LOGI(TAG, "[Phase C] SD Manager waiting for HOSTED_READY...");
    
    // Wait for Phase B completion
    EventBits_t bits = xEventGroupWaitBits(
        manager->event_group,
        BOOTSTRAP_HOSTED_READY_BIT | BOOTSTRAP_FAILURE_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );
    
    if (bits & BOOTSTRAP_FAILURE_BIT) {
        ESP_LOGE(TAG, "[Phase C] Phase B failed, aborting");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase C] HOSTED_READY received, SDMMC bus is safe");
    
    // Enable pull-ups on SDMMC Slot 0 pins (GPIO39-44)
    ESP_LOGI(TAG, "[Phase C] Enabling pull-ups on SDMMC pins (GPIO39-44)...");
    for (int gpio = 39; gpio <= 44; gpio++) {
        gpio_pullup_en(gpio);
    }
    
    // Mount SD card filesystem
    ESP_LOGI(TAG, "[Phase C] Mounting SD card filesystem...");
    esp_err_t ret = sd_card_mount_safe(&manager->sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase C] SD mount failed: %s", esp_err_to_name(ret));
        xEventGroupSetBits(manager->event_group, BOOTSTRAP_FAILURE_BIT);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "[Phase C] SD card mounted successfully");
    
    // Signal completion
    ESP_LOGI(TAG, "[Phase C] Signaling SD_READY");
    xEventGroupSetBits(manager->event_group, BOOTSTRAP_SD_READY_BIT);
    
    ESP_LOGI(TAG, "[Phase C] SD Manager task exiting");
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
    
    // Configure GPIOs as outputs
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
    ESP_LOGI(TAG, "  Bootstrap Manager v1.1.0-dev");
    ESP_LOGI(TAG, "  Deterministic Three-Phase Init");
    ESP_LOGI(TAG, "========================================");
    
    // Record boot timestamp
    manager->boot_timestamp_ms = esp_timer_get_time() / 1000;
    
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
    BaseType_t ret = xTaskCreate(
        bootstrap_power_manager_task,
        "bootstrap_power",
        4096,
        manager,
        BOOTSTRAP_POWER_TASK_PRIORITY,
        &manager->power_task_handle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power manager task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    // Spawn Phase B task
    ret = xTaskCreate(
        bootstrap_wifi_manager_task,
        "bootstrap_wifi",
        4096,
        manager,
        BOOTSTRAP_WIFI_TASK_PRIORITY,
        &manager->wifi_task_handle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi manager task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    // Spawn Phase C task
    ret = xTaskCreate(
        bootstrap_sd_manager_task,
        "bootstrap_sd",
        4096,
        manager,
        BOOTSTRAP_SD_TASK_PRIORITY,
        &manager->sd_task_handle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SD manager task");
        vEventGroupDelete(manager->event_group);
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Three-phase bootstrap tasks spawned");
    ESP_LOGI(TAG, "  Phase A: Power Manager (priority %d)", BOOTSTRAP_POWER_TASK_PRIORITY);
    ESP_LOGI(TAG, "  Phase B: WiFi Manager (priority %d)", BOOTSTRAP_WIFI_TASK_PRIORITY);
    ESP_LOGI(TAG, "  Phase C: SD Manager (priority %d)", BOOTSTRAP_SD_TASK_PRIORITY);
    
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
                                       BOOTSTRAP_HOSTED_READY_BIT | 
                                       BOOTSTRAP_SD_READY_BIT;
    
    // CRITICAL FIX: Wait for ALL_READY_BITS only (not including FAILURE_BIT)
    // If FAILURE_BIT was included with pdTRUE, it would wait for:
    // POWER_READY AND HOSTED_READY AND SD_READY AND FAILURE (impossible!)
    // causing 30s timeout every time.
    EventBits_t bits = xEventGroupWaitBits(
        manager->event_group,
        ALL_READY_BITS,  // *** FIXED: Only wait for success bits ***
        pdFALSE,         // Don't clear bits
        pdTRUE,          // Wait for ALL bits (AND logic)
        pdMS_TO_TICKS(timeout_ms)
    );
    
    // Check for failure after wait completes
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
        ESP_LOGI(TAG, "========================================");
        return ESP_OK;
    }
    
    // Timeout - report which phases didn't complete
    ESP_LOGE(TAG, "Bootstrap timeout!");
    if (!(bits & BOOTSTRAP_POWER_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase A (Power) did not complete");
    }
    if (!(bits & BOOTSTRAP_HOSTED_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase B (WiFi Hosted) did not complete");
    }
    if (!(bits & BOOTSTRAP_SD_READY_BIT)) {
        ESP_LOGE(TAG, "  Phase C (SD Card) did not complete");
    }
    ESP_LOGE(TAG, "  Event bits: 0x%x (expected: 0x%x)", bits, ALL_READY_BITS);
    
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
    
    ESP_LOGI(TAG, "Bootstrap manager deinitialized");
}
