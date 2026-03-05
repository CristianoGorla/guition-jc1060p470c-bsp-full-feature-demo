#pragma once

#include "esp_log.h"

#define BSP_LOG_TAG "BSP"
#define BSP_PANEL_FMT "| %-10s | "

#define BSP_LOGI_PANEL(unit, fmt, ...) \
    ESP_LOGI(BSP_LOG_TAG, BSP_PANEL_FMT fmt, unit, ##__VA_ARGS__)

#define BSP_LOGW_PANEL(unit, fmt, ...) \
    ESP_LOGW(BSP_LOG_TAG, BSP_PANEL_FMT fmt, unit, ##__VA_ARGS__)

#define BSP_LOGE_PANEL(unit, fmt, ...) \
    ESP_LOGE(BSP_LOG_TAG, BSP_PANEL_FMT fmt, unit, ##__VA_ARGS__)

#define BSP_LOGD_PANEL(unit, fmt, ...) \
    ESP_LOGD(BSP_LOG_TAG, BSP_PANEL_FMT fmt, unit, ##__VA_ARGS__)
