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

#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_SCL_IO 8

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

    // ============================================================
    // 2. SD CARD INIT - PRIMA DI TUTTO (prima che ESP-Hosted usi il controller)
    // ============================================================
    ESP_LOGI(TAG, "Initializing SD card (Slot %d)...", CONFIG_EXAMPLE_SDMMC_SLOT);

    // Attiva alimentazione SD Card via GPIO (dal Kconfig)
#ifdef CONFIG_EXAMPLE_PIN_CARD_POWER_RESET
    gpio_config_t pwr_io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_EXAMPLE_PIN_CARD_POWER_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_io_conf);
    gpio_set_level(CONFIG_EXAMPLE_PIN_CARD_POWER_RESET, 0); // Assumo LOW = Power ON
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "SD Card power enabled via GPIO%d", CONFIG_EXAMPLE_PIN_CARD_POWER_RESET);
#endif

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = CONFIG_EXAMPLE_SDMMC_SLOT;

    // Configurazione slot con pin dal Kconfig
    sdmmc_slot_config_t slot_config = {
        .clk = CONFIG_EXAMPLE_PIN_CLK,
        .cmd = CONFIG_EXAMPLE_PIN_CMD,
        .d0 = CONFIG_EXAMPLE_PIN_D0,
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
        .d1 = CONFIG_EXAMPLE_PIN_D1,
        .d2 = CONFIG_EXAMPLE_PIN_D2,
        .d3 = CONFIG_EXAMPLE_PIN_D3,
#endif
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };

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

    // ============================================================
    // 3. Resto delle periferiche (Display, Touch, I2C)
    // ============================================================

    // I2C
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

    // Display
    ESP_LOGI(TAG, "Initializing display...");
    init_jd9165_display();
    ESP_LOGI(TAG, "Display ready");

    // Touch
    ESP_LOGI(TAG, "Initializing touch...");
    init_touch_gt911(bus_handle);
    ESP_LOGI(TAG, "Touch ready");

    ESP_LOGI(TAG, "=== System ready ===");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
