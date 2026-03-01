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

// Header del progetto
#include "sd_card_functions.h"
#include "esp_hosted_wifi.h"
#include "display_jd9165.h"
#include "touch_gt911.h"

static const char *TAG = "GUITION_MAIN";

// Pinout Guition JC1060 [2, 3, 5]
#define C6_CHIP_PU_GPIO 54
#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_SCL_IO 8

void app_main(void)
{
    esp_err_t ret;

    // 1. Reset Hardware ESP32-C6 (Sblocca il bus SDIO per Hosted) [2, 6]
    ESP_LOGI(TAG, "Resetting ESP32-C6 co-processor...");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << C6_CHIP_PU_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(C6_CHIP_PU_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(C6_CHIP_PU_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. Inizializzazione NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 3. Inizializzazione Bus I2C (Per Touch e RTC) [3, 7]
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

    // 4. Inizializzazione Display JD9165 (MIPI DSI) [8, 9]
    ESP_LOGI(TAG, "Initializing JD9165 Display...");
    init_jd9165_display();

    // 5. Inizializzazione Touch GT911 [3]
    ESP_LOGI(TAG, "Initializing GT911 Touch...");
    init_touch_gt911(bus_handle);

    // 6. Inizializzazione Wi-Fi Hosted (ESP-Hosted su Slot 1) [1, 10]
    ESP_LOGI(TAG, "Initializing Wi-Fi via ESP-Hosted...");
    init_wifi();

    // Attesa ottenimento IP e sincronizzazione orario (LwIP SNTP)
    wait_for_ip();
    custom_sntp_sync();

    // 7. Inizializzazione SD Card (Slot 0) con Workaround IDF 5.5 [1, 4]
    ESP_LOGI(TAG, "Mounting SD Card...");
    ret = mount_sd_card();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SD Card. Check connections.");
    }
    else
    {
        ESP_LOGI(TAG, "SD Card mounted successfully.");

        // Esempio test file system
        const char *file_hello = "/sdcard/hello.txt";
        FILE *f = fopen(file_hello, "w");
        if (f)
        {
            fprintf(f, "Guition JC1060 System Ready!\n");
            fclose(f);
            ESP_LOGI(TAG, "File written to SD Card.");
        }
    }

    ESP_LOGI(TAG, "System initialization complete. Entering main loop.");

    while (1)
    {
        // Il tuo codice applicativo o task LVGL qui
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
