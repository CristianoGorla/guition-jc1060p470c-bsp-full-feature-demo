/*
 * SD Card + ESP-Hosted Example with Bootstrap Manager
 * 
 * v1.1.0-restored: Three-phase initialization (v1.0.0-beta sequence)
 * - Phase A: Power management (GPIO isolation)
 * - Phase B: WiFi Hosted (SDIO transport)
 * - Phase C: SD card (automatic mount after WiFi)
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
#include "build_info.h"

// Network includes
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
#include "bootstrap_manager.h"

#if ENABLE_WIFI && ENABLE_WIFI_CONNECT
#include "wifi_config.h"
#endif

static const char *TAG = "GUITION_MAIN";

// I2C Bus
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
    ESP_LOGI(TAG, "   v1.1.0-restored (Three-Phase)");
    ESP_LOGI(TAG, "   Build: %s", BUILD_GIT_COMMIT);
    ESP_LOGI(TAG, "   Date: %s", BUILD_TIMESTAMP);
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

    // ========== 2. I2C Bus ==========
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

    // ========== 3. Audio Codec ==========
#if ENABLE_AUDIO
    if (bus_handle) {
        LOG_AUDIO(TAG, "=== ES8311 Audio Codec ===");
        ret = es8311_init(bus_handle);
        if (ret == ESP_OK) {
            LOG_AUDIO(TAG, "✓ ES8311 initialized (powered down)\n");
        } else {
            ESP_LOGW(TAG, "ES8311 not responding\n");
        }
    }
#else
    ESP_LOGI(TAG, "Audio codec disabled\n");
#endif

    // ========== 4. RTC ==========
#if ENABLE_RTC
    if (bus_handle) {
        ESP_LOGI(TAG, "=== RTC Initialization ===");
        LOG_RTC(TAG, "RTC driver will validate device at 0x32");
        
#if ENABLE_RTC_HW_TEST
        rtc_hardware_test(bus_handle);
#else
        ret = rtc_rx8025t_init(bus_handle);
        if (ret == ESP_OK) {
            LOG_RTC(TAG, "✓ RTC initialized\n");
            
#if ENABLE_RTC_TEST && !ENABLE_RTC_NTP_SYNC
            rtc_time_t current_time;
            if (rtc_rx8025t_get_time(&current_time) == ESP_OK) {
                LOG_RTC(TAG, "Current time: 20%02d-%02d-%02d %02d:%02d:%02d",
                        current_time.year, current_time.month, current_time.day,
                        current_time.hour, current_time.minute, current_time.second);
            }
            
            bool pon_flag, vlf_flag;
            if (rtc_rx8025t_check_power_on_flag(&pon_flag) == ESP_OK) {
                LOG_RTC(TAG, "PON Flag: %s", pon_flag ? "SET" : "CLEAR");
            }
            if (rtc_rx8025t_check_voltage_low_flag(&vlf_flag) == ESP_OK) {
                LOG_RTC(TAG, "VLF Flag: %s\n", vlf_flag ? "SET" : "CLEAR");
            }
#endif
        } else {
            ESP_LOGW(TAG, "RTC not responding\n");
        }
#endif
    }
#else
    ESP_LOGI(TAG, "RTC disabled\n");
#endif

    // ========== 5. Display ==========
#if ENABLE_DISPLAY
    ESP_LOGI(TAG, "=== Display Initialization ===");
    panel_handle = init_jd9165_display();
    if (panel_handle) {
        LOG_DISPLAY(TAG, "✓ Display ready (1024x600)\n");
    } else {
        ESP_LOGE(TAG, "✗ Display failed!\n");
    }
#else
    ESP_LOGI(TAG, "Display disabled\n");
#endif

    // ========== 6. Touch ==========
#if ENABLE_TOUCH
    if (bus_handle) {
        ESP_LOGI(TAG, "=== Touch Controller ===");
        LOG_TOUCH(TAG, "GT911 will auto-detect I2C address");
        
        touch_handle = init_touch_gt911(bus_handle);
        
        if (touch_handle) {
            LOG_TOUCH(TAG, "✓ Touch ready");
            
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_err_t ret14 = i2c_master_probe(bus_handle, 0x14, 100);
            esp_err_t ret5d = i2c_master_probe(bus_handle, 0x5D, 100);
            
            if (ret14 == ESP_OK) {
                LOG_TOUCH(TAG, "GT911 at 0x14\n");
            } else if (ret5d == ESP_OK) {
                LOG_TOUCH(TAG, "GT911 at 0x5D\n");
            }
        } else {
            ESP_LOGE(TAG, "✗ Touch failed!\n");
        }
    }
#else
    ESP_LOGI(TAG, "Touch disabled\n");
#endif

    // ========== 7. Bootstrap Manager ==========
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   Starting Bootstrap Manager");
    ESP_LOGI(TAG, "========================================\n");
    
    bootstrap_manager_t bootstrap_mgr = {0};
    
#if ENABLE_SD_CARD || ENABLE_WIFI
    ret = bootstrap_manager_init(&bootstrap_mgr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap init failed: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    ret = bootstrap_manager_wait(&bootstrap_mgr, 30000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "\n========================================");
        ESP_LOGI(TAG, "   Bootstrap Complete - System Ready");
        ESP_LOGI(TAG, "========================================\n");
        
        // Get SD card info
        sdmmc_card_t *card = bootstrap_manager_get_sd_card(&bootstrap_mgr);
        if (card) {
            ESP_LOGI(TAG, "SD Card: %s, Capacity: %llu MB",
                    card->cid.name,
                    ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
        }
        
#if ENABLE_WIFI_CONNECT
        // WiFi connection test
        ESP_LOGI(TAG, "\n=== WiFi Connection Test ===");
        ESP_LOGI(TAG, "Connecting to: %s", WIFI_SSID);
        
        wifi_connect(WIFI_SSID, WIFI_PASSWORD);
        ESP_LOGI(TAG, "Waiting for IP (15s)...");
        
        wait_for_ip();
        
        if (check_if_already_has_ip()) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip;
            
            if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                ESP_LOGI(TAG, "✓ WiFi connected!");
                ESP_LOGI(TAG, "   IP: " IPSTR, IP2STR(&ip.ip));
                ESP_LOGI(TAG, "   Netmask: " IPSTR, IP2STR(&ip.netmask));
                ESP_LOGI(TAG, "   Gateway: " IPSTR "\n", IP2STR(&ip.gw));
                
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    ESP_LOGI(TAG, "   RSSI: %d dBm\n", ap_info.rssi);
                }
                
#if ENABLE_RTC && ENABLE_RTC_NTP_SYNC
                rtc_ntp_sync_test();
#endif
            }
        } else {
            ESP_LOGW(TAG, "WiFi timeout\n");
        }
#elif ENABLE_WIFI
        // Simple scan
        ESP_LOGI(TAG, "\n=== WiFi Scan Test ===");
        if (do_wifi_scan_and_check(NULL)) {
            ESP_LOGI(TAG, "✓ WiFi scan successful\n");
        } else {
            ESP_LOGW(TAG, "No networks\n");
        }
#endif
        
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Bootstrap timeout!");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Bootstrap failed: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }
#else
    ESP_LOGI(TAG, "Bootstrap skipped\n");
#endif

    // ========== System Ready ==========
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   System Ready");
    ESP_LOGI(TAG, "========================================\n");

    // ========== Display Tests ==========
#if ENABLE_DISPLAY && ENABLE_DISPLAY_TEST
    if (panel_handle) {
        ESP_LOGI(TAG, "=== Display Test Sequence ===");
        
        ESP_LOGI(TAG, "Test 1/4: RED");
        test_display_fill_color(0xF800);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Test 2/4: GREEN");
        test_display_fill_color(0x07E0);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Test 3/4: BLUE");
        test_display_fill_color(0x001F);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Test 4/4: RGB stripes");
        test_display_rgb_pattern();
        ESP_LOGI(TAG, "Tests complete\n");
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif

    // ========== Touch Test ==========
#if ENABLE_TOUCH && ENABLE_TOUCH_TEST
    if (touch_handle) {
        ESP_LOGI(TAG, "=== Touch Test ===");
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
