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

// Header del tuo progetto (Assicurati che esistano)
#include "display_jd9165.h"
#include "touch_gt911.h"

static const char *TAG = "GUITION_MAIN";

// Pinout Hardware Guition JC1060 [2, 3]
#define C6_CHIP_PU_GPIO 54
#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_SCL_IO 8

void app_main(void)
{
    esp_err_t ret;

    // 1. Reset Hardware ESP32-C6 (Obbligatorio per sbloccare il bus SDIO) [2, 4]
    ESP_LOGI(TAG, "Resetting ESP32-C6 co-processor...");
    gpio_set_direction(C6_CHIP_PU_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(C6_CHIP_PU_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(C6_CHIP_PU_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. Inizializzazione NVS (Necessaria per Wi-Fi e calibrazioni)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 3. Inizializzazione Bus I2C (Pin 7 e 8 per Touch/RTC) [2]
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // Forza pull-up interni se gli esterni falliscono
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // 4. Inizializzazione Display JD9165 (MIPI DSI)
    init_jd9165_display();

    // 5. Inizializzazione Touch GT911
    init_touch_gt911(bus_handle);

    // 6. Montaggio SD Card (Slot 0) - Logica estratta dall'esempio standard [5]
    ESP_LOGI(TAG, "Initializing SD card (Slot 0)");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};
    sdmmc_card_t *card;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // WORKAROUND per IDF 5.5.3: se ESP-Hosted (Slot 1) è già attivo, non resettare l'host [1, 6]
#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0))
    host.init = NULL; // Impedisce il reset del controller SDMMC condiviso
#endif

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4; // JC1060 usa 4-line mode [7]

    ESP_LOGI(TAG, "Mounting filesystem...");
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SD card (0x%x)", ret);
    }
    else
    {
        ESP_LOGI(TAG, "SD card mounted. Capacity: %llu MB", ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
    }

    ESP_LOGI(TAG, "System up. Check logs for Wi-Fi Hosted status.");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}