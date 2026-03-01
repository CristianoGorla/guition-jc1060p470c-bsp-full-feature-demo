#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "driver/gpio.h" // FIX: Necessario per gpio_set_direction e gpio_set_level
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_card_functions.h"
#include "esp_hosted_wifi.h"
#include "display_jd9165.h"
#include "wifi_config.h"
#include "touch_gt911.h"
#include "driver/i2c_types.h"

// static const char *TAG = "GUITION_MAIN";

static i2c_master_bus_handle_t i2c_bus_handle;

void custom_sntp_sync(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 15)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    static const char *TAG = "GUITION_MAIN";

    // NON fare reset C6 qui - lascia che lo faccia il driver LCD

    // 1. I2C
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = 8,
        .sda_io_num = 7,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus_handle));
    ESP_LOGI(TAG, "I2C initialized");

    // 2. Display
    init_jd9165_display();
    ESP_LOGI(TAG, "Display initialized");

    // 3. Touch
    init_touch_gt911(i2c_bus_handle);
    ESP_LOGI(TAG, "Touch initialized");

    // 4. WiFi
    init_wifi();
    wifi_connect(WIFI_SSID, WIFI_PASSWORD);
    wait_for_ip();
    custom_sntp_sync();

    ESP_LOGI(TAG, "Guition JC1060 pronta.");
    while (1)
        vTaskDelay(pdMS_TO_TICKS(10000));
}
