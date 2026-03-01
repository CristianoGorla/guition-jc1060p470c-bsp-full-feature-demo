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

#include "display_jd9165.h"
#include "touch_gt911.h"

static const char *TAG = "GUITION_MAIN";

#define I2C_MASTER_SDA_IO    7
#define I2C_MASTER_SCL_IO    8
#define SD_CARD_POWER_GPIO   GPIO_NUM_36  // MOSFET Q1 controlla TF_VCC

// Dummy functions per workaround ESP-Hosted + SD Card Slot 0
#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
static esp_err_t sdmmc_host_init_dummy(void) { return ESP_OK; }
static esp_err_t sdmmc_host_deinit_dummy(void) { return ESP_OK; }
#endif

static void sd_card_power_on(void)
{
    // Attiva alimentazione SD Card via MOSFET P-Channel Q1
    // GPIO36: LOW = MOSFET ON (alimentazione attiva)
    //         HIGH = MOSFET OFF (alimentazione spenta)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SD_CARD_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    gpio_set_level(SD_CARD_POWER_GPIO, 0);  // LOW = Power ON
    vTaskDelay(pdMS_TO_TICKS(100));  // Stabilizzazione alimentazione
    ESP_LOGI(TAG, "SD Card power enabled via GPIO36");
}

void app_main(void)
{
    esp_err_t ret;

    // 1. Inizializzazione NVS (Necessaria per Wi-Fi e calibrazioni)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // 2. Inizializzazione Bus I2C (Pin 7 e 8 per Touch/Codec/RTC)
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
    ESP_LOGI(TAG, "I2C bus initialized");

    // 3. Inizializzazione Display JD9165 (MIPI DSI)
    ESP_LOGI(TAG, "Initializing display...");
    init_jd9165_display();
    ESP_LOGI(TAG, "Display ready");

    // 4. Inizializzazione Touch GT911
    ESP_LOGI(TAG, "Initializing touch...");
    init_touch_gt911(bus_handle);
    ESP_LOGI(TAG, "Touch ready");

    // 5. Attiva alimentazione SD Card
    sd_card_power_on();
    vTaskDelay(pdMS_TO_TICKS(50));  // Attesa stabilizzazione card

    // 6. Montaggio SD Card (Slot 0)
    ESP_LOGI(TAG, "Initializing SD card (Slot 0)...");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    sdmmc_card_t *card;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // WORKAROUND per IDF 5.5.3: ESP-Hosted (Slot 1) è già attivo, non resettare host
#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    host.init = &sdmmc_host_init_dummy;
    host.deinit = &sdmmc_host_deinit_dummy;
    ESP_LOGI(TAG, "Using dummy SDMMC host init/deinit for ESP-Hosted workaround");
#endif

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4; // 4-line mode
    // Pin Slot 0: CLK=43, CMD=44, D0=39, D1=40, D2=41, D3=42 (hardware fissi)

    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SD card (0x%x)", ret);
    }
    else
    {
        ESP_LOGI(TAG, "SD card mounted successfully");
        ESP_LOGI(TAG, "Card name: %s", card->cid.name);
        ESP_LOGI(TAG, "Capacity: %llu MB", 
                 ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
    }

    ESP_LOGI(TAG, "=== System ready ===");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
