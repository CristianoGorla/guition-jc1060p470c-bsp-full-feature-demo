#include "lvgl_port.h"
#include "jd9165_bsp.h"
#include "gt911_bsp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LVGL_PORT";

// LVGL objects
static lv_display_t *disp = NULL;
static lv_indev_t *indev_touchpad = NULL;
static TaskHandle_t lvgl_task_handle = NULL;

// PSRAM buffers (1024x600 RGB565 = 1228800 bytes per buffer)
#define LVGL_BUFFER_SIZE (CONFIG_LVGL_DISP_RES_X * CONFIG_LVGL_DISP_RES_Y * 2)
static uint8_t *lvgl_buf1 = NULL;
static uint8_t *lvgl_buf2 = NULL;

/**
 * @brief Display flush callback for JD9165
 */
static void jd9165_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    
    // Call BSP JD9165 flush function
    esp_err_t ret = bsp_jd9165_flush_area(x1, y1, x2, y2, (uint16_t *)px_map);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display flush failed: %s", esp_err_to_name(ret));
    }
    
    lv_display_flush_ready(disp_drv);
}

/**
 * @brief Touch read callback for GT911
 */
static void gt911_read_cb(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    uint16_t x, y;
    bool pressed;
    
    // Read touch from BSP GT911 driver
    esp_err_t ret = bsp_gt911_read_pos(&x, &y, &pressed);
    
    if (ret == ESP_OK && pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief LVGL tick handler task (FreeRTOS)
 */
void lvgl_tick_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL tick task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5));
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

esp_err_t bsp_lvgl_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   LVGL v9.2.2 Initialization");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Display: JD9165 (1024x600, MIPI DSI)");
    ESP_LOGI(TAG, "Touch: GT911 (I2C 0x14)");
    ESP_LOGI(TAG, "Memory: PSRAM double buffer (32MB @ 200MHz)\n");
    
    // 1. Initialize LVGL library
    lv_init();
    ESP_LOGI(TAG, "[1/5] ✓ LVGL library initialized");
    
    // 2. Allocate buffers in PSRAM
    lvgl_buf1 = heap_caps_malloc(LVGL_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    lvgl_buf2 = heap_caps_malloc(LVGL_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    
    if (!lvgl_buf1 || !lvgl_buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers in PSRAM");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "[2/5] ✓ Allocated %d bytes x2 in PSRAM", LVGL_BUFFER_SIZE);
    
    // 3. Create display driver
    disp = lv_display_create(CONFIG_LVGL_DISP_RES_X, CONFIG_LVGL_DISP_RES_Y);
    lv_display_set_flush_cb(disp, jd9165_flush_cb);
    lv_display_set_buffers(disp, lvgl_buf1, lvgl_buf2, LVGL_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "[3/5] ✓ Display driver created (%dx%d)", 
             CONFIG_LVGL_DISP_RES_X, CONFIG_LVGL_DISP_RES_Y);
    
    // 4. Create touch input device
    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, gt911_read_cb);
    ESP_LOGI(TAG, "[4/5] ✓ Touch input device created");
    
    // 5. Create LVGL tick task
    BaseType_t ret = xTaskCreate(
        lvgl_tick_task,
        "lvgl_tick",
        4096,
        NULL,
        5,
        &lvgl_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL tick task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[5/5] ✓ LVGL tick task created (5ms period)\n");
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   LVGL Ready!");
    ESP_LOGI(TAG, "========================================\n");
    
    return ESP_OK;
}
