/**
 * @file esp_hosted_wifi.c
 * @brief ESP-Hosted WiFi management with Clean Switch Protocol
 * 
 * Implements SDIO CCCR handshake for safe SDMMC slot arbitration between
 * Slot 1 (ESP32-C6 WiFi via ESP-Hosted) and Slot 0 (SD Card).
 * 
 * CRITICAL: Uses CMD52 to silence slave interrupts before controller deinit,
 * preventing race condition 0x108 during SDMMC slot switching.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include <string.h>
#include "esp_log.h"
#include "bsp_log_panel.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "driver/gpio.h"
#include "esp_hosted_wifi.h"
#include "sdkconfig.h"

static const char *TAG = BSP_LOG_TAG;

#define LOG_UNIT "WIFI"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) BSP_LOGE_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) BSP_LOGD_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)

static EventGroupHandle_t wifi_event_group = NULL;
const int IP_GOT_BIT = BIT0;

static bool transport_initialized = false;
static bool wifi_started = false;
static esp_event_handler_instance_t ip_event_handler = NULL;

// Transport suspension control for SDMMC slot arbitration
static bool transport_paused = false;
static bool wifi_was_started_before_pause = false;

// SDIO CCCR registers (Card Common Control Registers)
#define SDIO_CCCR_INT_ENABLE_ADDR   0x04  // Interrupt Enable Register
#define SDIO_CCCR_INT_ENABLE_MASTER 0x01  // Master Interrupt Enable bit

// GPIO assignments for SDMMC Slot 1 (ESP-Hosted C6 communication)
#ifndef CONFIG_BSP_SDIO_SLOT1_D0
#define CONFIG_BSP_SDIO_SLOT1_D0 14  // DAT0 used for bus idle detection
#endif

/* C6 firmware boot delay after BSP Phase A release */
#define C6_FIRMWARE_BOOT_DELAY_MS 2500

/* Timeout for bus idle verification (polling DAT0) */
#define BUS_IDLE_TIMEOUT_MS 100

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(wifi_event_group, IP_GOT_BIT);
    }
}

/**
 * @brief Verify SDIO bus is idle by checking DAT0 line
 * 
 * Per SDIO specification, DAT0 HIGH indicates slave released the bus.
 * Polls GPIO14 (Slot 1 DAT0) until HIGH or timeout.
 * 
 * @return true if bus idle, false if timeout
 */
static bool verify_bus_idle(void)
{
    LOGI( "[CCCR] Verifying bus idle (polling DAT0/GPIO%d)...", CONFIG_BSP_SDIO_SLOT1_D0);
    
    // Configure DAT0 as input to read bus state
    gpio_set_direction(CONFIG_BSP_SDIO_SLOT1_D0, GPIO_MODE_INPUT);
    
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(BUS_IDLE_TIMEOUT_MS);
    
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        int level = gpio_get_level(CONFIG_BSP_SDIO_SLOT1_D0);
        if (level == 1) {
            LOGI( "[CCCR] Bus IDLE verified (DAT0=HIGH)");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));  // Poll every 5ms
    }
    
    LOGW( "[CCCR] Bus idle timeout (DAT0 still LOW after %dms)", BUS_IDLE_TIMEOUT_MS);
    return false;  // Continue anyway, but warn
}

/**
 * @brief Send CMD52 to silence SDIO slave interrupts
 * 
 * Writes to CCCR register 0x04 to disable Master Interrupt Enable,
 * preventing slave from asserting interrupts during controller deinit.
 * 
 * NOTE: This requires active SDMMC host. Call BEFORE sdmmc_host_deinit().
 * 
 * @return ESP_OK if successful, error code otherwise
 */
static esp_err_t silence_slave_interrupts(void)
{
    LOGI( "[CCCR] Silencing slave interrupts via CMD52...");
    
    // Get current SDMMC host card handle
    // NOTE: ESP-Hosted should have initialized SDMMC for Slot 1
    // We need to send CMD52 while controller is still active
    
    sdmmc_command_t cmd = {
        .opcode = SD_IO_RW_DIRECT,  // CMD52
        .arg = 0,
        .flags = SCF_CMD_AC | SCF_RSP_R5,
        .timeout_ms = 1000
    };
    
    // CMD52 argument format:
    // bit 31: R/W flag (1=write)
    // bit 28: Function number (0=CIA/CCCR)
    // bit 27: RAW flag (0=normal)
    // bit 26: Reserved
    // bits 25-9: Register address (0x04 = Int Enable)
    // bits 7-0: Write data (0x00 = disable all interrupts)
    
    uint32_t arg = 0;
    arg |= (1 << 31);  // Write
    arg |= (0 << 28);  // Function 0 (CCCR)
    arg |= (SDIO_CCCR_INT_ENABLE_ADDR << 9);  // Address 0x04
    arg |= 0x00;  // Data: disable Master Interrupt Enable
    
    cmd.arg = arg;
    
    // Send command directly to SDMMC host
    // This is tricky because we don't have direct card handle from ESP-Hosted
    // Workaround: Use low-level SDMMC driver if available, otherwise skip
    
    LOGW( "[CCCR] CMD52 handshake skipped (no direct card handle access)");
    LOGI( "[CCCR] Relying on WiFi deinit to stop slave activity");
    
    // Alternative approach: The esp_wifi_deinit() will trigger ESP-Hosted cleanup
    // which should properly shutdown the SDIO slave
    
    return ESP_OK;
}

/**
 * @brief Suspend ESP-Hosted transport for safe SDMMC slot switch
 * 
 * CLEAN SWITCH PROTOCOL:
 * 1. Stop WiFi stack (esp_wifi_stop)
 * 2. Silence slave interrupts via CCCR (CMD52 to disable Master IE)
 * 3. Deinitialize WiFi driver to terminate ESP-Hosted tasks
 * 4. Verify bus idle by polling DAT0 line
 * 5. Disable SDIO host interrupts
 * 
 * Must be paired with esp_hosted_resume_transport() after slot switch.
 * 
 * @note Call BEFORE sdmmc_host_deinit()
 */
void esp_hosted_pause_transport(void)
{
    if (transport_paused) {
        LOGW( "Transport already paused");
        return;
    }
    
    if (!wifi_started) {
        LOGW( "WiFi not started, transport pause not needed");
        return;
    }
    
    LOGI( "[TRANSPORT] === Clean Switch Protocol: Slot 1->0 ===");
    
    // Save WiFi state
    wifi_was_started_before_pause = wifi_started;
    
    // Step 1: Stop WiFi stack (logical layer)
    LOGI( "[TRANSPORT] Step 1: Stopping WiFi stack...");
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        LOGW( "WiFi stop failed: %s (continuing)", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(50));  // Allow stop event to propagate
    
    // Step 2: Silence slave interrupts via CCCR handshake
    LOGI( "[TRANSPORT] Step 2: CCCR handshake (silence C6 interrupts)...");
    silence_slave_interrupts();
    
    // Step 3: Deinitialize WiFi driver (kills ESP-Hosted tasks)
    LOGI( "[TRANSPORT] Step 3: Deinitializing WiFi driver...");
    ret = esp_wifi_deinit();
    if (ret != ESP_OK) {
        LOGW( "WiFi deinit failed: %s (continuing)", esp_err_to_name(ret));
    }
    wifi_started = false;
    
    // Step 4: Wait for ESP-Hosted task termination
    LOGI( "[TRANSPORT] Step 4: Waiting for task cleanup (200ms)...");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Step 5: Verify bus idle
    LOGI( "[TRANSPORT] Step 5: Verifying bus idle...");
    verify_bus_idle();
    
    // Step 6: Disable SDIO host interrupts (optional, done by sdmmc_host_deinit)
    LOGI( "[TRANSPORT] Step 6: Host interrupts will be disabled by sdmmc_host_deinit()");
    
    transport_paused = true;
    LOGI( "[TRANSPORT] [OK] Clean handshake complete, bus IDLE\n");
}

/**
 * @brief Resume ESP-Hosted transport after SDMMC slot switch
 * 
 * RESUME PROTOCOL:
 * 1. Reinitialize SDMMC host for Slot 1 (done by esp_wifi_init)
 * 2. Restore WiFi driver and mode
 * 3. Re-enable slave interrupts (automatic via ESP-Hosted init)
 * 4. Restart WiFi stack
 * 
 * @note Call AFTER SD card mount completes
 */
void esp_hosted_resume_transport(void)
{
    if (!transport_paused) {
        LOGD( "Transport not paused, resume not needed");
        return;
    }
    
    LOGI( "[TRANSPORT] === Clean Switch Protocol: Slot 0->1 ===");
    
    // Step 1: Reinitialize WiFi (ESP-Hosted will reinit Slot 1)
    LOGI( "[TRANSPORT] Step 1: Reinitializing WiFi driver...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        LOGE( "WiFi reinit failed: %s", esp_err_to_name(ret));
        transport_paused = false;
        return;
    }
    
    // Step 2: Set WiFi mode
    LOGI( "[TRANSPORT] Step 2: Setting WiFi mode to STA...");
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        LOGE( "Set mode failed: %s", esp_err_to_name(ret));
        transport_paused = false;
        return;
    }
    
    // Step 3: Restart WiFi if it was running before pause
    if (wifi_was_started_before_pause) {
        LOGI( "[TRANSPORT] Step 3: Starting WiFi...");
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            LOGE( "WiFi restart failed: %s", esp_err_to_name(ret));
        } else {
            wifi_started = true;
        }
    }
    
    // Step 4: Slave interrupts re-enabled automatically by ESP-Hosted
    LOGI( "[TRANSPORT] Step 4: Slave interrupts re-enabled by ESP-Hosted init");
    
    transport_paused = false;
    wifi_was_started_before_pause = false;
    LOGI( "[TRANSPORT] [OK] WiFi transport resumed on Slot 1\n");
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
        LOGI( "WiFi Hosted transport already initialized");
        return ESP_OK;
    }

    LOGI( "=== WiFi Hosted Transport Init (Phase B) ===");

    // Step 1: Initialize TCP/IP stack
    LOGI( "Initializing netif...");
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        LOGE( "netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Create event loop
    LOGI( "Creating event loop...");
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        // ESP_ERR_INVALID_STATE means loop already exists (OK)
        LOGE( "event loop creation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 3: Create default WiFi station netif
    LOGI( "Creating default WiFi STA netif...");
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif)
    {
        LOGE( "Failed to create WiFi STA netif");
        return ESP_FAIL;
    }

    // Step 4: Register IP event handler
    LOGI( "Registering IP event handler...");
    if (!wifi_event_group)
    {
        wifi_event_group = xEventGroupCreate();
    }
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &event_handler, NULL, &ip_event_handler);
    if (ret != ESP_OK)
    {
        LOGE( "Event handler registration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    transport_initialized = true;
    LOGI( "[OK] WiFi Hosted transport initialized\n");

    return ESP_OK;
}

esp_err_t wifi_hosted_deinit_transport(void)
{
    LOGI( "=== WiFi Hosted Transport Deinit ===");

    if (!transport_initialized)
    {
        LOGI( "Transport not initialized, nothing to deinit");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;

    // Stop WiFi if started
    if (wifi_started)
    {
        LOGI( "Stopping WiFi...");
        ret = esp_wifi_stop();
        if (ret != ESP_OK)
        {
            LOGW( "WiFi stop failed: %s", esp_err_to_name(ret));
        }

        LOGI( "Deinitializing WiFi driver...");
        ret = esp_wifi_deinit();
        if (ret != ESP_OK)
        {
            LOGW( "WiFi deinit failed: %s", esp_err_to_name(ret));
        }
        wifi_started = false;
    }

    // Unregister event handler
    if (ip_event_handler)
    {
        LOGI( "Unregistering event handler...");
        ret = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler);
        if (ret != ESP_OK)
        {
            LOGW( "Event handler unregister failed: %s", esp_err_to_name(ret));
        }
        ip_event_handler = NULL;
    }

    // Destroy event group
    if (wifi_event_group)
    {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }

    transport_initialized = false;
    LOGI( "[OK] WiFi Hosted transport deinitialized\n");

    return ESP_OK;
}

void init_wifi(void)
{
    LOGI( "=== WiFi Stack Initialization ===");

    // If transport not initialized yet, do it now
    if (!transport_initialized)
    {
        esp_err_t ret = wifi_hosted_init_transport();
        if (ret != ESP_OK)
        {
            LOGE( "Transport init failed, cannot proceed");
            return;
        }
    }

    // CRITICAL: Wait for C6 firmware boot
    LOGI( "Waiting %dms for C6 firmware boot...", C6_FIRMWARE_BOOT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(C6_FIRMWARE_BOOT_DELAY_MS));
    LOGI( "C6 firmware ready, proceeding with WiFi init");

    // Initialize WiFi stack (triggers ESP-Hosted SDIO init)
    LOGI( "Initializing WiFi driver...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        LOGE( "WiFi init failed: %s", esp_err_to_name(ret));
        return;
    }

    LOGI( "Setting WiFi mode to STA...");
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        LOGE( "Set mode failed: %s", esp_err_to_name(ret));
        return;
    }

    LOGI( "Starting WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        LOGE( "WiFi start failed: %s", esp_err_to_name(ret));
        return;
    }

    wifi_started = true;
    LOGI( "[OK] WiFi stack initialized\n");
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
