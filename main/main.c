/*
 * Guition JC1060P470C BSP - Full Feature Demo
 * Copyright (c) 2026 Cristiano Gorla | SPDX-License-Identifier: Unlicense
 */

#include <string.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "build_info.h"
#include "bsp_board.h"
#include "bsp_priv.h"
#include "bsp_sensors.h"
#ifdef CONFIG_BSP_ENABLE_DEBUG_MODE
#include "bsp_tests.h"
#endif
#ifdef CONFIG_BSP_ENABLE_HEARTBEAT
#include "bsp_heartbeat.h"
#endif
#include "rtc_ntp_sync.h"
#include "bootstrap_manager.h"
#include "backlight_test.h"

#ifdef CONFIG_BSP_ENABLE_WIFI
#include "esp_hosted_wifi.h"
#include "esp_netif.h"
#ifdef CONFIG_APP_ENABLE_WIFI_CONNECT
#include "wifi_config.h"
#endif
#endif

#ifdef CONFIG_BSP_ENABLE_LVGL
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "lvgl_init.h"
#include "lvgl_demo.h"
#include "lvgl_dashboard.h"
#endif

static const char *TAG = "MAIN";
#if CONFIG_BSP_SENSORS_LOG_UPTIME_TEMP
static const char *SYSTEM_TAG = "SYSTEM";
#endif
static const char *MAIN_BANNER_LINE = "====================================================================================================";
#define MAIN_BANNER_WIDTH 78

static void main_log_banner_border(int top)
{
    if (top) {
        ESP_LOGI(TAG, "┌%.*s┐", MAIN_BANNER_WIDTH, MAIN_BANNER_LINE);
    } else {
        ESP_LOGI(TAG, "└%.*s┘", MAIN_BANNER_WIDTH, MAIN_BANNER_LINE);
    }
}

static void main_log_banner_row(const char *text)
{
    size_t len = strlen(text);
    int left_pad = 0;
    int right_pad = 0;

    if (len > MAIN_BANNER_WIDTH) {
        len = MAIN_BANNER_WIDTH;
    }

    left_pad = (int)((MAIN_BANNER_WIDTH - len) / 2);
    right_pad = (int)(MAIN_BANNER_WIDTH - len - left_pad);

    ESP_LOGI(TAG, "│%*s%.*s%*s│", left_pad, "", (int)len, text, right_pad, "");
}

static void main_log_banner_row_lcr(const char *left, const char *center, const char *right)
{
    char row[MAIN_BANNER_WIDTH + 1];
    size_t left_len = strlen(left);
    size_t center_len = strlen(center);
    size_t right_len = strlen(right);
    size_t center_start = 0;
    size_t right_start = 0;

    memset(row, ' ', sizeof(row));
    row[MAIN_BANNER_WIDTH] = '\0';

    if (left_len > MAIN_BANNER_WIDTH) {
        left_len = MAIN_BANNER_WIDTH;
    }
    if (center_len > MAIN_BANNER_WIDTH) {
        center_len = MAIN_BANNER_WIDTH;
    }
    if (right_len > MAIN_BANNER_WIDTH) {
        right_len = MAIN_BANNER_WIDTH;
    }

    memcpy(row, left, left_len);

    center_start = (MAIN_BANNER_WIDTH - center_len) / 2;
    memcpy(row + center_start, center, center_len);

    right_start = MAIN_BANNER_WIDTH - right_len;
    memcpy(row + right_start, right, right_len);

    ESP_LOGI(TAG, "│%s│", row);
}

static void main_log_centered_plain(const char *text)
{
    size_t len = strlen(text);
    int left_pad = 0;

    if (len > MAIN_BANNER_WIDTH) {
        len = MAIN_BANNER_WIDTH;
    }

    left_pad = (int)((MAIN_BANNER_WIDTH - len) / 2);
    ESP_LOGI(TAG, "%*s%.*s", left_pad, "", (int)len, text);
}

#ifdef CONFIG_BSP_ENABLE_LVGL
static void on_debug_tool_selected(debug_tool_t tool, void *user_data)
{
    (void)user_data;

    ESP_LOGI(TAG, "Debug tool selected: %d", (int)tool);

    switch (tool) {
        case DEBUG_TOOL_LOG_MONITOR:
            ESP_LOGI(TAG, "Launch log monitor (TODO)");
            break;
        case DEBUG_TOOL_CAMERA_TEST:
            ESP_LOGI(TAG, "Camera test not implemented");
            break;
        case DEBUG_TOOL_SENSOR_MONITOR:
            ESP_LOGI(TAG, "Sensor monitor not implemented");
            break;
        case DEBUG_TOOL_WIFI_SCANNER:
            ESP_LOGI(TAG, "WiFi scanner not implemented");
            break;
        case DEBUG_TOOL_SD_BROWSER:
            ESP_LOGI(TAG, "SD browser not implemented");
            break;
        case DEBUG_TOOL_I2C_SCANNER:
            ESP_LOGI(TAG, "I2C scanner not implemented");
            break;
        case DEBUG_TOOL_SYSTEM_INFO:
            ESP_LOGI(TAG, "System info not implemented");
            break;
        case DEBUG_TOOL_GPIO_MONITOR:
            ESP_LOGI(TAG, "Radar monitor not implemented");
            break;
        case DEBUG_TOOL_PERFORMANCE:
            ESP_LOGI(TAG, "Performance view not implemented");
            break;
        default:
            ESP_LOGW(TAG, "Unknown debug tool: %d", (int)tool);
            break;
    }
}
#endif

#if CONFIG_BSP_SENSORS_LOG_UPTIME_TEMP
static void uptime_temp_log_task(void *arg)
{
    (void)arg;

    while (1) {
        int64_t uptime_sec = esp_timer_get_time() / 1000000LL;
        bsp_sensor_data_t sensor_data = {0};
        char temp_buf[24];
        char hum_buf[24];
        char press_buf[24];

        (void)bsp_sensor_get_data(&sensor_data);

        if (sensor_data.has_temperature) {
            snprintf(temp_buf, sizeof(temp_buf), "%.2f °C", (double)sensor_data.temperature_c);
        } else {
            snprintf(temp_buf, sizeof(temp_buf), "n/a");
        }

        if (sensor_data.has_humidity) {
            snprintf(hum_buf, sizeof(hum_buf), "%.2f %%", (double)sensor_data.humidity_pct);
        } else {
            snprintf(hum_buf, sizeof(hum_buf), "n/a");
        }

        if (sensor_data.has_pressure) {
            snprintf(press_buf, sizeof(press_buf), "%.2f hPa", (double)sensor_data.pressure_hpa);
        } else {
            snprintf(press_buf, sizeof(press_buf), "n/a");
        }

        ESP_LOGI(SYSTEM_TAG,
                 "Uptime: %llds | Temp: %s | Hum: %s | Press: %s",
                 (long long)uptime_sec,
                 temp_buf,
                 hum_buf,
                 press_buf);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif

void app_main(void)
{
    esp_err_t ret;
    char version_line[40];
    char center_line[24];
    char build_line[64];

    snprintf(version_line, sizeof(version_line), "Version: v1.3.0-dev");
    snprintf(center_line, sizeof(center_line), "             ");
    snprintf(build_line, sizeof(build_line), "Build: %s", BUILD_GIT_COMMIT);

    main_log_banner_border(1);
    main_log_banner_row("____    _            ___    ___");
    main_log_banner_row("| __ )  (_)   ___    ( _ )  / _ \\");
    main_log_banner_row("|  _ \\  | |  / _ \\   / _ \\ | | | |");
    main_log_banner_row("| |_) | | | | |_| | | (_) || |_| |");
    main_log_banner_row("|____/  |_|  \\___/   \\___/  \\___/");
    main_log_banner_row("");
    main_log_banner_row("https://github.com/CristianoGorla/");
    main_log_banner_row("guition-jc1060p470c-bsp-full-feature-demo");
    main_log_banner_row("");
    main_log_banner_row("Guition JC1060P470C Firmware");
    main_log_banner_row("");
    main_log_banner_row_lcr(version_line, center_line, build_line);
    main_log_banner_border(0);

    ESP_LOGI(TAG, "");
    main_log_centered_plain(BUILD_TIMESTAMP);
    ESP_LOGI(TAG, "");

    /* Step 1: BSP - Hardware Only (Display/Touch/Audio/RTC drivers) */
    ret = bsp_board_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Step 2: NVS Init */
#ifdef CONFIG_APP_ENABLE_NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
#endif

    /* Step 3: Bootstrap - WiFi + SD (PSRAM allocations happen here) */    
#if defined(CONFIG_BSP_ENABLE_SDCARD) || defined(CONFIG_BSP_ENABLE_WIFI)
        bootstrap_manager_t bootstrap_mgr = {0};
    ret = bootstrap_manager_init(&bootstrap_mgr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap init failed");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    ret = bootstrap_manager_wait(&bootstrap_mgr, 30000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap timeout/failed");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }
    
#endif

#if defined(CONFIG_BSP_ENABLE_WIFI) && defined(CONFIG_APP_ENABLE_WIFI_CONNECT)
    if (check_if_already_has_ip()) {
        ESP_LOGI(TAG, "WiFi already connected (IP assigned)");
    } else {
        ESP_LOGI(TAG, "Connecting to: %s", WIFI_SSID);
        wifi_connect(WIFI_SSID, WIFI_PASSWORD);
        ESP_LOGI(TAG, "Waiting for IP address (15s timeout)...");
        wait_for_ip();

        if (check_if_already_has_ip()) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info;

            if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                ESP_LOGI(TAG, "WiFi connected");
                ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info.ip));
                ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&ip_info.gw));
                ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
            }
        } else {
            ESP_LOGW(TAG, "WiFi connect timeout");
        }
    }
#endif

    /* Step 4: LVGL Init - AFTER Bootstrap (safe PSRAM allocation) */
#ifdef CONFIG_BSP_ENABLE_LVGL
    ret = lvgl_port_init_custom();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL init failed: %s", esp_err_to_name(ret));
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
#endif

    /* Step 5: LVGL UI Creation */
#ifdef CONFIG_BSP_ENABLE_LVGL
    dashboard_config_t dashboard_cfg = DASHBOARD_CONFIG_DEFAULT();
    dashboard_cfg.tool_callback = on_debug_tool_selected;

    ret = lvgl_dashboard_init(&dashboard_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "UI ready: dashboard loaded");
    } else {
        ESP_LOGW(TAG, "Dashboard init failed (%s), fallback to simple demo", esp_err_to_name(ret));
        lvgl_demo_simple();
        ESP_LOGI(TAG, "UI ready: simple demo loaded");
    }
#endif

#ifdef CONFIG_BSP_ENABLE_DEBUG_MODE
    /* Run optional hardware tests (configured via menuconfig) */
    bsp_run_hardware_tests();
#endif
    
    ESP_LOGI(TAG, "=== System Ready ===\n");

#ifdef CONFIG_BSP_ENABLE_HEARTBEAT
    /* Start BSP heartbeat monitoring (if enabled in menuconfig) */
    ret = bsp_heartbeat_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start heartbeat: %s", esp_err_to_name(ret));
    }
#endif

#if CONFIG_BSP_SENSORS_LOG_UPTIME_TEMP
    BaseType_t task_ret = xTaskCreate(
        uptime_temp_log_task,
        "uptime_temp_log",
        3072,
        NULL,
        1,
        NULL
    );
    if (task_ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to start uptime temperature log task");
    }
#endif

    /* Main loop now just sleeps - heartbeat runs in background task */
    ESP_LOGI(TAG, "Entering idle loop...\n");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
