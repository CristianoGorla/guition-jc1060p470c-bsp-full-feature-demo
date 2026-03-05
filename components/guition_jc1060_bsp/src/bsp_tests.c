/**
 * @file bsp_tests.c
 * @brief Hardware testing service implementation
 */

#include "bsp_tests.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "bsp_board.h"

#if defined(CONFIG_BSP_ENABLE_RTC)
#include "rtc_test.h"
#endif

#if defined(CONFIG_BSP_ENABLE_SDCARD)
#include "sdmmc_cmd.h"
#include "sd_card_manager.h"
#endif

#if defined(CONFIG_BSP_ENABLE_WIFI)
#include "esp_hosted_wifi.h"
#endif

static const char *TAG = "BSP_TESTS";

#ifdef CONFIG_BSP_ENABLE_DEBUG_MODE

static esp_err_t test_rtc(void)
{
#if defined(CONFIG_APP_ENABLE_RTC_TEST) && defined(CONFIG_BSP_ENABLE_RTC)
    ESP_LOGI(TAG, "=== RTC Test ===");

    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_bus_handle();
    if (!i2c_bus) {
        ESP_LOGW(TAG, "I2C bus not available");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = rtc_test_read_only(i2c_bus);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OK] RTC test complete\\n");
    } else {
        ESP_LOGW(TAG, "[FAIL] RTC test failed\\n");
    }

    return ret;
#else
    return ESP_OK;
#endif
}

static esp_err_t test_sdcard(void)
{
#if defined(CONFIG_APP_ENABLE_SD_TEST) && defined(CONFIG_BSP_ENABLE_SDCARD)
    ESP_LOGI(TAG, "=== SD Card Test ===");

    sdmmc_card_t *card = sd_card_get_handle();
    if (card) {
        uint64_t capacity_mb = ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024);
        ESP_LOGI(TAG, "SD Card Info:");
        ESP_LOGI(TAG, "   Name: %s", card->cid.name);
        ESP_LOGI(TAG, "   Size: %llu MB", (unsigned long long)capacity_mb);
        ESP_LOGI(TAG, "   Speed: %s", (card->csd.tr_speed > 25000000) ? "High Speed" : "Default Speed");
        ESP_LOGI(TAG, "   Sector size: %d bytes", card->csd.sector_size);
        ESP_LOGI(TAG, "   Capacity: %d sectors", card->csd.capacity);
        ESP_LOGI(TAG, "[OK] SD card test complete\\n");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "SD card not available");
    ESP_LOGW(TAG, "[SKIP] SD card test\\n");
    return ESP_ERR_INVALID_STATE;
#else
    return ESP_OK;
#endif
}

static esp_err_t test_wifi(void)
{
#if defined(CONFIG_APP_ENABLE_WIFI_SCAN_TEST) && defined(CONFIG_BSP_ENABLE_WIFI)
    ESP_LOGI(TAG, "=== WiFi Scan Test ===");

    bool ok = do_wifi_scan_and_check(NULL);
    esp_err_t ret = ok ? ESP_OK : ESP_FAIL;

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OK] WiFi scan complete\\n");
    } else {
        ESP_LOGW(TAG, "[FAIL] WiFi scan failed\\n");
    }

    return ret;
#else
    return ESP_OK;
#endif
}

esp_err_t bsp_run_hardware_tests(void)
{
    ESP_LOGI(TAG, "\\n========================================");
    ESP_LOGI(TAG, "   RUNNING HARDWARE TESTS");
    ESP_LOGI(TAG, "========================================\\n");

    esp_err_t ret_rtc = test_rtc();
    esp_err_t ret_sd = test_sdcard();
    esp_err_t ret_wifi = test_wifi();

    ESP_LOGI(TAG, "\\n========================================");
    ESP_LOGI(TAG, "   ALL HARDWARE TESTS COMPLETE");
    ESP_LOGI(TAG, "========================================\\n");

    if (ret_rtc == ESP_OK && ret_sd == ESP_OK && ret_wifi == ESP_OK) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

#else

esp_err_t bsp_run_hardware_tests(void)
{
    ESP_LOGI(TAG, "\\n=== Hardware Tests Skipped (Debug Mode Disabled) ===\\n");
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
