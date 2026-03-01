#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "JD9165_GUITION";

// Init sequence completa dal BSP vendor - SPOSTATA FUORI DALLA FUNZIONE
static const jd9165_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49, 0x61, 0x02, 0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0}, // 2 lanes
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0}, // 16bpp
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

void init_jd9165_display(void)
{
    ESP_LOGI(TAG, "=== INIZIO INIT DISPLAY ===");

    // 1. Backlight su GPIO 23
    ESP_LOGI(TAG, ">>> Step 1: Configurazione GPIO23 backlight");
    gpio_set_direction(23, GPIO_MODE_OUTPUT);
    gpio_set_level(23, 0); // Spento inizialmente
    ESP_LOGI(TAG, ">>> Step 1: COMPLETATO");

    // 2. Power MIPI DSI PHY (LDO3 a 2.5V)
    ESP_LOGI(TAG, ">>> Step 2: Acquisizione LDO3 per MIPI DSI PHY");
    esp_ldo_channel_handle_t ldo_mipi = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi);
    ESP_LOGI(TAG, ">>> Step 2: esp_ldo_acquire_channel returned: %s (0x%x)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERRORE LDO3! Uscita.");
        return;
    }
    ESP_LOGI(TAG, ">>> Step 2: COMPLETATO - Attesa stabilizzazione PHY (50ms)");
    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. DSI Bus (750 Mbps come nel BSP vendor)
    ESP_LOGI(TAG, ">>> Step 3: Creazione DSI bus");
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 750,
    };
    ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    ESP_LOGI(TAG, ">>> Step 3: esp_lcd_new_dsi_bus returned: %s (0x%x)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERRORE DSI BUS! Uscita.");
        return;
    }
    ESP_LOGI(TAG, ">>> Step 3: COMPLETATO");

    // 4. DBI IO per comandi DCS
    ESP_LOGI(TAG, ">>> Step 4: Creazione DBI IO handle");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle);
    ESP_LOGI(TAG, ">>> Step 4: esp_lcd_new_panel_io_dbi returned: %s (0x%x)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERRORE DBI IO! Uscita.");
        return;
    }
    ESP_LOGI(TAG, ">>> Step 4: COMPLETATO");

    // 5. DPI Panel Config (esattamente come BSP vendor)
    ESP_LOGI(TAG, ">>> Step 5: Configurazione DPI panel");
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 52, // 51.2 arrotondato a 52
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = 1024,
            .v_size = 600,
            .hsync_back_porch = 160,
            .hsync_pulse_width = 24,
            .hsync_front_porch = 160,
            .vsync_back_porch = 21,
            .vsync_pulse_width = 2,
            .vsync_front_porch = 12,
        },
        .flags = {
            .use_dma2d = true,
        }};
    ESP_LOGI(TAG, ">>> Step 5: COMPLETATO");

    // 6. Vendor Config con init sequence custom
    ESP_LOGI(TAG, ">>> Step 6: Preparazione vendor config");
    jd9165_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(jd9165_lcd_init_cmd_t),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    ESP_LOGI(TAG, ">>> Step 6: COMPLETATO");

    // 7. Panel Device Config
    ESP_LOGI(TAG, ">>> Step 7: Configurazione panel device");
    esp_lcd_panel_dev_config_t panel_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = -1, // Reset gestito dal tuo board, non qui
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, ">>> Step 7: COMPLETATO");

    // 8. Crea pannello
    ESP_LOGI(TAG, ">>> Step 8: Creazione pannello JD9165");
    esp_lcd_panel_handle_t panel_handle = NULL;
    ret = esp_lcd_new_panel_jd9165(io_handle, &panel_dev_config, &panel_handle);
    ESP_LOGI(TAG, ">>> Step 8: esp_lcd_new_panel_jd9165 returned: %s (0x%x)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERRORE CREAZIONE PANEL! Uscita.");
        return;
    }
    ESP_LOGI(TAG, ">>> Step 8: COMPLETATO");

    ESP_LOGI(TAG, ">>> Step 9: Panel reset");
    ret = esp_lcd_panel_reset(panel_handle);
    ESP_LOGI(TAG, ">>> Step 9: esp_lcd_panel_reset returned: %s (0x%x)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERRORE PANEL RESET! Uscita.");
        return;
    }
    ESP_LOGI(TAG, ">>> Step 9: COMPLETATO");

    ESP_LOGI(TAG, ">>> Step 10: Panel init");
    ret = esp_lcd_panel_init(panel_handle);
    ESP_LOGI(TAG, ">>> Step 10: esp_lcd_panel_init returned: %s (0x%x)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERRORE PANEL INIT! Uscita.");
        return;
    }
    ESP_LOGI(TAG, ">>> Step 10: COMPLETATO");

    // 9. Accendi backlight
    ESP_LOGI(TAG, ">>> Step 11: Accensione backlight");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(23, 1);
    ESP_LOGI(TAG, ">>> Step 11: COMPLETATO");

    ESP_LOGI(TAG, "=== DISPLAY JD9165 PRONTO (1024x600 @ 52MHz, 2 lane DSI) ===");
}
