#include "lvgl_port.h"
#include "esp_lvgl_port.h"
#include "jd9165_bsp.h"
#include "gt911_bsp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "LVGL_PORT";

// Global handles (managed by esp_lvgl_port)
static lv_display_t *disp = NULL;
static lv_indev_t *indev_touchpad = NULL;

esp_err_t bsp_lvgl_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   LVGL v9.2.2 Initialization");
    ESP_LOGI(TAG, "   Using esp_lvgl_port v2.x");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Display: JD9165 (1024x600, MIPI DSI)");
    ESP_LOGI(TAG, "Touch: GT911 (I2C 0x14)");
    ESP_LOGI(TAG, "Memory: PSRAM double buffer (32MB @ 200MHz)");
#ifdef CONFIG_LVGL_ENABLE_PPA
    ESP_LOGI(TAG, "PPA: Enabled (HW rotation support)");
#endif
    ESP_LOGI(TAG, "Rotation: %d degrees\n", CONFIG_LVGL_DISP_ROTATION_DEGREES);
    
    // 1. Initialize esp_lvgl_port
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    ESP_LOGI(TAG, "[1/4] ✓ esp_lvgl_port initialized");
    
    // 2. Get display panel handle from JD9165 BSP
    esp_lcd_panel_handle_t panel_handle = bsp_jd9165_get_panel();
    if (panel_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get JD9165 panel handle");
        return ESP_FAIL;
    }
    
    // 3. Add display to LVGL
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .buffer_size = CONFIG_LVGL_DISP_RES_X * 50,  // 50 lines buffer
        .double_buffer = 1,
        .hres = CONFIG_LVGL_DISP_RES_X,
        .vres = CONFIG_LVGL_DISP_RES_Y,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,  // Use PSRAM for framebuffers
#ifdef CONFIG_LVGL_ENABLE_PPA
            .sw_rotate = (CONFIG_LVGL_DISP_ROTATION_DEGREES != 0),  // PPA rotation
#else
            .sw_rotate = false,
#endif
        }
    };
    
    disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to add display to LVGL");
        return ESP_FAIL;
    }
    
    // Apply rotation if configured
#ifdef CONFIG_LVGL_ENABLE_PPA
    if (CONFIG_LVGL_DISP_ROTATION_DEGREES == 90) {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    } else if (CONFIG_LVGL_DISP_ROTATION_DEGREES == 180) {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
    } else if (CONFIG_LVGL_DISP_ROTATION_DEGREES == 270) {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
    }
#endif
    
    ESP_LOGI(TAG, "[2/4] ✓ Display added (%dx%d, rotation=%d°)", 
             CONFIG_LVGL_DISP_RES_X, CONFIG_LVGL_DISP_RES_Y, 
             CONFIG_LVGL_DISP_ROTATION_DEGREES);
    
    // 4. Get touch handle from GT911 BSP
    esp_lcd_touch_handle_t touch_handle = bsp_gt911_get_handle();
    if (touch_handle == NULL) {
        ESP_LOGW(TAG, "No touch controller handle available");
    } else {
        // 5. Add touch input to LVGL
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = touch_handle,
        };
        
        indev_touchpad = lvgl_port_add_touch(&touch_cfg);
        if (indev_touchpad == NULL) {
            ESP_LOGE(TAG, "Failed to add touch to LVGL");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "[3/4] ✓ Touch input added (GT911)");
    }
    
    ESP_LOGI(TAG, "[4/4] ✓ LVGL tick task running (5ms period)\n");
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   LVGL Ready!");
    ESP_LOGI(TAG, "========================================\n");
    
    return ESP_OK;
}

void lvgl_tick_task(void *arg)
{
    // Not needed - esp_lvgl_port manages tick automatically
    ESP_LOGW(TAG, "lvgl_tick_task called but esp_lvgl_port handles ticks automatically");
    vTaskDelete(NULL);
}
