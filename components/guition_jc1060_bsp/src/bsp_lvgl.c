/*
 * SPDX-FileCopyrightText: 2024 Cristiano Gorla
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Guition JC1060P470C - LVGL 9.2.2 BSP Implementation
 */

#include "sdkconfig.h"

#ifdef CONFIG_BSP_ENABLE_LVGL

#include "bsp_lvgl.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

/* External handles from BSP components (already initialized in bsp_board_init) */
extern esp_lcd_panel_handle_t g_panel_handle;  // From jd9165 driver
extern esp_lcd_touch_handle_t g_touch_handle;  // From gt911 driver

static const char *TAG = "bsp_lvgl";

/* Global LVGL handles */
static lv_display_t *lvgl_display = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;
static bool lvgl_initialized = false;

/* Calculate buffer size based on Kconfig */
#define LVGL_BUFFER_HEIGHT CONFIG_BSP_LVGL_BUFFER_LINES
#define LVGL_BUFFER_SIZE (CONFIG_BSP_DISPLAY_WIDTH * LVGL_BUFFER_HEIGHT)

/**
 * @brief Initialize LVGL display with BSP display drivers
 */
static lv_display_t *bsp_lvgl_init_display(const bsp_lvgl_config_t *config)
{
    ESP_LOGI(TAG, "Initializing LVGL display (1024x600, %d buffer lines)", config->buffer.buffer_lines);

    /* Display already initialized by bsp_board_init() - just retrieve handle */
    if (g_panel_handle == NULL) {
        ESP_LOGE(TAG, "Display not initialized! Call bsp_board_init() first");
        return NULL;
    }
    ESP_LOGI(TAG, "Using existing display panel handle");

    /* Allocate LVGL draw buffers based on Kconfig */
    size_t buffer_size = LVGL_BUFFER_SIZE * sizeof(lv_color_t);
    uint32_t caps = MALLOC_CAP_DEFAULT;
    
    if (config->buffer.use_spiram) {
        caps = MALLOC_CAP_SPIRAM;
        ESP_LOGI(TAG, "Using SPIRAM for LVGL buffers");
    } else if (config->buffer.use_dma) {
        caps = MALLOC_CAP_DMA;
        ESP_LOGI(TAG, "Using DMA-capable memory for LVGL buffers");
    }

    void *buf1 = heap_caps_malloc(buffer_size, caps);
    if (buf1 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer 1 (%zu bytes)", buffer_size);
        return NULL;
    }
    ESP_LOGI(TAG, "Buffer 1 allocated: %zu bytes (%s)", buffer_size, 
             config->buffer.use_spiram ? "SPIRAM" : config->buffer.use_dma ? "DMA" : "Internal");

    void *buf2 = NULL;
    if (config->buffer.double_buffer) {
        buf2 = heap_caps_malloc(buffer_size, caps);
        if (buf2 == NULL) {
            ESP_LOGE(TAG, "Failed to allocate LVGL buffer 2");
            free(buf1);
            return NULL;
        }
        ESP_LOGI(TAG, "Buffer 2 allocated: %zu bytes (double buffering enabled)", buffer_size);
    }

    /* Create LVGL display configuration
     * For MIPI-DSI: io_handle can be NULL, only panel_handle matters */
    const lvgl_port_display_cfg_t lvgl_disp_cfg = {
        .io_handle = NULL,                /* Not used for MIPI-DSI */
        .panel_handle = g_panel_handle,   /* Display handle from BSP */
        .buffer_size = buffer_size,
        .double_buffer = config->buffer.double_buffer,
        .hres = CONFIG_BSP_DISPLAY_WIDTH,
        .vres = CONFIG_BSP_DISPLAY_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = config->buffer.use_dma,
            .buff_spiram = config->buffer.use_spiram,
            .swap_bytes = false,
#ifdef CONFIG_BSP_LVGL_ENABLE_SW_ROTATE
            .sw_rotate = config->rotation.sw_rotate,
#else
            .sw_rotate = false,
#endif
        }
    };

    /* For MIPI-DSI displays, use special configuration */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = true,  /* Enable for smooth rendering */
        }
    };

    ESP_LOGI(TAG, "Creating LVGL display with DSI config...");
    lv_display_t *display = lvgl_port_add_disp_dsi(&lvgl_disp_cfg, &dsi_cfg);
    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        free(buf1);
        if (buf2) free(buf2);
        return NULL;
    }

#ifdef CONFIG_BSP_LVGL_ENABLE_SW_ROTATE
    /* Set initial rotation from Kconfig */
    if (config->rotation.sw_rotate) {
        lv_disp_rotation_t lv_rotation = LV_DISPLAY_ROTATION_0;
        switch (config->rotation.initial_rotation) {
            case 0:   lv_rotation = LV_DISPLAY_ROTATION_0; break;
            case 90:  lv_rotation = LV_DISPLAY_ROTATION_90; break;
            case 180: lv_rotation = LV_DISPLAY_ROTATION_180; break;
            case 270: lv_rotation = LV_DISPLAY_ROTATION_270; break;
        }
        lv_display_set_rotation(display, lv_rotation);
        ESP_LOGI(TAG, "Display rotation set to %d degrees", config->rotation.initial_rotation);
    }
#endif

    ESP_LOGI(TAG, "LVGL display initialized successfully");
    return display;
}

/**
 * @brief Initialize LVGL touch input
 */
static lv_indev_t *bsp_lvgl_init_touch(lv_display_t *display)
{
#ifdef CONFIG_BSP_LVGL_TOUCH_ENABLE
    #ifndef CONFIG_BSP_ENABLE_TOUCH
        ESP_LOGW(TAG, "Touch is disabled in BSP configuration");
        return NULL;
    #endif

    ESP_LOGI(TAG, "Initializing LVGL touch input (GT911)");

    /* Touch already initialized by bsp_board_init() - just retrieve handle */
    if (g_touch_handle == NULL) {
        ESP_LOGE(TAG, "Touch not initialized!");
        return NULL;
    }
    ESP_LOGI(TAG, "Using existing touch handle");

    /* Add touch to LVGL */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display,
        .handle = g_touch_handle,
    };

    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    if (indev == NULL) {
        ESP_LOGE(TAG, "Failed to add touch to LVGL");
        return NULL;
    }

    ESP_LOGI(TAG, "LVGL touch initialized successfully");
    return indev;
#else
    ESP_LOGI(TAG, "Touch input disabled (CONFIG_BSP_LVGL_TOUCH_ENABLE not set)");
    return NULL;
#endif
}

lv_display_t *bsp_lvgl_init(const bsp_lvgl_config_t *config)
{
    if (lvgl_initialized) {
        ESP_LOGW(TAG, "LVGL already initialized");
        return lvgl_display;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "NULL config passed to bsp_lvgl_init");
        return NULL;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing LVGL v%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ESP_LOGI(TAG, "========================================");

    /* Initialize LVGL port */
    esp_err_t ret = lvgl_port_init(&config->lvgl_port_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port initialization failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    /* Initialize display */
    lvgl_display = bsp_lvgl_init_display(config);
    if (lvgl_display == NULL) {
        ESP_LOGE(TAG, "Display initialization failed");
        lvgl_port_deinit();
        return NULL;
    }

    /* Initialize touch input */
    lvgl_touch_indev = bsp_lvgl_init_touch(lvgl_display);

    lvgl_initialized = true;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "LVGL initialization complete!");
    bsp_lvgl_print_stats();
    ESP_LOGI(TAG, "========================================");

    return lvgl_display;
}

lv_display_t *bsp_lvgl_init_default(void)
{
    bsp_lvgl_config_t config = BSP_LVGL_CONFIG_DEFAULT();
    return bsp_lvgl_init(&config);
}

esp_err_t bsp_lvgl_deinit(void)
{
    if (!lvgl_initialized) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deinitializing LVGL");

    /* LVGL port will handle cleanup of display and touch */
    lvgl_port_deinit();

    lvgl_display = NULL;
    lvgl_touch_indev = NULL;
    lvgl_initialized = false;

    return ESP_OK;
}

lv_display_t *bsp_lvgl_get_display(void)
{
    return lvgl_display;
}

lv_indev_t *bsp_lvgl_get_touch_input(void)
{
    return lvgl_touch_indev;
}

esp_err_t bsp_lvgl_set_rotation(bsp_lvgl_rotation_t rotation)
{
#ifdef CONFIG_BSP_LVGL_ENABLE_SW_ROTATE
    if (!lvgl_initialized || lvgl_display == NULL) {
        return ESP_FAIL;
    }

    if (rotation != BSP_LVGL_ROTATION_0 && 
        rotation != BSP_LVGL_ROTATION_90 && 
        rotation != BSP_LVGL_ROTATION_180 && 
        rotation != BSP_LVGL_ROTATION_270) {
        return ESP_ERR_INVALID_ARG;
    }

    if (bsp_lvgl_lock(-1)) {
        lv_display_set_rotation(lvgl_display, (lv_disp_rotation_t)rotation);
        bsp_lvgl_unlock();
        ESP_LOGI(TAG, "Display rotation changed to %d degrees", 
                 rotation == BSP_LVGL_ROTATION_0 ? 0 :
                 rotation == BSP_LVGL_ROTATION_90 ? 90 :
                 rotation == BSP_LVGL_ROTATION_180 ? 180 : 270);
        return ESP_OK;
    }

    return ESP_FAIL;
#else
    ESP_LOGW(TAG, "Software rotation disabled (CONFIG_BSP_LVGL_ENABLE_SW_ROTATE not set)");
    return ESP_FAIL;
#endif
}

int bsp_lvgl_get_rotation(void)
{
    if (!lvgl_initialized || lvgl_display == NULL) {
        return 0;
    }

    lv_disp_rotation_t lv_rot = lv_display_get_rotation(lvgl_display);
    
    switch (lv_rot) {
        case LV_DISPLAY_ROTATION_0:   return 0;
        case LV_DISPLAY_ROTATION_90:  return 90;
        case LV_DISPLAY_ROTATION_180: return 180;
        case LV_DISPLAY_ROTATION_270: return 270;
        default: return 0;
    }
}

bool bsp_lvgl_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_lvgl_unlock(void)
{
    lvgl_port_unlock();
}

size_t bsp_lvgl_get_buffer_size(void)
{
    size_t single_buffer = LVGL_BUFFER_SIZE * sizeof(lv_color_t);
    
#ifdef CONFIG_BSP_LVGL_DOUBLE_BUFFER
    return single_buffer * 2;
#else
    return single_buffer;
#endif
}

void bsp_lvgl_print_stats(void)
{
    ESP_LOGI(TAG, "LVGL Configuration:");
    ESP_LOGI(TAG, "  Display: %dx%d", CONFIG_BSP_DISPLAY_WIDTH, CONFIG_BSP_DISPLAY_HEIGHT);
    ESP_LOGI(TAG, "  Buffer lines: %d", CONFIG_BSP_LVGL_BUFFER_LINES);
    ESP_LOGI(TAG, "  Buffer size: %zu KB (single)", LVGL_BUFFER_SIZE * sizeof(lv_color_t) / 1024);
    
#ifdef CONFIG_BSP_LVGL_DOUBLE_BUFFER
    ESP_LOGI(TAG, "  Double buffer: Yes");
#else
    ESP_LOGI(TAG, "  Double buffer: No");
#endif

    ESP_LOGI(TAG, "  Total memory: %zu KB", bsp_lvgl_get_buffer_size() / 1024);
    
#ifdef CONFIG_BSP_LVGL_USE_SPIRAM_BUFFER
    ESP_LOGI(TAG, "  Buffer location: SPIRAM");
#elif defined(CONFIG_BSP_LVGL_USE_DMA_BUFFER)
    ESP_LOGI(TAG, "  Buffer location: DMA");
#else
    ESP_LOGI(TAG, "  Buffer location: Internal");
#endif
    
#ifdef CONFIG_BSP_LVGL_ENABLE_SW_ROTATE
    ESP_LOGI(TAG, "  Rotation: %d degrees (SW rotate enabled)", bsp_lvgl_get_rotation());
#else
    ESP_LOGI(TAG, "  Rotation: Disabled");
#endif

#ifdef CONFIG_BSP_LVGL_TOUCH_ENABLE
    ESP_LOGI(TAG, "  Touch: %s", lvgl_touch_indev ? "Enabled" : "Disabled");
#else
    ESP_LOGI(TAG, "  Touch: Disabled");
#endif

    /* Memory statistics */
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "LVGL Memory Usage:");
    ESP_LOGI(TAG, "  Used: %u KB / %u KB", mon.used_cnt / 1024, mon.total_size / 1024);
    ESP_LOGI(TAG, "  Free: %u KB", mon.free_size / 1024);
    ESP_LOGI(TAG, "  Fragmentation: %u%%", mon.frag_pct);
}

#endif /* CONFIG_BSP_ENABLE_LVGL */
