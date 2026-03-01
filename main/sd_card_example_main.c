/*
 * SD Card + ESP-Hosted Example with Bootstrap Manager
 * 
 * v1.2.0-arbiter: Two-phase initialization with SDMMC arbiter
 * - Phase A: Power management (GPIO isolation)
 * - Phase B: WiFi Hosted (SDIO transport via arbiter)
 * - SD Card: On-demand via sdmmc_arbiter_request_sd_card()
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

// Network includes (always needed for WiFi connection test types)
#include "esp_netif.h"
#include "esp_wifi.h"

#include "display_jd9165.h"
#include "touch_gt911.h"
#include "rtc_rx8025t.h"
#include "rtc_test.h"
#include "rtc_ntp_sync.h"
#include "es8311_audio.h"
#include "feature_flags.h"
#include "i2c_utils.h"
#include "esp_hosted_wifi.h"
#include "bootstrap_manager.h"  // Coordinated initialization with arbiter

#if ENABLE_WIFI && ENABLE_WIFI_CONNECT
// Load WiFi credentials from wifi_config.h (gitignored)
#include "wifi_config.h"
#endif

static const char *TAG = "GUITION_MAIN";

// I2C Bus (shared: ES8311 + GT911 + RTC)
#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_SCL_IO 8

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

#if ENABLE_DISPLAY && ENABLE_DISPLAY_TEST
void test_display_fill_color(uint16_t color)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Panel handle not initialized!");
        return;
    }

    const int width = 1024;
    const int height = 600;
    const int buffer_lines = 10;
    
    uint16_t *line_buffer = heap_caps_malloc(width * buffer_lines * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }

    for (int i = 0; i < width * buffer_lines; i++) {
        line_buffer[i] = color;
    }

    LOG_DISPLAY(TAG, "Filling display with color 0x%04X...", color);

    for (int y = 0; y < height; y += buffer_lines) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, width, y + buffer_lines, line_buffer);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    free(line_buffer);
    LOG_DISPLAY(TAG, "Display fill complete");
}

void test_display_rgb_pattern(void)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Panel handle not initialized!");
        return;
    }

    LOG_DISPLAY(TAG, "Drawing RGB pattern...");
    
    const uint16_t RED   = 0xF800;
    const uint16_t GREEN = 0x07E0;
    const uint16_t BLUE  = 0x001F;
    
    const int width = 1024;
    const int height = 600;
    const int stripe_width = width / 3;
    const int lines_per_batch = 10;
    
    uint16_t *line_buffer = heap_caps_malloc(width * lines_per_batch * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }

    for (int i = 0; i < lines_per_batch; i++) {
        for (int x = 0; x < width; x++) {
            int idx = i * width + x;
            if (x < stripe_width) {
                line_buffer[idx] = RED;
            } else if (x < stripe_width * 2) {
                line_buffer[idx] = GREEN;
            } else {
                line_buffer[idx] = BLUE;
            }
        }
    }

    for (int y = 0; y < height; y += lines_per_batch) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, width, y + lines_per_batch, line_buffer);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    free(line_buffer);
    LOG_DISPLAY(TAG, "RGB pattern complete");
}
#endif

#if ENABLE_TOUCH && ENABLE_TOUCH_TEST
void test_touch_read_loop(void)
{
    if (!touch_handle) {
        ESP_LOGE(TAG, "Touch handle not initialized!");
        return;
    }

    LOG_TOUCH(TAG, "Touch test started. Touch the screen...");
    LOG_TOUCH(TAG, "Press Ctrl+C to stop");

    esp_lcd_touch_point_data_t point_data[1];
    uint8_t touch_cnt = 0;

    while (1) {
        esp_lcd_touch_read_data(touch_handle);
        
        esp_err_t ret = esp_lcd_touch_get_data(touch_handle, point_data, &touch_cnt, 1);
        
        if (ret == ESP_OK && touch_cnt > 0) {
            LOG_TOUCH(TAG, "Touch detected: X=%d, Y=%d, Strength=%d", 
                     point_data[0].x, point_data[0].y, point_data[0].strength);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   Guition JC1060P470C Initialization");
    ESP_LOGI(TAG, "   v1.2.0-arbiter (SDMMC Arbiter)");
    ESP_LOGI(TAG, "========================================\n");

    // ========== 1. NVS ==========
#if ENABLE_NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    LOG_NVS(TAG, "✓ NVS initialized\n");
#else
    ESP_LOGI(TAG, "NVS disabled by feature flags\n");
#endif

    // ========== 2. I2C Bus (shared by all peripherals) ==========
#if ENABLE_I2C
    ESP_LOGI(TAG, "=== I2C Bus Initialization ===");
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    LOG_I2C(TAG, "✓ I2C bus ready (SDA=GPIO%d, SCL=GPIO%d)\n", 
            I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
#else
    ESP_LOGI(TAG, "I2C disabled by feature flags\n");
    i2c_master_bus_handle_t bus_handle = NULL;
#endif

    // ========== 3. Audio Codec (ES8311 - optional) ==========
#if ENABLE_AUDIO
    if (bus_handle) {
        LOG_AUDIO(TAG, "=== ES8311 Audio Codec ===");
        ret = es8311_init(bus_handle);
        if (ret == ESP_OK) {
            LOG_AUDIO(TAG, "✓ ES8311 initialized (powered down)\n");
        } else {
            ESP_LOGW(TAG, "ES8311 not responding (may not be populated)\n");
        }
    }
#else
    ESP_LOGI(TAG, "Audio codec disabled by feature flags\n");
#endif

    // ========== 4. RTC (RX8025T - direct init, no probe) ==========
#if ENABLE_RTC
    if (bus_handle) {
        ESP_LOGI(TAG, "=== RTC Initialization ===");
        LOG_RTC(TAG, "RTC driver will validate device at 0x32 (no pre-probe)");
        
#if ENABLE_RTC_HW_TEST
        rtc_hardware_test(bus_handle);
#else
        // Direct init - driver handles device detection via first read
        ret = rtc_rx8025t_init(bus_handle);
        if (ret == ESP_OK) {
            LOG_RTC(TAG, "✓ RTC initialized successfully\n");
            
#if ENABLE_RTC_TEST && !ENABLE_RTC_NTP_SYNC
            // Read and display current time (skip if NTP sync test will run)
            rtc_time_t current_time;
            if (rtc_rx8025t_get_time(&current_time) == ESP_OK) {
                LOG_RTC(TAG, "Current time: 20%02d-%02d-%02d %02d:%02d:%02d",
                        current_time.year, current_time.month, current_time.day,
                        current_time.hour, current_time.minute, current_time.second);
            }
            
            // Check power flags
            bool pon_flag, vlf_flag;
            if (rtc_rx8025t_check_power_on_flag(&pon_flag) == ESP_OK) {
                LOG_RTC(TAG, "PON Flag: %s", pon_flag ? "SET (power was lost)" : "CLEAR");
            }
            if (rtc_rx8025t_check_voltage_low_flag(&vlf_flag) == ESP_OK) {
                LOG_RTC(TAG, "VLF Flag: %s\n", vlf_flag ? "SET (voltage was low)" : "CLEAR");
            }
#endif
        } else {
            ESP_LOGW(TAG, "RTC not responding (may not be populated)\n");
        }
#endif
    }
#else
    ESP_LOGI(TAG, "RTC disabled by feature flags\n");
#endif

    // ========== 5. Display (MIPI DSI JD9165) ==========
#if ENABLE_DISPLAY
    ESP_LOGI(TAG, "=== Display Initialization ===");
    panel_handle = init_jd9165_display();
    if (panel_handle) {
        LOG_DISPLAY(TAG, "✓ Display ready (1024x600 MIPI DSI)\n");
    } else {
        ESP_LOGE(TAG, "✗ Display initialization failed!\n");
    }
#else
    ESP_LOGI(TAG, "Display disabled by feature flags\n");
#endif

    // ========== 6. Touch Controller (GT911 - direct init, no probe) ==========
#if ENABLE_TOUCH
    if (bus_handle) {
        ESP_LOGI(TAG, "=== Touch Controller Initialization ===");
        LOG_TOUCH(TAG, "GT911 driver will auto-reset and detect I2C address (0x14 or 0x5D)");
        
        touch_handle = init_touch_gt911(bus_handle);
        
        if (touch_handle) {
            LOG_TOUCH(TAG, "✓ Touch controller ready");
            
            // Verify which address GT911 responded at (after init)
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_err_t ret14 = i2c_master_probe(bus_handle, 0x14, 100);
            esp_err_t ret5d = i2c_master_probe(bus_handle, 0x5D, 100);
            
            if (ret14 == ESP_OK) {
                LOG_TOUCH(TAG, "GT911 active at 0x14 (INT=HIGH during reset)\n");
            } else if (ret5d == ESP_OK) {
                LOG_TOUCH(TAG, "GT911 active at 0x5D (INT=LOW during reset)\n");
            }
        } else {
            ESP_LOGE(TAG, "✗ Touch initialization failed!\n");
        }
    } else {
        ESP_LOGW(TAG, "Touch skipped (I2C not initialized)\n");
    }
#else
    ESP_LOGI(TAG, "Touch disabled by feature flags\n");
#endif

    // ========== 7. Bootstrap Manager (WiFi with SDMMC Arbiter) ==========
    // Two-phase coordinated initialization:
    //   Phase A: Power management (GPIO isolation, strapping)
    //   Phase B: WiFi Hosted (SDIO transport via arbiter)
    //   SD Card: Available on-demand via sdmmc_arbiter_request_sd_card()
    
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   Starting Bootstrap Manager");
    ESP_LOGI(TAG, "========================================\n");
    
    bootstrap_manager_t bootstrap_mgr = {0};
    
#if ENABLE_SD_CARD || ENABLE_WIFI
    // Initialize bootstrap manager (spawns two tasks + arbiter)
    ret = bootstrap_manager_init(&bootstrap_mgr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap manager init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Cannot continue without SD/WiFi - restarting in 5s...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    // Wait for both phases to complete (30 second timeout)
    ret = bootstrap_manager_wait(&bootstrap_mgr, 30000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "\n");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "   Bootstrap Complete - System Ready");
        ESP_LOGI(TAG, "========================================\n");
        
        // NOTE: SD card test temporarily disabled to isolate WiFi testing
        // TODO: Re-enable after WiFi validation
        /*
        // Get SD card handle from bootstrap manager (switches to SD mode)
        sdmmc_card_t *card = bootstrap_manager_get_sd_card(&bootstrap_mgr);
        if (card) {
            ESP_LOGI(TAG, "SD Card: %s, Capacity: %llu MB",
                    card->cid.name,
                    ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
        }
        */
        
#if ENABLE_WIFI_CONNECT
        // WiFi connection test (arbiter keeps WiFi mode active)
        ESP_LOGI(TAG, "\n=== WiFi Connection Test ===");
        ESP_LOGI(TAG, "Connecting to: %s", WIFI_SSID);
        
        wifi_connect(WIFI_SSID, WIFI_PASSWORD);
        ESP_LOGI(TAG, "Waiting for IP address (15s timeout)...");
        
        wait_for_ip();
        
        if (check_if_already_has_ip()) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip;
            
            if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                ESP_LOGI(TAG, "✓ WiFi connected!");
                ESP_LOGI(TAG, "   IP Address: " IPSTR, IP2STR(&ip.ip));
                ESP_LOGI(TAG, "   Netmask:    " IPSTR, IP2STR(&ip.netmask));
                ESP_LOGI(TAG, "   Gateway:    " IPSTR "\n", IP2STR(&ip.gw));
                
                // Get signal strength (RSSI)
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    ESP_LOGI(TAG, "   RSSI: %d dBm\n", ap_info.rssi);
                }
                
#if ENABLE_RTC && ENABLE_RTC_NTP_SYNC
                // RTC NTP Sync Test (requires WiFi connection)
                rtc_ntp_sync_test();
#endif
            }
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout\n");
        }
#elif ENABLE_WIFI
        // Simple scan test (default)
        ESP_LOGI(TAG, "\n=== WiFi Scan Test ===");
        if (do_wifi_scan_and_check(NULL)) {
            ESP_LOGI(TAG, "✓ WiFi scan successful\n");
        } else {
            ESP_LOGW(TAG, "WiFi scan returned no networks\n");
        }
#endif
        
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Bootstrap timeout! Check hardware connections.");
        ESP_LOGE(TAG, "Restarting in 10 seconds...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Bootstrap failed! Error: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Restarting in 10 seconds...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }
#else
    ESP_LOGI(TAG, "Bootstrap manager skipped (SD and WiFi disabled)\n");
#endif

    // ========== System Ready ==========
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   System Initialization Complete");
    ESP_LOGI(TAG, "========================================\n");

    // ========== Display Tests ==========
#if ENABLE_DISPLAY && ENABLE_DISPLAY_TEST
    if (panel_handle) {
        ESP_LOGI(TAG, "=== Display Test Sequence ===");
        
        ESP_LOGI(TAG, "Test 1/4: RED fill");
        test_display_fill_color(0xF800);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Test 2/4: GREEN fill");
        test_display_fill_color(0x07E0);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Test 3/4: BLUE fill");
        test_display_fill_color(0x001F);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Test 4/4: RGB stripes");
        test_display_rgb_pattern();
        ESP_LOGI(TAG, "Display tests complete\n");
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif

    // ========== Touch Test ==========
#if ENABLE_TOUCH && ENABLE_TOUCH_TEST
    if (touch_handle) {
        ESP_LOGI(TAG, "=== Touch Test (continuous) ===");
        test_touch_read_loop();
    }
#endif

    // Main loop
    ESP_LOGI(TAG, "Entering main loop...\n");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
