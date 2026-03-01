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

#include "display_jd9165.h"
#include "touch_gt911.h"

static const char *TAG = "GUITION_MAIN";

#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_SCL_IO 8

// Handle globali per display e touch
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

// Workaround per ESP-Hosted
#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
static esp_err_t sdmmc_host_init_dummy(void) 
{ 
    ESP_LOGI(TAG, "Skipping sdmmc_host_init (controller already initialized by ESP-Hosted)");
    return ESP_OK; 
}

static esp_err_t sdmmc_host_deinit_dummy(void) 
{ 
    ESP_LOGI(TAG, "Skipping sdmmc_host_deinit (keep controller active for ESP-Hosted)");
    return ESP_OK; 
}
#endif

// Test RAW Display: riempie schermo di colore
void test_display_fill_color(uint16_t color)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Panel handle not initialized!");
        return;
    }

    const int width = 1024;
    const int height = 600;
    const int buffer_lines = 10; // Processa 10 linee alla volta
    
    uint16_t *line_buffer = heap_caps_malloc(width * buffer_lines * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }

    // Riempi buffer con il colore
    for (int i = 0; i < width * buffer_lines; i++) {
        line_buffer[i] = color;
    }

    ESP_LOGI(TAG, "Filling display with color 0x%04X...", color);

    // Scrivi tutte le linee
    for (int y = 0; y < height; y += buffer_lines) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, width, y + buffer_lines, line_buffer);
    }

    free(line_buffer);
    ESP_LOGI(TAG, "Display fill complete");
}

// Test RAW Display: pattern RGB
void test_display_rgb_pattern(void)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Panel handle not initialized!");
        return;
    }

    ESP_LOGI(TAG, "Drawing RGB pattern...");
    
    // Rosso, Verde, Blu (RGB565 format)
    const uint16_t RED   = 0xF800;  // 0b1111100000000000
    const uint16_t GREEN = 0x07E0;  // 0b0000011111100000
    const uint16_t BLUE  = 0x001F;  // 0b0000000000011111
    
    const int width = 1024;
    const int height = 600;
    const int stripe_width = width / 3;
    
    uint16_t *line_buffer = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }

    // Prepara linea con pattern RGB
    for (int x = 0; x < width; x++) {
        if (x < stripe_width) {
            line_buffer[x] = RED;
        } else if (x < stripe_width * 2) {
            line_buffer[x] = GREEN;
        } else {
            line_buffer[x] = BLUE;
        }
    }

    // Disegna tutte le linee
    for (int y = 0; y < height; y++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, width, y + 1, line_buffer);
    }

    free(line_buffer);
    ESP_LOGI(TAG, "RGB pattern complete");
}

// Test RAW Touch: API corretta con struct point_data
void test_touch_read_loop(void)
{
    if (!touch_handle) {
        ESP_LOGE(TAG, "Touch handle not initialized!");
        return;
    }

    ESP_LOGI(TAG, "Touch test started. Touch the screen...");
    ESP_LOGI(TAG, "Press Ctrl+C to stop");

    esp_lcd_touch_point_data_t point_data[1];
    uint8_t touch_cnt = 0;

    while (1) {
        esp_lcd_touch_read_data(touch_handle);
        
        esp_err_t ret = esp_lcd_touch_get_data(touch_handle, point_data, &touch_cnt, 1);
        
        if (ret == ESP_OK && touch_cnt > 0) {
            ESP_LOGI(TAG, "Touch detected: X=%d, Y=%d, Strength=%d", 
                     point_data[0].x, point_data[0].y, point_data[0].strength);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Poll ogni 100ms
    }
}

void app_main(void)
{
    esp_err_t ret;

    // 1. NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // 2. SD Card (con workaround)
    ESP_LOGI(TAG, "Initializing SD card (Slot %d)...", CONFIG_EXAMPLE_SDMMC_SLOT);

#ifdef CONFIG_EXAMPLE_PIN_CARD_POWER_RESET
    gpio_config_t pwr_io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_EXAMPLE_PIN_CARD_POWER_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_io_conf);
    gpio_set_level(CONFIG_EXAMPLE_PIN_CARD_POWER_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "SD Card power enabled via GPIO%d", CONFIG_EXAMPLE_PIN_CARD_POWER_RESET);
#endif

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

#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    ESP_LOGI(TAG, "ESP-Hosted detected - using workaround");
    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init slot 0 (0x%x)", ret);
        goto sd_failed;
    }
    ESP_LOGI(TAG, "Slot 0 initialized successfully");
#endif

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = CONFIG_EXAMPLE_SDMMC_SLOT;

#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    host.init = &sdmmc_host_init_dummy;
    host.deinit = &sdmmc_host_deinit_dummy;
#endif

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

sd_failed:

    // 3. I2C
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

    // 4. Display
    ESP_LOGI(TAG, "Initializing display...");
    panel_handle = init_jd9165_display();
    ESP_LOGI(TAG, "Display ready");

    // 5. Touch
    ESP_LOGI(TAG, "Initializing touch...");
    touch_handle = init_touch_gt911(bus_handle);
    ESP_LOGI(TAG, "Touch ready");

    ESP_LOGI(TAG, "=== System ready ===");

    // ============================================================
    // TEST RAW DISPLAY E TOUCH
    // ============================================================

    vTaskDelay(pdMS_TO_TICKS(500));

    // Test 1: Riempi schermo di rosso
    ESP_LOGI(TAG, "\n=== TEST 1: Fill RED ===");
    test_display_fill_color(0xF800);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 2: Riempi schermo di verde
    ESP_LOGI(TAG, "\n=== TEST 2: Fill GREEN ===");
    test_display_fill_color(0x07E0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 3: Riempi schermo di blu
    ESP_LOGI(TAG, "\n=== TEST 3: Fill BLUE ===");
    test_display_fill_color(0x001F);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 4: Pattern RGB a strisce
    ESP_LOGI(TAG, "\n=== TEST 4: RGB Pattern ===");
    test_display_rgb_pattern();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 5: Touch input loop (infinito)
    ESP_LOGI(TAG, "\n=== TEST 5: Touch Input ===");
    test_touch_read_loop();

    // Non dovrebbe mai arrivare qui
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
