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
#include "rtc_rx8025t.h"
#include "rtc_test.h"
#include "es8311_audio.h"
#include "feature_flags.h"
#include "i2c_utils.h"
#include "hw_init.h"
#include "esp_hosted_wifi.h"

static const char *TAG = "GUITION_MAIN";

// I2C Bus (Audio + Touch + RTC on same bus!)
#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_SCL_IO 8

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

#if ENABLE_SD_CARD
#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
static esp_err_t sdmmc_host_init_dummy(void) 
{ 
    LOG_SD(TAG, "Skipping sdmmc_host_init (controller already initialized by ESP-Hosted)");
    return ESP_OK; 
}

static esp_err_t sdmmc_host_deinit_dummy(void) 
{ 
    LOG_SD(TAG, "Skipping sdmmc_host_deinit (keep controller active for ESP-Hosted)");
    return ESP_OK; 
}
#endif
#endif

#if ENABLE_I2C
/**
 * @brief Check GPIO raw state and detect I2C bus faults
 * @return true if both SDA and SCL are HIGH (healthy), false otherwise
 */
static bool check_i2c_gpio_state(const char *context)
{
    ESP_LOGI(TAG, "\n=== I2C GPIO STATE CHECK (%s) ===", context);
    
    // Reset GPIO to default state
    gpio_reset_pin(GPIO_NUM_7);
    gpio_reset_pin(GPIO_NUM_8);
    
    // Configure as input with pullup
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_7, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_8, GPIO_PULLUP_ONLY);
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    int sda_level = gpio_get_level(GPIO_NUM_7);
    int scl_level = gpio_get_level(GPIO_NUM_8);
    
    ESP_LOGI(TAG, "GPIO7 (SDA) level: %d", sda_level);
    ESP_LOGI(TAG, "GPIO8 (SCL) level: %d", scl_level);
    
    bool gpio_ok = (sda_level == 1 && scl_level == 1);
    
    if (gpio_ok) {
        ESP_LOGI(TAG, "✓ GPIO levels OK (both HIGH with pullups)");
    } else {
        ESP_LOGE(TAG, "✗ GPIO FAULT DETECTED!");
        if (sda_level == 0) {
            ESP_LOGE(TAG, "  → SDA (GPIO7) stuck LOW - possible short to GND or slave holding bus");
        }
        if (scl_level == 0) {
            ESP_LOGE(TAG, "  → SCL (GPIO8) stuck LOW - possible short to GND or clock stretch issue");
        }
        ESP_LOGE(TAG, "  Possible causes:");
        ESP_LOGE(TAG, "    1. Missing or damaged pull-up resistors");
        ESP_LOGE(TAG, "    2. Short circuit on PCB traces");
        ESP_LOGE(TAG, "    3. I2C slave holding bus (power issue)");
        ESP_LOGE(TAG, "    4. GPIO conflict with other peripheral (e.g., MIPI DSI)");
    }
    ESP_LOGI(TAG, "===========================\n");
    
    return gpio_ok;
}

/**
 * @brief Attempt to unstick SDA by pulsing SCL (I2C bus recovery procedure)
 * This implements the standard I2C bus recovery: pulse SCL until SDA goes HIGH
 */
static void i2c_bus_unstick_sda(void)
{
    ESP_LOGI(TAG, "=== ATTEMPTING TO UNSTICK SDA (I2C Recovery Procedure) ===");
    
    // Configure SCL as output, SDA as input
    gpio_reset_pin(GPIO_NUM_7);
    gpio_reset_pin(GPIO_NUM_8);
    
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_7, GPIO_PULLUP_ONLY);
    
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_8, 1);
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Check if SDA is stuck LOW
    if (gpio_get_level(GPIO_NUM_7) == 0) {
        ESP_LOGI(TAG, "SDA is LOW - pulsing SCL to release it...");
        
        // Pulse SCL up to 9 times (standard I2C recovery)
        for (int i = 0; i < 9; i++) {
            gpio_set_level(GPIO_NUM_8, 0);
            vTaskDelay(pdMS_TO_TICKS(1));
            gpio_set_level(GPIO_NUM_8, 1);
            vTaskDelay(pdMS_TO_TICKS(1));
            
            if (gpio_get_level(GPIO_NUM_7) == 1) {
                ESP_LOGI(TAG, "✓ SDA released after %d SCL pulses", i + 1);
                break;
            }
        }
        
        // Final check
        if (gpio_get_level(GPIO_NUM_7) == 0) {
            ESP_LOGE(TAG, "✗ SDA still LOW after 9 SCL pulses - hardware issue!");
        }
    } else {
        ESP_LOGI(TAG, "SDA already HIGH - no unstick needed");
    }
    
    // Generate I2C STOP condition
    ESP_LOGI(TAG, "Generating I2C STOP condition...");
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_7, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(GPIO_NUM_8, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(GPIO_NUM_7, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    
    ESP_LOGI(TAG, "========================================\n");
}

/**
 * @brief Re-initialize I2C bus (recovery from lockup)
 * @param bus_handle Pointer to bus handle (will be updated)
 * @return ESP_OK on success
 */
static esp_err_t reinit_i2c_bus(i2c_master_bus_handle_t *bus_handle)
{
    ESP_LOGI(TAG, "=== I2C BUS RE-INITIALIZATION ===");
    
    if (*bus_handle != NULL) {
        ESP_LOGI(TAG, "Deleting existing I2C bus...");
        esp_err_t ret = i2c_del_master_bus(*bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete I2C bus (0x%x), forcing NULL", ret);
        }
        *bus_handle = NULL;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Force GPIO reset before re-init
    ESP_LOGI(TAG, "Resetting I2C GPIO pins...");
    gpio_reset_pin(GPIO_NUM_7);
    gpio_reset_pin(GPIO_NUM_8);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Re-create bus
    ESP_LOGI(TAG, "Creating new I2C bus (SDA=%d, SCL=%d)...", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, bus_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ I2C bus re-initialized successfully");
    } else {
        ESP_LOGE(TAG, "✗ Failed to re-initialize I2C bus (0x%x)", ret);
    }
    
    ESP_LOGI(TAG, "=================================\n");
    return ret;
}

/**
 * @brief Aggressive I2C recovery with multiple attempts
 * @param bus_handle Pointer to bus handle
 * @return true if recovery successful
 */
static bool aggressive_i2c_recovery(i2c_master_bus_handle_t *bus_handle)
{
    ESP_LOGW(TAG, "\n⚠ STARTING AGGRESSIVE I2C RECOVERY ⚠\n");
    
    const int max_attempts = 3;
    
    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        ESP_LOGI(TAG, "--- Recovery Attempt %d/%d ---\n", attempt, max_attempts);
        
        // Step 1: Try to unstick SDA by pulsing SCL
        i2c_bus_unstick_sda();
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Step 2: Check if unstick worked
        bool gpio_ok = check_i2c_gpio_state("after SDA unstick");
        
        if (gpio_ok) {
            ESP_LOGI(TAG, "✓ GPIO unstuck! Proceeding with bus re-init...\n");
        } else {
            ESP_LOGW(TAG, "GPIO still stuck after unstick attempt\n");
        }
        
        // Step 3: Re-initialize bus regardless
        if (reinit_i2c_bus(bus_handle) != ESP_OK) {
            ESP_LOGE(TAG, "Bus re-init failed on attempt %d\n", attempt);
            continue;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Step 4: Verify recovery
        gpio_ok = check_i2c_gpio_state("verification");
        
        if (gpio_ok) {
            ESP_LOGI(TAG, "\n✓✓✓ I2C RECOVERY SUCCESSFUL (attempt %d/%d) ✓✓✓\n", 
                     attempt, max_attempts);
            return true;
        }
        
        ESP_LOGW(TAG, "Recovery attempt %d failed - GPIO still stuck\n", attempt);
        
        // Exponential backoff between attempts
        if (attempt < max_attempts) {
            int delay_ms = 200 * attempt;
            ESP_LOGI(TAG, "Waiting %dms before next attempt...\n", delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
    
    ESP_LOGE(TAG, "\n✗✗✗ I2C RECOVERY FAILED after %d attempts ✗✗✗\n", max_attempts);
    ESP_LOGE(TAG, "Possible hardware issue:");
    ESP_LOGE(TAG, "  1. MIPI DSI has permanent GPIO conflict");
    ESP_LOGE(TAG, "  2. GT911 or ES8311 holding SDA LOW (power/reset issue)");
    ESP_LOGE(TAG, "  3. External pull-up resistors too weak or missing");
    ESP_LOGE(TAG, "  4. PCB short circuit on SDA line\n");
    
    return false;
}
#endif

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

    // ========== 1. NVS ==========
#if ENABLE_NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    LOG_NVS(TAG, "NVS initialized");
#else
    ESP_LOGI(TAG, "NVS disabled by feature flags");
#endif

    // ========== 2. GPIO RAW STATE CHECK (before I2C config) ==========
#if ENABLE_I2C
    check_i2c_gpio_state("before I2C init");
#endif

    // ========== 3. I2C Bus (EARLY INIT - before peripherals!) ==========
#if ENABLE_I2C
    ESP_LOGI(TAG, "=== EARLY I2C INIT (before peripheral resets) ===");
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
    LOG_I2C(TAG, "✓ I2C bus initialized EARLY (SDA=%d, SCL=%d)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    LOG_I2C(TAG, "This prevents bus lockup from GT911 reset\n");
#else
    ESP_LOGI(TAG, "I2C disabled by feature flags");
    i2c_master_bus_handle_t bus_handle = NULL;
#endif

    // ========== 4. AUDIO CODEC (ES8311 - before RTC!) ==========
#if ENABLE_AUDIO
    if (bus_handle) {
        LOG_AUDIO(TAG, "\n========== ES8311 AUDIO CODEC INIT ==========");
        LOG_AUDIO(TAG, "Initializing ES8311 FIRST to release I2C bus if stuck...");
        
        ret = es8311_init(bus_handle);
        if (ret == ESP_OK) {
            LOG_AUDIO(TAG, "✓ ES8311 initialized and powered down");
            LOG_AUDIO(TAG, "I2C bus should be free now");
        } else {
            ESP_LOGW(TAG, "ES8311 init failed or not responding (0x%x)", ret);
            ESP_LOGW(TAG, "This is OK if codec is not populated on this board");
        }
        
        LOG_AUDIO(TAG, "========== ES8311 INIT COMPLETE ==========\n");
    }
#else
    ESP_LOGI(TAG, "Audio codec disabled by feature flags");
#endif

    // ========== 4.5 I2C SCAN (after audio, BEFORE RTC test!) ==========
#if ENABLE_I2C && ENABLE_I2C_SCAN
    if (bus_handle) {
        ESP_LOGI(TAG, "\n========== I2C BUS SCAN (after audio init, before RTC) ==========");
        ESP_LOGI(TAG, "This scan should show ES8311 (0x18), GT911 (0x14), and RTC (0x32)\n");
        i2c_scan_bus(bus_handle);
        ESP_LOGI(TAG, "========== I2C BUS SCAN COMPLETE ==========\n");
    }
#endif

    // ========== 5. RTC Init (AFTER audio codec!) ==========
#if ENABLE_RTC
    if (bus_handle) {
        LOG_RTC(TAG, "\n========== RTC EARLY INITIALIZATION ==========");
        LOG_RTC(TAG, "Initializing RTC AFTER ES8311 (bus should be clear now)...");
        
#if ENABLE_RTC_HW_TEST
        // Run comprehensive hardware test
        rtc_hardware_test(bus_handle);
#else
        // Standard init
        ESP_LOGI(TAG, "Probing RTC at address 0x32...");
        ret = i2c_master_probe(bus_handle, 0x32, 500);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✓ RTC responds to probe!");
            
            ret = rtc_rx8025t_init(bus_handle);
            if (ret == ESP_OK) {
                LOG_RTC(TAG, "✓ RTC initialized successfully");
                
#if ENABLE_RTC_TEST
                rtc_time_t current_time;
                ret = rtc_rx8025t_get_time(&current_time);
                if (ret == ESP_OK) {
                    LOG_RTC(TAG, "Current RTC time: 20%02d-%02d-%02d (wday=%d) %02d:%02d:%02d",
                            current_time.year, current_time.month, current_time.day,
                            current_time.wday,
                            current_time.hour, current_time.minute, current_time.second);
                }
                
                bool pon_flag, vlf_flag;
                if (rtc_rx8025t_check_power_on_flag(&pon_flag) == ESP_OK) {
                    LOG_RTC(TAG, "PON Flag (Power-On): %s", pon_flag ? "SET" : "CLEAR");
                }
                if (rtc_rx8025t_check_voltage_low_flag(&vlf_flag) == ESP_OK) {
                    LOG_RTC(TAG, "VLF Flag (Voltage Low): %s", vlf_flag ? "SET" : "CLEAR");
                }
#endif
            } else {
                ESP_LOGE(TAG, "Failed to initialize RTC (0x%x)", ret);
            }
        } else {
            ESP_LOGE(TAG, "RTC does NOT respond to probe (0x%x)", ret);
        }
#endif // ENABLE_RTC_HW_TEST
        
        LOG_RTC(TAG, "========== RTC EARLY INIT COMPLETE ==========\n");
    } else {
        ESP_LOGW(TAG, "RTC init skipped (I2C not initialized)");
    }
#else
    ESP_LOGI(TAG, "RTC disabled by feature flags");
#endif

    // ========== 6. SD Card ==========
#if ENABLE_SD_CARD
    LOG_SD(TAG, "Initializing SD card (Slot 0 - forced)...");

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
    vTaskDelay(pdMS_TO_TICKS(250));
    LOG_SD(TAG, "SD Card power enabled via GPIO%d (waited 250ms)", CONFIG_EXAMPLE_PIN_CARD_POWER_RESET);
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
    LOG_SD(TAG, "ESP-Hosted detected - initializing slot 0 (no deinit)");
    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init slot 0 (0x%x)", ret);
        goto sd_failed;
    }
    LOG_SD(TAG, "Slot 0 initialized successfully");
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    LOG_SD(TAG, "Forced host.slot = SDMMC_HOST_SLOT_0");

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
        LOG_SD(TAG, "✓ SD card mounted successfully");
        LOG_SD(TAG, "Card name: %s", card->cid.name);
        LOG_SD(TAG, "Capacity: %llu MB",
                 ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
    }

sd_failed:
#else
    ESP_LOGI(TAG, "SD card disabled by feature flags");
#endif

    // ========== 7. WiFi (ESP-Hosted) ==========
#if ENABLE_WIFI
    LOG_WIFI(TAG, "Initializing WiFi (ESP-Hosted via C6)...");
    init_wifi();
    LOG_WIFI(TAG, "✓ WiFi initialized - scanning networks...");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    if (do_wifi_scan_and_check(NULL)) {
        LOG_WIFI(TAG, "✓ WiFi scan successful - ESP-Hosted is working!");
    } else {
        ESP_LOGW(TAG, "WiFi scan returned 0 networks (check C6 firmware)");
    }
#else
    ESP_LOGI(TAG, "WiFi disabled by feature flags");
#endif

    // ========== 8. Hardware Reset (GT911/ES8311 - AFTER audio/RTC init!) ==========
#if ENABLE_DISPLAY || ENABLE_TOUCH
    ESP_LOGI(TAG, "\nRunning hardware reset for GT911...");
    ESP_LOGI(TAG, "(Audio and RTC already initialized)");
    hw_reset_all_peripherals();
    
    ESP_LOGI(TAG, "Waiting 500ms for I2C bus stabilization after GT911 reset...");
    vTaskDelay(pdMS_TO_TICKS(500));
#else
    ESP_LOGI(TAG, "Hardware reset skipped (no Display/Touch enabled)");
#endif

    // ========== 9. Display ==========
#if ENABLE_DISPLAY
    ESP_LOGI(TAG, "Initializing display...");
    panel_handle = init_jd9165_display();
    ESP_LOGI(TAG, "✓ Display ready (1024x600)");
    
    // CRITICAL: Check I2C GPIO state after display init and attempt aggressive recovery
#if ENABLE_I2C
    vTaskDelay(pdMS_TO_TICKS(100));
    bool gpio_healthy = check_i2c_gpio_state("after display init");
    
    if (!gpio_healthy) {
        bool recovery_ok = aggressive_i2c_recovery(&bus_handle);
        
        if (!recovery_ok) {
            ESP_LOGW(TAG, "\n⚠ WARNING: Continuing with potentially broken I2C bus");
            ESP_LOGW(TAG, "Touch controller may not work properly\n");
        }
    } else {
        ESP_LOGI(TAG, "✓ I2C GPIO remains healthy after display init (no recovery needed)\n");
    }
    
    // GT911 DUAL ADDRESS TEST (0x14 vs 0x5D)
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   GT911 DUAL ADDRESS TEST");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Testing GT911 presence at both possible I2C addresses...");
    ESP_LOGI(TAG, "(Address determined by INT pin state during reset)\n");
    
    esp_err_t ret14 = i2c_master_probe(bus_handle, 0x14, 100);
    esp_err_t ret5d = i2c_master_probe(bus_handle, 0x5D, 100);
    
    ESP_LOGI(TAG, "[0x14] INT=HIGH config:  %s", ret14 == ESP_OK ? "✓ RESPONDS" : "✗ NO RESPONSE");
    ESP_LOGI(TAG, "[0x5D] INT=LOW config:   %s", ret5d == ESP_OK ? "✓ RESPONDS" : "✗ NO RESPONSE");
    ESP_LOGI(TAG, "");
    
    if (ret14 == ESP_OK && ret5d != ESP_OK) {
        ESP_LOGI(TAG, "✓ CORRECT: GT911 locked to 0x14 (INT=HIGH reset)");
        ESP_LOGI(TAG, "   Hardware reset sequence executed correctly.");
    } else if (ret5d == ESP_OK && ret14 != ESP_OK) {
        ESP_LOGW(TAG, "⚠ WARNING: GT911 at 0x5D (INT=LOW reset)");
        ESP_LOGW(TAG, "   Expected 0x14. Check hw_init.c reset sequence.");
    } else if (ret14 == ESP_OK && ret5d == ESP_OK) {
        ESP_LOGE(TAG, "✗ ERROR: GT911 responds at BOTH addresses!");
        ESP_LOGE(TAG, "   This indicates incomplete/incorrect reset.");
        ESP_LOGE(TAG, "   GT911 may be in undefined state.");
    } else {
        ESP_LOGE(TAG, "✗ ERROR: GT911 not responding at any address!");
        ESP_LOGE(TAG, "   Check I2C bus, power, and hardware connections.");
    }
    
    ESP_LOGI(TAG, "========================================\n");
#endif
#else
    ESP_LOGI(TAG, "Display disabled by feature flags");
#endif

    // ========== 10. I2C SCAN (final) ==========
#if ENABLE_I2C && ENABLE_I2C_SCAN
    if (bus_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "\n========== I2C BUS SCAN (final - after all init) ==========");
        i2c_scan_bus(bus_handle);
        ESP_LOGI(TAG, "========== I2C BUS SCAN COMPLETE ==========\n");
    }
#endif

    // ========== 11. Touch ==========
#if ENABLE_TOUCH
    if (bus_handle) {
        LOG_TOUCH(TAG, "Initializing touch...");
        touch_handle = init_touch_gt911(bus_handle);
        LOG_TOUCH(TAG, "Touch ready");
    } else {
        ESP_LOGW(TAG, "Touch skipped (I2C not initialized)");
    }
#else
    ESP_LOGI(TAG, "Touch disabled by feature flags");
#endif

    ESP_LOGI(TAG, "\n=== System ready ===");

    vTaskDelay(pdMS_TO_TICKS(500));

#if ENABLE_DISPLAY && ENABLE_DISPLAY_TEST
    ESP_LOGI(TAG, "\n=== TEST 1: Fill RED ===");
    test_display_fill_color(0xF800);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "\n=== TEST 2: Fill GREEN ===");
    test_display_fill_color(0x07E0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "\n=== TEST 3: Fill BLUE ===");
    test_display_fill_color(0x001F);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "\n=== TEST 4: RGB Pattern ===");
    test_display_rgb_pattern();
    vTaskDelay(pdMS_TO_TICKS(2000));
#endif

#if ENABLE_TOUCH && ENABLE_TOUCH_TEST
    ESP_LOGI(TAG, "\n=== TEST 5: Touch Input ===");
    test_touch_read_loop();
#endif

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
