#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEBUG_TOOL_LOG_MONITOR = 0,
    DEBUG_TOOL_CAMERA_TEST,
    DEBUG_TOOL_SENSOR_MONITOR,
    DEBUG_TOOL_WIFI_SCANNER,
    DEBUG_TOOL_SD_BROWSER,
    DEBUG_TOOL_I2C_SCANNER,
    DEBUG_TOOL_SYSTEM_INFO,
    DEBUG_TOOL_GPIO_MONITOR,
    DEBUG_TOOL_PERFORMANCE,
    DEBUG_TOOL_MAX
} debug_tool_t;

typedef enum {
    PERIPH_STATUS_OK = 0,
    PERIPH_STATUS_WARNING,
    PERIPH_STATUS_ERROR,
    PERIPH_STATUS_DISABLED,
    PERIPH_STATUS_NOT_IMPL
} periph_status_t;

typedef void (*debug_tool_callback_t)(debug_tool_t tool, void *user_data);

typedef struct {
    debug_tool_callback_t tool_callback;
    void *user_data;
    bool auto_refresh;
    uint32_t refresh_interval_ms;
} dashboard_config_t;

#define DASHBOARD_CONFIG_DEFAULT() { \
    .tool_callback = NULL, \
    .user_data = NULL, \
    .auto_refresh = true, \
    .refresh_interval_ms = 2000, \
}

esp_err_t lvgl_dashboard_init(const dashboard_config_t *config);
esp_err_t lvgl_dashboard_deinit(void);
void lvgl_dashboard_load_screen(uint8_t screen_index, bool anim);
uint8_t lvgl_dashboard_get_active_screen(void);
void lvgl_dashboard_refresh_status(void);
lv_obj_t *lvgl_dashboard_get_tileview(void);

#ifdef __cplusplus
}
#endif
