/**
 * @file debug_logging.h
 * @brief Aggressive Debug Logging for Display and LVGL
 * 
 * Conditional logging macros for deep debugging of:
 * - Display driver (MIPI-DSI commands, timing, backlight)
 * - LVGL initialization (buffers, memory allocation)
 * - LVGL rendering (flush callbacks, dirty areas)
 * 
 * Enable via menuconfig:
 * - CONFIG_DEBUG_DISPLAY_AGGRESSIVE
 * - CONFIG_DEBUG_LVGL_AGGRESSIVE
 */

#pragma once

#include "esp_log.h"
#include "sdkconfig.h"

/* ========================================================================
 * DISPLAY DRIVER DEBUG
 * ======================================================================== */

#ifdef CONFIG_DEBUG_DISPLAY_AGGRESSIVE
    #define DISP_DEBUG(tag, fmt, ...) \
        ESP_LOGI(tag, "\033[1;36m🔍 DISP\033[0m " fmt, ##__VA_ARGS__)
    
    #define DISP_TRACE(tag, fmt, ...) \
        ESP_LOGD(tag, "  \033[0;36m↳\033[0m " fmt, ##__VA_ARGS__)
    
    #define DISP_CMD(tag, cmd, delay_ms) \
        ESP_LOGD(tag, "  \033[0;33m➜ CMD\033[0m 0x%02X (delay %dms)", cmd, delay_ms)
    
    #define DISP_DATA(tag, data, len) \
        do { \
            ESP_LOGD(tag, "  \033[0;33m➜ DATA\033[0m [%d bytes]", len); \
            ESP_LOG_BUFFER_HEX_LEVEL(tag, data, len, ESP_LOG_DEBUG); \
        } while(0)
#else
    #define DISP_DEBUG(tag, fmt, ...)
    #define DISP_TRACE(tag, fmt, ...)
    #define DISP_CMD(tag, cmd, delay_ms)
    #define DISP_DATA(tag, data, len)
#endif

/* ========================================================================
 * LVGL DEBUG
 * ======================================================================== */

#ifdef CONFIG_DEBUG_LVGL_AGGRESSIVE
    #define LVGL_DEBUG(fmt, ...) \
        ESP_LOGI("LVGL", "\033[1;35m🎨 LVGL\033[0m " fmt, ##__VA_ARGS__)
    
    #define LVGL_TRACE(fmt, ...) \
        ESP_LOGD("LVGL", "  \033[0;35m↳\033[0m " fmt, ##__VA_ARGS__)
    
    #define LVGL_FLUSH(area, ptr) \
        ESP_LOGD("LVGL", "  \033[0;32m➜ FLUSH\033[0m [%d,%d] → [%d,%d] (%dx%d px) ptr=%p", \
                 (area)->x1, (area)->y1, (area)->x2, (area)->y2, \
                 lv_area_get_width(area), lv_area_get_height(area), ptr)
    
    #define LVGL_BUFFER(name, ptr, size) \
        ESP_LOGD("LVGL", "  \033[0;34m➜ BUFFER\033[0m %s: %p (%d bytes) [%s]", \
                 name, ptr, size, \
                 esp_ptr_external_ram(ptr) ? "PSRAM" : "DRAM")
#else
    #define LVGL_DEBUG(fmt, ...)
    #define LVGL_TRACE(fmt, ...)
    #define LVGL_FLUSH(area, ptr)
    #define LVGL_BUFFER(name, ptr, size)
#endif

/* ========================================================================
 * MEMORY DEBUG
 * ======================================================================== */

#if defined(CONFIG_DEBUG_DISPLAY_AGGRESSIVE) || defined(CONFIG_DEBUG_LVGL_AGGRESSIVE)
    #define MEM_DEBUG(tag, fmt, ...) \
        ESP_LOGI(tag, "\033[1;33m📦 MEM\033[0m " fmt, ##__VA_ARGS__)
    
    #define MEM_TRACE(tag, fmt, ...) \
        ESP_LOGD(tag, "  \033[0;33m↳\033[0m " fmt, ##__VA_ARGS__)
#else
    #define MEM_DEBUG(tag, fmt, ...)
    #define MEM_TRACE(tag, fmt, ...)
#endif

/* ========================================================================
 * TIMING DEBUG
 * ======================================================================== */

#if defined(CONFIG_DEBUG_DISPLAY_AGGRESSIVE) || defined(CONFIG_DEBUG_LVGL_AGGRESSIVE)
    #include "esp_timer.h"
    
    #define TIMING_START(var) \
        uint64_t var = esp_timer_get_time()
    
    #define TIMING_END(tag, var, label) \
        do { \
            uint64_t elapsed = esp_timer_get_time() - var; \
            ESP_LOGD(tag, "  \033[0;36m⏱️ %s\033[0m: %llu us", label, elapsed); \
        } while(0)
#else
    #define TIMING_START(var)
    #define TIMING_END(tag, var, label)
#endif
