#include "sdkconfig.h"

#include "lvgl_dashboard.h"

#include <string.h>

#include "bsp_board.h"
#include "bsp_sensors.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"

#ifdef CONFIG_BSP_ENABLE_CAMERA
#include "drivers/ov02c10_wrapper.h"
#endif

#ifdef CONFIG_BSP_ENABLE_LVGL

#define COLOR_BG_DARK         0x070c14
#define COLOR_BG_CARD         0x13213a
#define COLOR_BG_HEADER       0x0f2b4d
#define COLOR_ACCENT          0x19e2ff
#define COLOR_TEXT_PRIMARY    0xffffff
#define COLOR_TEXT_SECONDARY  0xb8c2d4
#define COLOR_STATUS_OK       0x00ff88
#define COLOR_STATUS_WARN     0xffaa00
#define COLOR_STATUS_ERROR    0xff5555
#define COLOR_STATUS_OFF      0x444444

#define DASHBOARD_WIDTH       1024
#define DASHBOARD_HEIGHT      600

#define PERIPH_CARD_W         240
#define PERIPH_CARD_H         116
#define PERIPH_CARD_GAP       8

#define TOOL_CARD_W           322
#define TOOL_CARD_H           116
#define TOOL_CARD_GAP         9

#define MAX_DASH_SCREENS      3
#define DEBUG_TOOLS_PER_PAGE  9
#define ENV_HISTORY_POINTS    60

#define HEADER_H              70
#define SUBTITLE_Y            80
#define CARDS_Y               105
#define CARDS_H               385
#define INDICATOR_Y           490
#define INDICATOR_H           70
#define FOOTER_Y              560
#define FOOTER_H              40

typedef struct {
    const char *name;
    const char *description;
    const char *detail;
    const char *icon_symbol;
    bool enabled_in_config;
    bool implemented;
    periph_status_t status;
    lv_obj_t *card;
    lv_obj_t *status_led;
    lv_obj_t *status_label;
    lv_obj_t *description_label;
    lv_obj_t *detail_label;
} peripheral_info_t;

typedef struct {
    const char *name;
    const char *description;
    const char *detail;
    const char *icon_symbol;
    debug_tool_t tool_id;
} tool_info_t;

typedef struct {
    bool initialized;
    uint8_t active_screen;
    dashboard_config_t config;

    lv_obj_t *tileview;
    lv_obj_t *tile_peripherals;
    lv_obj_t *tile_debugtools;
    lv_obj_t *tile_debugtools_overflow;

    lv_obj_t *header_info[MAX_DASH_SCREENS];
    lv_obj_t *indicator_dot[MAX_DASH_SCREENS][MAX_DASH_SCREENS];
    lv_obj_t *swipe_hint[MAX_DASH_SCREENS];

    lv_timer_t *refresh_timer;
    lv_timer_t *env_timer;

    peripheral_info_t peripherals[12];
    uint8_t periph_count;

    /* Overlay is diagnostic/read-only: no HW config writes from UI. */
    lv_obj_t *overlay;
    peripheral_info_t *overlay_periph;
    debug_tool_t overlay_tool;
    lv_obj_t *camera_canvas;
    lv_obj_t *camera_status_label;
    lv_obj_t *camera_ctrl_panel;
    lv_obj_t *camera_gain_slider;
    lv_obj_t *camera_gain_label;
    lv_obj_t *camera_exposure_slider;
    lv_obj_t *camera_exposure_label;

    peripheral_info_t *env_peripheral;
    lv_obj_t *sensor_tool_detail_label;
    lv_obj_t *sensor_tool_chart;
    lv_chart_series_t *sensor_tool_temp_series;
    lv_chart_series_t *sensor_tool_hum_series;
    lv_chart_series_t *sensor_tool_press_series;
} dashboard_state_t;

static const char *TAG = "lvgl_dashboard";
static dashboard_state_t s_dash = {0};

#ifdef CONFIG_BSP_ENABLE_CAMERA
static void dashboard_camera_preview_frame_ready(void *user_data);
static void camera_gain_slider_event_cb(lv_event_t *e);
static void camera_exposure_slider_event_cb(lv_event_t *e);
#endif
static void update_env_card_widgets(void);

static const tool_info_t s_tools[] = {
    {
        .name = "Serial Log Monitor",
        .description = "ESP-IDF log viewer",
        .detail = "Filter, export, pause",
        .icon_symbol = LV_SYMBOL_LIST,
        .tool_id = DEBUG_TOOL_LOG_MONITOR,
    },
    {
        .name = "Camera Test",
        .description = "Live preview + capture",
        .detail = "MIPI CSI interface",
        .icon_symbol = LV_SYMBOL_EYE_OPEN,
        .tool_id = DEBUG_TOOL_CAMERA_TEST,
    },
    {
        .name = "Sensor Monitor",
        .description = "Real-time data graphs",
        .detail = "Temp, humidity, pressure",
        .icon_symbol = LV_SYMBOL_SETTINGS,
        .tool_id = DEBUG_TOOL_SENSOR_MONITOR,
    },
    {
        .name = "WiFi Scanner",
        .description = "Network discovery",
        .detail = "RSSI, channel, security",
        .icon_symbol = LV_SYMBOL_WIFI,
        .tool_id = DEBUG_TOOL_WIFI_SCANNER,
    },
    {
        .name = "SD Card Browser",
        .description = "File manager",
        .detail = "Read, write, delete",
        .icon_symbol = LV_SYMBOL_SD_CARD,
        .tool_id = DEBUG_TOOL_SD_BROWSER,
    },
    {
        .name = "I2C Bus Scanner",
        .description = "Device detection",
        .detail = "Address 0x00-0x7F",
        .icon_symbol = LV_SYMBOL_SHUFFLE,
        .tool_id = DEBUG_TOOL_I2C_SCANNER,
    },
    {
        .name = "System Info",
        .description = "CPU, memory, tasks",
        .detail = "Heap, stack, uptime",
        .icon_symbol = LV_SYMBOL_SETTINGS,
        .tool_id = DEBUG_TOOL_SYSTEM_INFO,
    },
    {
        .name = "GPIO Monitor",
        .description = "Pin state viewer",
        .detail = "Input/output levels",
        .icon_symbol = LV_SYMBOL_CHARGE,
        .tool_id = DEBUG_TOOL_GPIO_MONITOR,
    },
    {
        .name = "Performance",
        .description = "FPS, latency, profiling",
        .detail = "LVGL render stats",
        .icon_symbol = LV_SYMBOL_LOOP,
        .tool_id = DEBUG_TOOL_PERFORMANCE,
    },
};

static lv_color_t status_to_color(periph_status_t status)
{
    switch (status) {
        case PERIPH_STATUS_OK:
            return lv_color_hex(COLOR_STATUS_OK);
        case PERIPH_STATUS_WARNING:
            return lv_color_hex(COLOR_STATUS_WARN);
        case PERIPH_STATUS_ERROR:
            return lv_color_hex(COLOR_STATUS_ERROR);
        case PERIPH_STATUS_DISABLED:
        case PERIPH_STATUS_NOT_IMPL:
        default:
            return lv_color_hex(COLOR_STATUS_OFF);
    }
}

static const char *status_to_text(periph_status_t status)
{
    switch (status) {
        case PERIPH_STATUS_OK:
            return "OK";
        case PERIPH_STATUS_WARNING:
            return "WARN";
        case PERIPH_STATUS_ERROR:
            return "ERR";
        case PERIPH_STATUS_DISABLED:
            return "OFF";
        case PERIPH_STATUS_NOT_IMPL:
        default:
            return "N/A";
    }
}

static void set_peripheral_status(peripheral_info_t *periph)
{
    esp_lcd_panel_handle_t display;
    esp_lcd_touch_handle_t touch;
    i2c_master_bus_handle_t i2c;
    bsp_env_data_t env_data = {0};

    if (!periph->implemented) {
        periph->status = PERIPH_STATUS_NOT_IMPL;
        return;
    }

    if (!periph->enabled_in_config) {
        periph->status = PERIPH_STATUS_DISABLED;
        return;
    }

    if (strcmp(periph->name, "Display") == 0) {
        display = bsp_display_get_handle();
        periph->status = (display != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
        return;
    }

    if (strcmp(periph->name, "Touch") == 0) {
        touch = bsp_touch_get_handle();
        periph->status = (touch != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
        return;
    }

    if (strcmp(periph->name, "I2C Bus") == 0) {
        i2c = bsp_i2c_get_bus_handle();
        periph->status = (i2c != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
        return;
    }

    if (strcmp(periph->name, "SD Card") == 0) {
        periph->status = PERIPH_STATUS_WARNING;
        return;
    }

    if (strcmp(periph->name, "Sensors") == 0) {
        periph->status = (bsp_env_get_data(&env_data) == ESP_OK && env_data.has_temperature)
                            ? PERIPH_STATUS_OK
                            : PERIPH_STATUS_WARNING;
        return;
    }

    periph->status = PERIPH_STATUS_OK;
}

static void init_peripheral_list(void)
{
    s_dash.periph_count = 0;

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Display",
        .description = "JD9165 1024x600 MIPI-DSI",
        .detail = "2-lane, 60Hz",
        .icon_symbol = LV_SYMBOL_IMAGE,
#ifdef CONFIG_BSP_ENABLE_DISPLAY
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Touch",
        .description = "GT911 5-point capacitive",
        .detail = "I2C 0x14",
        .icon_symbol = LV_SYMBOL_EDIT,
#ifdef CONFIG_BSP_ENABLE_TOUCH
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "I2C Bus",
        .description = "400kHz master",
        .detail = "Touch+Audio+RTC",
        .icon_symbol = LV_SYMBOL_SHUFFLE,
#ifdef CONFIG_BSP_ENABLE_I2C
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Audio",
        .description = "ES8311 + NS4150 PA",
        .detail = "I2S codec",
        .icon_symbol = LV_SYMBOL_VOLUME_MAX,
#ifdef CONFIG_BSP_ENABLE_AUDIO
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "RTC",
        .description = "RX8025T w/ battery",
        .detail = "I2C 0x32",
        .icon_symbol = LV_SYMBOL_BELL,
#ifdef CONFIG_BSP_ENABLE_RTC
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "SD Card",
        .description = "SDIO 4-bit mode",
        .detail = "Experimental",
        .icon_symbol = LV_SYMBOL_SD_CARD,
#ifdef CONFIG_BSP_ENABLE_SDCARD
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "WiFi",
        .description = "ESP32-C6 ESP-Hosted",
        .detail = "SDIO Slot 1",
        .icon_symbol = LV_SYMBOL_WIFI,
#ifdef CONFIG_BSP_ENABLE_WIFI
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Camera",
        .description = "MIPI CSI interface",
        .detail = "Not impl.",
        .icon_symbol = LV_SYMBOL_EYE_OPEN,
        .enabled_in_config = false,
        .implemented = false,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Sensors",
        .description = "Temp/Humidity/Pressure",
        .detail = "AHT20 0x38 + BMP280 0x77",
        .icon_symbol = LV_SYMBOL_SETTINGS,
        .enabled_in_config = true,
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "GPIO Exp",
        .description = "I2C I/O expander",
        .detail = "Future",
        .icon_symbol = LV_SYMBOL_LIST,
        .enabled_in_config = false,
        .implemented = false,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "SPI Flash",
        .description = "External storage",
        .detail = "Future",
        .icon_symbol = LV_SYMBOL_USB,
        .enabled_in_config = false,
        .implemented = false,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "UART Ext",
        .description = "Serial communication",
        .detail = "Future",
        .icon_symbol = LV_SYMBOL_CALL,
        .enabled_in_config = false,
        .implemented = false,
    };
}

static void update_peripheral_status(void)
{
    for (uint8_t i = 0; i < s_dash.periph_count; i++) {
        set_peripheral_status(&s_dash.peripherals[i]);
    }
}

static void update_header_info_labels(void)
{
    char info_text[96];
    uint32_t free_heap_kb = esp_get_free_heap_size() / 1024U;
    uint32_t free_psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U;
    uint32_t uptime_sec = esp_log_timestamp() / 1000U;

    snprintf(info_text, sizeof(info_text), "Heap: %luKB | PSRAM: %luKB | Up: %02u:%02u",
             (unsigned long)free_heap_kb,
             (unsigned long)free_psram_kb,
             (unsigned int)(uptime_sec / 60U),
             (unsigned int)(uptime_sec % 60U));

    for (uint8_t i = 0; i < MAX_DASH_SCREENS; i++) {
        if (s_dash.header_info[i]) {
            lv_label_set_text(s_dash.header_info[i], info_text);
        }
    }
}

static void apply_peripheral_status_to_ui(void)
{
    for (uint8_t i = 0; i < s_dash.periph_count; i++) {
        peripheral_info_t *periph = &s_dash.peripherals[i];
        lv_opa_t opacity;
        bool is_disabled;

        if (!periph->status_led || !periph->status_label || !periph->card) {
            continue;
        }

        is_disabled = (periph->status == PERIPH_STATUS_DISABLED ||
                       periph->status == PERIPH_STATUS_NOT_IMPL);

        lv_obj_set_style_bg_color(periph->status_led, status_to_color(periph->status), 0);
        lv_label_set_text(periph->status_label, status_to_text(periph->status));

        opacity = is_disabled ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_opa(periph->card, opacity, 0);

        if (is_disabled) {
            lv_obj_remove_flag(periph->card, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_flag(periph->card, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static void update_page_indicators(void)
{
    uint8_t screen_count = s_dash.tile_debugtools_overflow ? 3 : 2;

    for (uint8_t screen = 0; screen < MAX_DASH_SCREENS; screen++) {
        for (uint8_t dot = 0; dot < MAX_DASH_SCREENS; dot++) {
            if (s_dash.indicator_dot[screen][dot]) {
                if (dot >= screen_count) {
                    lv_obj_set_style_bg_opa(s_dash.indicator_dot[screen][dot], LV_OPA_TRANSP, 0);
                } else {
                    lv_obj_set_style_bg_opa(s_dash.indicator_dot[screen][dot], LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_color(
                        s_dash.indicator_dot[screen][dot],
                        s_dash.active_screen == dot ? lv_color_hex(COLOR_ACCENT) : lv_color_hex(COLOR_STATUS_OFF),
                        0);
                }
            }
        }
    }

    if (s_dash.swipe_hint[0]) {
        lv_label_set_text(s_dash.swipe_hint[0], "Swipe left for Debug Tools ->");
    }
    if (s_dash.swipe_hint[1]) {
        if (screen_count == 3) {
            lv_label_set_text(s_dash.swipe_hint[1], "<- Swipe for More Tools ->");
        } else {
            lv_label_set_text(s_dash.swipe_hint[1], "<- Swipe right to return");
        }
    }
    if (s_dash.swipe_hint[2]) {
        lv_label_set_text(s_dash.swipe_hint[2], "<- Swipe right for Tools");
    }
}

static void refresh_status_internal(void)
{
    update_peripheral_status();
    update_header_info_labels();
    apply_peripheral_status_to_ui();
    update_page_indicators();
    update_env_card_widgets();
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status_internal();
}

static void update_env_card_widgets(void)
{
    bsp_env_data_t env_data = {0};
    char text[96];
    char hum_buf[16];
    char press_buf[16];
    const char *src_tag = "";

    if (bsp_env_get_data(&env_data) != ESP_OK) {
        if (s_dash.env_peripheral && s_dash.env_peripheral->detail_label) {
            lv_label_set_text(s_dash.env_peripheral->detail_label, "No sensor data");
        }
        if (s_dash.sensor_tool_detail_label) {
            lv_label_set_text(s_dash.sensor_tool_detail_label, "Waiting sensors...");
        }
        return;
    }

    src_tag = env_data.temperature_from_internal ? " [Internal]" : "";

    if (env_data.has_humidity) {
        snprintf(hum_buf, sizeof(hum_buf), "%.1f%%", (double)env_data.humidity_pct);
    } else {
        snprintf(hum_buf, sizeof(hum_buf), "n/a");
    }

    if (env_data.has_pressure) {
        snprintf(press_buf, sizeof(press_buf), "%.1fhPa", (double)env_data.pressure_hpa);
    } else {
        snprintf(press_buf, sizeof(press_buf), "n/a");
    }

    snprintf(text,
             sizeof(text),
             "T %.1fC%s | H %s | P %s",
             (double)env_data.temperature_c,
             src_tag,
             hum_buf,
             press_buf);

    if (s_dash.env_peripheral && s_dash.env_peripheral->detail_label) {
        lv_label_set_text(s_dash.env_peripheral->detail_label, text);
    }
    if (s_dash.sensor_tool_detail_label) {
        lv_label_set_text(s_dash.sensor_tool_detail_label, text);
    }

    if (s_dash.sensor_tool_chart &&
        s_dash.sensor_tool_temp_series &&
        s_dash.sensor_tool_hum_series &&
        s_dash.sensor_tool_press_series) {
        lv_chart_set_next_value(s_dash.sensor_tool_chart,
                                s_dash.sensor_tool_temp_series,
                                (int32_t)(env_data.temperature_c * 10.0f));
        lv_chart_set_next_value(s_dash.sensor_tool_chart,
                                s_dash.sensor_tool_hum_series,
                                (int32_t)((env_data.has_humidity ? env_data.humidity_pct : 0.0f) * 10.0f));
        lv_chart_set_next_value(s_dash.sensor_tool_chart,
                                s_dash.sensor_tool_press_series,
                                (int32_t)(env_data.has_pressure ? env_data.pressure_hpa : 0.0f));
        lv_chart_refresh(s_dash.sensor_tool_chart);
    }
}

static void env_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_env_card_widgets();
}

static lv_color_t overlay_status_badge_color(periph_status_t status)
{
    switch (status) {
        case PERIPH_STATUS_OK:
            return lv_color_hex(0x4CAF50);
        case PERIPH_STATUS_WARNING:
            return lv_color_hex(0xFF9800);
        case PERIPH_STATUS_ERROR:
            return lv_color_hex(0xF44336);
        case PERIPH_STATUS_DISABLED:
        case PERIPH_STATUS_NOT_IMPL:
        default:
            return lv_color_hex(0x9E9E9E);
    }
}

static void overlay_back_event_cb(lv_event_t *e);

static void get_peripheral_gpio_text(const peripheral_info_t *periph, char *buf, size_t buf_size)
{
    if (strcmp(periph->name, "Display") == 0) {
        snprintf(buf,
                 buf_size,
                 "GPIO %d (I2C_SCL)\n"
                 "GPIO %d (I2C_SDA)\n"
                 "GPIO %d (BACKLIGHT)",
                 CONFIG_BSP_I2C_SCL_GPIO,
                 CONFIG_BSP_I2C_SDA_GPIO,
                 CONFIG_BSP_PIN_LCD_BL);
        return;
    }

    if (strcmp(periph->name, "Touch") == 0) {
        snprintf(buf,
                 buf_size,
                 "GPIO %d (I2C_SCL)\n"
                 "GPIO %d (I2C_SDA)\n"
                 "GPIO %d (TOUCH_RST)\n"
                 "GPIO %d (TOUCH_INT)",
                 CONFIG_BSP_I2C_SCL_GPIO,
                 CONFIG_BSP_I2C_SDA_GPIO,
                 CONFIG_BSP_PIN_TOUCH_RST,
                 CONFIG_BSP_PIN_TOUCH_INT);
        return;
    }

    if (strcmp(periph->name, "I2C Bus") == 0) {
        snprintf(buf,
                 buf_size,
                 "GPIO %d (I2C_SCL)\n"
                 "GPIO %d (I2C_SDA)",
                 CONFIG_BSP_I2C_SCL_GPIO,
                 CONFIG_BSP_I2C_SDA_GPIO);
        return;
    }

    if (strcmp(periph->name, "Audio") == 0) {
        snprintf(buf,
                 buf_size,
                 "GPIO %d (I2S_MCLK)\n"
                 "GPIO %d (I2S_BCLK)\n"
                 "GPIO %d (I2S_WS)\n"
                 "GPIO %d (I2S_DOUT)\n"
                 "GPIO %d (PA_ENABLE)",
                 CONFIG_BSP_PIN_I2S_MCLK,
                 CONFIG_BSP_PIN_I2S_BCLK,
                 CONFIG_BSP_PIN_I2S_WS,
                 CONFIG_BSP_PIN_I2S_DOUT,
                 CONFIG_BSP_PIN_PA_ENABLE);
        return;
    }

    if (strcmp(periph->name, "RTC") == 0) {
        snprintf(buf,
                 buf_size,
                 "GPIO %d (I2C_SCL)\n"
                 "GPIO %d (I2C_SDA)\n"
                 "GPIO %d (RTC_INT)",
                 CONFIG_BSP_I2C_SCL_GPIO,
                 CONFIG_BSP_I2C_SDA_GPIO,
                 CONFIG_BSP_PIN_RTC_INT);
        return;
    }

    if (strcmp(periph->name, "SD Card") == 0) {
        snprintf(buf,
                 buf_size,
                 "GPIO %d (SD_CMD)\n"
                 "GPIO %d (SD_CLK)\n"
                 "GPIO %d (SD_D0)\n"
                 "GPIO %d (SD_POWER_EN)",
                 CONFIG_BSP_PIN_CMD,
                 CONFIG_BSP_PIN_CLK,
                 CONFIG_BSP_PIN_D0,
                 CONFIG_BSP_PIN_SD_POWER_EN);
        return;
    }

    if (strcmp(periph->name, "WiFi") == 0) {
        snprintf(buf,
                 buf_size,
                 "GPIO %d (WIFI_RESET)\n"
                 "GPIO %d (WIFI_DATA_READY)\n"
                 "GPIO %d (WIFI_HANDSHAKE)\n"
                 "GPIO %d (SDIO_CLK)\n"
                 "GPIO %d (SDIO_CMD)\n"
                 "GPIO %d (SDIO_D0-D3)",
                 CONFIG_BSP_PIN_WIFI_RESET,
                 CONFIG_BSP_PIN_WIFI_DATA_READY,
                 CONFIG_BSP_PIN_WIFI_HANDSHAKE,
                 CONFIG_BSP_PIN_WIFI_SDIO_CLK,
                 CONFIG_BSP_PIN_WIFI_SDIO_CMD,
                 CONFIG_BSP_PIN_WIFI_SDIO_D0);
        return;
    }

    snprintf(buf, buf_size, "GPIO mapping not available for this peripheral.");
}

static void on_debug_tool_run_clicked(lv_event_t *e)
{
    const tool_info_t *tool = (const tool_info_t *)lv_event_get_user_data(e);

    if (lv_event_get_code(e) != LV_EVENT_CLICKED || tool == NULL) {
        return;
    }

    ESP_LOGI(TAG, "User clicked 'Run Tool' for: %s", tool->name);

    if (s_dash.config.tool_callback) {
        s_dash.config.tool_callback(tool->tool_id, s_dash.config.user_data);
    }
}

static void on_debug_tool_logs_clicked(lv_event_t *e)
{
    const tool_info_t *tool = (const tool_info_t *)lv_event_get_user_data(e);

    if (lv_event_get_code(e) != LV_EVENT_CLICKED || tool == NULL) {
        return;
    }

    ESP_LOGI(TAG, "User clicked 'View Logs' for: %s", tool->name);
}

#ifdef CONFIG_BSP_ENABLE_CAMERA
static void dashboard_camera_preview_frame_ready(void *user_data)
{
    const uint8_t *preview_buf;

    (void)user_data;

    if (!s_dash.camera_canvas) {
        return;
    }

    if (!lvgl_port_lock(5)) {
        return;
    }

    preview_buf = bsp_camera_get_preview_buffer();
    if (preview_buf) {
        lv_canvas_set_buffer(s_dash.camera_canvas,
                             (void *)preview_buf,
                             (int32_t)bsp_camera_get_preview_width(),
                             (int32_t)bsp_camera_get_preview_height(),
                             LV_COLOR_FORMAT_RGB565);
    }

    lv_obj_invalidate(s_dash.camera_canvas);
    lvgl_port_unlock();
}

static void camera_gain_slider_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    int32_t gain = lv_slider_get_value(lv_event_get_target_obj(e));
    esp_err_t ret = bsp_camera_set_gain_index((uint32_t)gain);
    if (ret == ESP_OK) {
        if (s_dash.camera_gain_label) {
            lv_label_set_text_fmt(s_dash.camera_gain_label, "Gain: %ld", (long)gain);
        }
    } else if (s_dash.camera_status_label) {
        lv_label_set_text_fmt(s_dash.camera_status_label, "Gain set failed: %s", esp_err_to_name(ret));
    }
}

static void camera_exposure_slider_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    int32_t exposure = lv_slider_get_value(lv_event_get_target_obj(e));
    esp_err_t ret = bsp_camera_set_exposure_value((uint32_t)exposure);
    if (ret == ESP_OK) {
        if (s_dash.camera_exposure_label) {
            lv_label_set_text_fmt(s_dash.camera_exposure_label, "Exposure: 0x%04lX", (unsigned long)exposure);
        }
    } else if (s_dash.camera_status_label) {
        lv_label_set_text_fmt(s_dash.camera_status_label, "Exposure set failed: %s", esp_err_to_name(ret));
    }
}

static void show_camera_tool_overlay(const tool_info_t *tool)
{
    lv_obj_t *header;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    lv_obj_t *title;
    lv_obj_t *gain_slider;
    lv_obj_t *exp_slider;
    lv_obj_t *gain_title;
    lv_obj_t *exp_title;
    esp_err_t ret;
    const uint8_t *preview_buf;
    uint32_t gain_min = 0;
    uint32_t gain_max = 255;
    uint32_t gain_default = 0;
    uint32_t gain_cur = 0;
    uint32_t exp_min = 0x0100;
    uint32_t exp_max = 0x0FFF;
    uint32_t exp_default = 0;
    uint32_t exp_cur = 0;

    if (tool == NULL) {
        return;
    }

    if (s_dash.overlay) {
#ifdef CONFIG_BSP_ENABLE_CAMERA
        if (s_dash.overlay_tool == DEBUG_TOOL_CAMERA_TEST) {
            bsp_camera_stop_preview();
        }
#endif
        lv_obj_del(s_dash.overlay);
        s_dash.overlay = NULL;
        s_dash.overlay_periph = NULL;
        s_dash.camera_canvas = NULL;
        s_dash.camera_status_label = NULL;
        s_dash.camera_ctrl_panel = NULL;
        s_dash.camera_gain_slider = NULL;
        s_dash.camera_gain_label = NULL;
        s_dash.camera_exposure_slider = NULL;
        s_dash.camera_exposure_label = NULL;
    }

    s_dash.overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_dash.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_dash.overlay, lv_color_hex(0x03080f), 0);
    lv_obj_set_style_border_width(s_dash.overlay, 0, 0);
    lv_obj_set_style_radius(s_dash.overlay, 0, 0);
    lv_obj_set_style_pad_all(s_dash.overlay, 0, 0);
    lv_obj_move_foreground(s_dash.overlay);

    s_dash.overlay_tool = DEBUG_TOOL_CAMERA_TEST;

    header = lv_obj_create(s_dash.overlay);
    lv_obj_set_size(header, LV_PCT(100), 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_BG_HEADER), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_80, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    back_btn = lv_button_create(header);
    lv_obj_set_size(back_btn, 120, 44);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a3e66), 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_add_event_cb(back_btn, overlay_back_event_cb, LV_EVENT_ALL, NULL);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(back_label);

    title = lv_label_create(header);
    lv_label_set_text_fmt(title, "%s", tool->name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 50, 0);

    s_dash.camera_status_label = lv_label_create(header);
    lv_label_set_text(s_dash.camera_status_label, "Starting camera preview...");
    lv_obj_set_style_text_font(s_dash.camera_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_dash.camera_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(s_dash.camera_status_label, LV_ALIGN_RIGHT_MID, -12, 0);

    ret = bsp_camera_start_preview(dashboard_camera_preview_frame_ready, NULL);
    if (ret != ESP_OK) {
        lv_label_set_text_fmt(s_dash.camera_status_label, "Preview start failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Camera preview start failed: %s", esp_err_to_name(ret));
        return;
    }

    preview_buf = bsp_camera_get_preview_buffer();
    if (!preview_buf || bsp_camera_get_preview_buffer_size() == 0) {
        lv_label_set_text(s_dash.camera_status_label, "Preview buffer unavailable");
        ESP_LOGE(TAG, "Camera preview buffer unavailable");
        bsp_camera_stop_preview();
        return;
    }

    s_dash.camera_canvas = lv_canvas_create(s_dash.overlay);
    lv_canvas_set_buffer(s_dash.camera_canvas,
                         (void *)preview_buf,
                         (int32_t)bsp_camera_get_preview_width(),
                         (int32_t)bsp_camera_get_preview_height(),
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_dash.camera_canvas, 0, 0);
    lv_obj_add_flag(s_dash.camera_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_dash.camera_canvas, overlay_back_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    (void)bsp_camera_get_gain_index_range(&gain_min, &gain_max, &gain_default);
    if (bsp_camera_get_gain_index(&gain_cur) != ESP_OK) {
        gain_cur = gain_default;
    }
    if (gain_cur < gain_min) {
        gain_cur = gain_min;
    }
    if (gain_cur > gain_max) {
        gain_cur = gain_max;
    }

    (void)bsp_camera_get_exposure_range(&exp_min, &exp_max, &exp_default);
    if (bsp_camera_get_exposure_value(&exp_cur) != ESP_OK) {
        exp_cur = exp_default;
    }
    if (exp_cur < exp_min) {
        exp_cur = exp_min;
    }
    if (exp_cur > exp_max) {
        exp_cur = exp_max;
    }

    s_dash.camera_ctrl_panel = lv_obj_create(s_dash.overlay);
    lv_obj_set_size(s_dash.camera_ctrl_panel, 430, 120);
    lv_obj_align(s_dash.camera_ctrl_panel, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(s_dash.camera_ctrl_panel, lv_color_hex(0x0b1729), 0);
    lv_obj_set_style_bg_opa(s_dash.camera_ctrl_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_color(s_dash.camera_ctrl_panel, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(s_dash.camera_ctrl_panel, 1, 0);
    lv_obj_set_style_radius(s_dash.camera_ctrl_panel, 10, 0);
    lv_obj_set_style_pad_all(s_dash.camera_ctrl_panel, 8, 0);

    gain_title = lv_label_create(s_dash.camera_ctrl_panel);
    lv_label_set_text(gain_title, "Gain");
    lv_obj_set_style_text_font(gain_title, &lv_font_montserrat_12, 0);
    lv_obj_align(gain_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_dash.camera_gain_label = lv_label_create(s_dash.camera_ctrl_panel);
    lv_label_set_text_fmt(s_dash.camera_gain_label, "Gain: %lu", (unsigned long)gain_cur);
    lv_obj_set_style_text_font(s_dash.camera_gain_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_dash.camera_gain_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    gain_slider = lv_slider_create(s_dash.camera_ctrl_panel);
    s_dash.camera_gain_slider = gain_slider;
    lv_obj_set_width(gain_slider, 410);
    lv_obj_align(gain_slider, LV_ALIGN_TOP_MID, 0, 20);
    lv_slider_set_range(gain_slider, (int32_t)gain_min, (int32_t)gain_max);
    lv_slider_set_value(gain_slider, (int32_t)gain_cur, LV_ANIM_OFF);
    lv_obj_add_event_cb(gain_slider, camera_gain_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    exp_title = lv_label_create(s_dash.camera_ctrl_panel);
    lv_label_set_text(exp_title, "Exposure");
    lv_obj_set_style_text_font(exp_title, &lv_font_montserrat_12, 0);
    lv_obj_align(exp_title, LV_ALIGN_TOP_LEFT, 0, 56);

    s_dash.camera_exposure_label = lv_label_create(s_dash.camera_ctrl_panel);
    lv_label_set_text_fmt(s_dash.camera_exposure_label, "Exposure: 0x%04lX", (unsigned long)exp_cur);
    lv_obj_set_style_text_font(s_dash.camera_exposure_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_dash.camera_exposure_label, LV_ALIGN_TOP_RIGHT, 0, 56);

    exp_slider = lv_slider_create(s_dash.camera_ctrl_panel);
    s_dash.camera_exposure_slider = exp_slider;
    lv_obj_set_width(exp_slider, 410);
    lv_obj_align(exp_slider, LV_ALIGN_TOP_MID, 0, 76);
    lv_slider_set_range(exp_slider, (int32_t)exp_min, (int32_t)exp_max);
    lv_slider_set_value(exp_slider, (int32_t)exp_cur, LV_ANIM_OFF);
    lv_obj_add_event_cb(exp_slider, camera_exposure_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Keep header controls above full-screen canvas. */
    lv_obj_move_foreground(header);
    lv_obj_move_foreground(s_dash.camera_ctrl_panel);

    lv_label_set_text(s_dash.camera_status_label, "Preview running (press Back to exit)");
}
#endif

static void show_debug_tool_overlay(const tool_info_t *tool)
{
    lv_obj_t *header;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    lv_obj_t *title;
    lv_obj_t *content;
    lv_obj_t *desc;
    lv_obj_t *btn_run;
    lv_obj_t *btn_run_label;
    lv_obj_t *btn_logs;
    lv_obj_t *btn_logs_label;

    if (tool == NULL) {
        return;
    }

#ifdef CONFIG_BSP_ENABLE_CAMERA
    if (tool->tool_id == DEBUG_TOOL_CAMERA_TEST) {
        show_camera_tool_overlay(tool);
        return;
    }
#endif

    if (s_dash.overlay) {
#ifdef CONFIG_BSP_ENABLE_CAMERA
        if (s_dash.overlay_tool == DEBUG_TOOL_CAMERA_TEST) {
            bsp_camera_stop_preview();
        }
#endif
        lv_obj_del(s_dash.overlay);
        s_dash.overlay = NULL;
        s_dash.overlay_periph = NULL;
        s_dash.overlay_tool = DEBUG_TOOL_MAX;
        s_dash.camera_canvas = NULL;
        s_dash.camera_status_label = NULL;
    }

    s_dash.overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_dash.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_dash.overlay, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(s_dash.overlay, 0, 0);
    lv_obj_set_style_radius(s_dash.overlay, 0, 0);
    lv_obj_move_foreground(s_dash.overlay);
    s_dash.overlay_tool = tool->tool_id;

    header = lv_obj_create(s_dash.overlay);
    lv_obj_set_size(header, LV_PCT(100), 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_BG_HEADER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    back_btn = lv_button_create(header);
    lv_obj_set_size(back_btn, 120, 44);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a3e66), 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_add_event_cb(back_btn, overlay_back_event_cb, LV_EVENT_ALL, NULL);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(back_label);

    title = lv_label_create(header);
    lv_label_set_text_fmt(title, "%s", tool->name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 50, 0);

    content = lv_obj_create(s_dash.overlay);
    lv_obj_set_size(content, LV_PCT(94), 460);
    lv_obj_set_pos(content, 30, 92);
    lv_obj_set_style_bg_color(content, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_border_color(content, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(content, 2, 0);
    lv_obj_set_style_radius(content, 10, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 16, 0);

    desc = lv_label_create(content);
    lv_label_set_text_fmt(desc, "Description:\n%s\n\n%s", tool->description, tool->detail);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT_PRIMARY), 0);

    btn_run = lv_button_create(content);
    lv_obj_set_size(btn_run, 200, 50);
    lv_obj_set_style_bg_color(btn_run, lv_color_hex(0x4CAF50), 0);
    lv_obj_add_event_cb(btn_run, on_debug_tool_run_clicked, LV_EVENT_CLICKED, (void *)tool);

    btn_run_label = lv_label_create(btn_run);
    lv_label_set_text(btn_run_label, "Run Tool");
    lv_obj_center(btn_run_label);

    btn_logs = lv_button_create(content);
    lv_obj_set_size(btn_logs, 200, 50);
    lv_obj_set_style_bg_color(btn_logs, lv_color_hex(0x2196F3), 0);
    lv_obj_add_event_cb(btn_logs, on_debug_tool_logs_clicked, LV_EVENT_CLICKED, (void *)tool);

    btn_logs_label = lv_label_create(btn_logs);
    lv_label_set_text(btn_logs_label, "View Logs");
    lv_obj_center(btn_logs_label);
}

static void tool_card_event_cb(lv_event_t *e)
{
    debug_tool_t tool_id;
    const tool_info_t *tool;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    tool_id = (debug_tool_t)(uintptr_t)lv_event_get_user_data(e);
    if ((uint32_t)tool_id >= DEBUG_TOOL_MAX) {
        return;
    }

    tool = &s_tools[tool_id];
    ESP_LOGI(TAG, "Debug tool card clicked: %s", tool->name);
    show_debug_tool_overlay(tool);
}

static void overlay_back_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_event_code_t close_code_compat = (lv_event_code_t)8;
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_LONG_PRESSED &&
        code != close_code_compat) {
        return;
    }

    (void)lv_event_get_user_data(e);

    if (s_dash.overlay) {
        ESP_LOGI(TAG, "Closing overlay via back event (code=%d, tool=%d)", (int)code, (int)s_dash.overlay_tool);
#ifdef CONFIG_BSP_ENABLE_CAMERA
        if (s_dash.overlay_tool == DEBUG_TOOL_CAMERA_TEST) {
            bsp_camera_stop_preview();
        }
#endif
        lv_obj_del(s_dash.overlay);
        s_dash.overlay = NULL;
        s_dash.overlay_periph = NULL;
        s_dash.overlay_tool = DEBUG_TOOL_MAX;
        s_dash.camera_canvas = NULL;
        s_dash.camera_status_label = NULL;
    }
}

static void show_peripheral_overlay(peripheral_info_t *periph)
{
    lv_obj_t *header;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    lv_obj_t *title;
    lv_obj_t *content;
    lv_obj_t *status_row;
    lv_obj_t *status_dot;
    lv_obj_t *status_label;
    lv_obj_t *desc;
    lv_obj_t *pins_label;
    char pins_buf[320];

    if (s_dash.overlay) {
#ifdef CONFIG_BSP_ENABLE_CAMERA
        if (s_dash.overlay_tool == DEBUG_TOOL_CAMERA_TEST) {
            bsp_camera_stop_preview();
        }
#endif
        lv_obj_del(s_dash.overlay);
        s_dash.overlay = NULL;
        s_dash.overlay_periph = NULL;
        s_dash.overlay_tool = DEBUG_TOOL_MAX;
        s_dash.camera_canvas = NULL;
        s_dash.camera_status_label = NULL;
    }

    s_dash.overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_dash.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_dash.overlay, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(s_dash.overlay, 0, 0);
    lv_obj_set_style_radius(s_dash.overlay, 0, 0);
    lv_obj_move_foreground(s_dash.overlay);
    s_dash.overlay_tool = DEBUG_TOOL_MAX;

    header = lv_obj_create(s_dash.overlay);
    lv_obj_set_size(header, LV_PCT(100), 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_BG_HEADER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    back_btn = lv_button_create(header);
    lv_obj_set_size(back_btn, 120, 44);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a3e66), 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_add_event_cb(back_btn, overlay_back_event_cb, LV_EVENT_ALL, NULL);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(back_label);

    title = lv_label_create(header);
    lv_label_set_text_fmt(title, "%s", periph->name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 50, 0);

    content = lv_obj_create(s_dash.overlay);
    lv_obj_set_size(content, LV_PCT(94), 460);
    lv_obj_set_pos(content, 30, 92);
    lv_obj_set_style_bg_color(content, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_border_color(content, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(content, 2, 0);
    lv_obj_set_style_radius(content, 10, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 12, 0);

    status_row = lv_obj_create(content);
    lv_obj_set_size(status_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 0, 0);
    lv_obj_set_style_pad_column(status_row, 10, 0);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    status_dot = lv_obj_create(status_row);
    lv_obj_set_size(status_dot, 14, 14);
    lv_obj_set_style_radius(status_dot, 7, 0);
    lv_obj_set_style_bg_color(status_dot, overlay_status_badge_color(periph->status), 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_set_style_pad_all(status_dot, 0, 0);

    status_label = lv_label_create(status_row);
    lv_label_set_text_fmt(status_label, "Status: %s", status_to_text(periph->status));
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);

    desc = lv_label_create(content);
    lv_label_set_text_fmt(desc, "Description:\n%s\n%s", periph->description, periph->detail);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT_PRIMARY), 0);

    get_peripheral_gpio_text(periph, pins_buf, sizeof(pins_buf));

    pins_label = lv_label_create(content);
    lv_label_set_text_fmt(pins_label, "GPIO Pins:\n%s", pins_buf);
    lv_obj_set_style_text_font(pins_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pins_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    s_dash.overlay_periph = periph;

    ESP_LOGI(TAG, "Opened peripheral overlay: %s", periph->name);
}

static void periph_card_event_cb(lv_event_t *e)
{
    peripheral_info_t *periph;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    periph = (peripheral_info_t *)lv_event_get_user_data(e);
    if (periph == NULL) {
        return;
    }

    if (periph->status == PERIPH_STATUS_DISABLED ||
        periph->status == PERIPH_STATUS_NOT_IMPL) {
        return;
    }

    show_peripheral_overlay(periph);
}

static void tileview_event_cb(lv_event_t *e)
{
    lv_obj_t *active_tile;
    lv_obj_t *tv = lv_event_get_target(e);

    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) {
        return;
    }

    active_tile = lv_tileview_get_tile_active(tv);
    if (active_tile == s_dash.tile_debugtools_overflow) {
        s_dash.active_screen = 2;
    } else if (active_tile == s_dash.tile_debugtools) {
        s_dash.active_screen = 1;
    } else {
        s_dash.active_screen = 0;
    }

    update_page_indicators();
}

static lv_obj_t *create_peripheral_card(lv_obj_t *parent, peripheral_info_t *periph)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *icon_box;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *desc;
    lv_obj_t *detail;

    lv_obj_set_size(card, PERIPH_CARD_W, PERIPH_CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    icon_box = lv_obj_create(card);
    lv_obj_set_size(icon_box, 56, 56);
    lv_obj_set_style_bg_color(icon_box, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_color(icon_box, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(icon_box, 1, 0);
    lv_obj_set_style_radius(icon_box, 5, 0);
    lv_obj_set_style_pad_all(icon_box, 0, 0);
    lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, 0, 0);

    icon = lv_label_create(icon_box);
    lv_label_set_text(icon, periph->icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_center(icon);

    name = lv_label_create(card);
    lv_label_set_text(name, periph->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 66, 10);

    desc = lv_label_create(card);
    lv_label_set_text(desc, periph->description);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 66, 36);

    detail = lv_label_create(card);
    lv_label_set_text(detail, periph->detail);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 66, 58);

    periph->status_led = lv_obj_create(card);
    lv_obj_set_size(periph->status_led, 14, 14);
    lv_obj_set_style_radius(periph->status_led, 7, 0);
    lv_obj_set_style_border_width(periph->status_led, 0, 0);
    lv_obj_set_style_pad_all(periph->status_led, 0, 0);
    lv_obj_align(periph->status_led, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

    periph->status_label = lv_label_create(card);
    lv_label_set_text(periph->status_label, "---");
    lv_obj_set_style_text_font(periph->status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(periph->status_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(periph->status_label, LV_ALIGN_BOTTOM_RIGHT, -28, -5);

    lv_obj_add_event_cb(card, periph_card_event_cb, LV_EVENT_CLICKED, periph);

    periph->card = card;
    periph->description_label = desc;
    periph->detail_label = detail;

    if (strcmp(periph->name, "Sensors") == 0) {
        s_dash.env_peripheral = periph;
    }

    return card;
}

static lv_obj_t *create_debug_tool_card(lv_obj_t *parent, const tool_info_t *tool)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *icon_box;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *desc;
    lv_obj_t *detail;
    lv_obj_t *arrow;
    lv_obj_t *chart;

    lv_obj_set_size(card, TOOL_CARD_W, TOOL_CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    icon_box = lv_obj_create(card);
    lv_obj_set_size(icon_box, 56, 56);
    lv_obj_set_style_bg_color(icon_box, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_border_color(icon_box, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(icon_box, 1, 0);
    lv_obj_set_style_radius(icon_box, 5, 0);
    lv_obj_set_style_pad_all(icon_box, 0, 0);
    lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, 0, 0);

    icon = lv_label_create(icon_box);
    lv_label_set_text(icon, tool->icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_center(icon);

    name = lv_label_create(card);
    lv_label_set_text(name, tool->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 68, 10);

    desc = lv_label_create(card);
    lv_label_set_text(desc, tool->description);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 68, 38);

    detail = lv_label_create(card);
    lv_label_set_text(detail, tool->detail);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 68, 62);

    if (tool->tool_id == DEBUG_TOOL_SENSOR_MONITOR) {
        s_dash.sensor_tool_detail_label = detail;

        chart = lv_chart_create(card);
        lv_obj_set_size(chart, 148, 40);
        lv_obj_align(chart, LV_ALIGN_BOTTOM_RIGHT, -28, -8);
        lv_obj_set_style_bg_opa(chart, LV_OPA_30, 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, ENV_HISTORY_POINTS);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1200);
        lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
        lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);

        s_dash.sensor_tool_temp_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
        s_dash.sensor_tool_hum_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
        s_dash.sensor_tool_press_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
        s_dash.sensor_tool_chart = chart;
    }

    arrow = lv_label_create(card);
    lv_label_set_text(arrow, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(arrow, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, tool_card_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)tool->tool_id);

    return card;
}

static void create_header(lv_obj_t *tile, const char *subtitle, uint8_t screen_idx)
{
    lv_obj_t *header = lv_obj_create(tile);
    lv_obj_t *title;

    lv_obj_set_size(header, LV_PCT(100), HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_BG_HEADER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 12, 0);

    title = lv_label_create(header);
    lv_label_set_text(title, "Guition JC1060P470C - System Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    s_dash.header_info[screen_idx] = lv_label_create(header);
    lv_label_set_text(s_dash.header_info[screen_idx], "Heap: --KB | PSRAM: --KB | Up: --:--");
    lv_obj_set_style_text_font(s_dash.header_info[screen_idx], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_dash.header_info[screen_idx], lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(s_dash.header_info[screen_idx], LV_ALIGN_RIGHT_MID, 0, 0);

    {
        lv_obj_t *subtitle_label = lv_label_create(tile);
        lv_label_set_text(subtitle_label, subtitle);
        lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(subtitle_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_pos(subtitle_label, 20, SUBTITLE_Y);
    }
}

static void create_page_area(lv_obj_t *tile, uint8_t screen_idx)
{
    lv_obj_t *area = lv_obj_create(tile);
    lv_obj_t *dot1;
    lv_obj_t *dot2;
    lv_obj_t *dot3;

    lv_obj_set_size(area, LV_PCT(100), INDICATOR_H);
    lv_obj_set_pos(area, 0, INDICATOR_Y);
    lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_radius(area, 0, 0);
    lv_obj_set_style_pad_all(area, 0, 0);

    dot1 = lv_obj_create(area);
    lv_obj_set_size(dot1, 12, 12);
    lv_obj_set_style_radius(dot1, 6, 0);
    lv_obj_set_style_border_width(dot1, 0, 0);
    lv_obj_set_style_pad_all(dot1, 0, 0);
    lv_obj_align(dot1, LV_ALIGN_TOP_MID, -20, 8);

    dot2 = lv_obj_create(area);
    lv_obj_set_size(dot2, 12, 12);
    lv_obj_set_style_radius(dot2, 6, 0);
    lv_obj_set_style_border_width(dot2, 0, 0);
    lv_obj_set_style_pad_all(dot2, 0, 0);
    lv_obj_align(dot2, LV_ALIGN_TOP_MID, 0, 8);

    dot3 = lv_obj_create(area);
    lv_obj_set_size(dot3, 12, 12);
    lv_obj_set_style_radius(dot3, 6, 0);
    lv_obj_set_style_border_width(dot3, 0, 0);
    lv_obj_set_style_pad_all(dot3, 0, 0);
    lv_obj_align(dot3, LV_ALIGN_TOP_MID, 20, 8);

    s_dash.indicator_dot[screen_idx][0] = dot1;
    s_dash.indicator_dot[screen_idx][1] = dot2;
    s_dash.indicator_dot[screen_idx][2] = dot3;

    s_dash.swipe_hint[screen_idx] = lv_label_create(area);
    lv_obj_set_style_text_font(s_dash.swipe_hint[screen_idx], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_dash.swipe_hint[screen_idx], lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(s_dash.swipe_hint[screen_idx], LV_ALIGN_TOP_MID, 0, 30);
}

static void create_footer(lv_obj_t *tile, const char *text)
{
    lv_obj_t *footer = lv_obj_create(tile);
    lv_obj_t *footer_text;

    lv_obj_set_size(footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_pos(footer, 0, FOOTER_Y);
    lv_obj_set_style_bg_color(footer, lv_color_hex(COLOR_BG_HEADER), 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);

    footer_text = lv_label_create(footer);
    lv_label_set_text(footer_text, text);
    lv_obj_set_style_text_font(footer_text, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(footer_text, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_center(footer_text);
}

static void create_screen1_peripherals(lv_obj_t *tile)
{
    lv_obj_t *container = lv_obj_create(tile);

    create_header(tile, "Hardware Peripherals Status", 0);

    lv_obj_set_size(container, LV_PCT(100), CARDS_H);
    lv_obj_set_pos(container, 0, CARDS_Y);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_pad_left(container, 20, 0);
    lv_obj_set_style_pad_right(container, 20, 0);
    lv_obj_set_style_pad_top(container, 8, 0);
    lv_obj_set_style_pad_bottom(container, 8, 0);
    lv_obj_set_style_pad_gap(container, PERIPH_CARD_GAP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < s_dash.periph_count; i++) {
        create_peripheral_card(container, &s_dash.peripherals[i]);
    }

    create_page_area(tile, 0);
    create_footer(tile, "ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 400MHz | Refresh: 2s");
}

static void create_screen2_debugtools(lv_obj_t *tile)
{
    uint8_t start_idx = 0;
    uint8_t end_idx = DEBUG_TOOL_MAX < DEBUG_TOOLS_PER_PAGE ? DEBUG_TOOL_MAX : DEBUG_TOOLS_PER_PAGE;
    lv_obj_t *container = lv_obj_create(tile);

    create_header(tile, "Debug & Development Tools", 1);

    lv_obj_set_size(container, LV_PCT(100), CARDS_H);
    lv_obj_set_pos(container, 0, CARDS_Y);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_pad_left(container, 20, 0);
    lv_obj_set_style_pad_right(container, 20, 0);
    lv_obj_set_style_pad_top(container, 8, 0);
    lv_obj_set_style_pad_bottom(container, 8, 0);
    lv_obj_set_style_pad_gap(container, TOOL_CARD_GAP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = start_idx; i < end_idx; i++) {
        create_debug_tool_card(container, &s_tools[i]);
    }

    create_page_area(tile, 1);
    create_footer(tile, "ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 400MHz | Tools Page 1");
}

static void create_screen3_debugtools_overflow(lv_obj_t *tile)
{
    uint8_t start_idx = DEBUG_TOOLS_PER_PAGE;
    uint8_t end_idx = DEBUG_TOOL_MAX;
    lv_obj_t *container = lv_obj_create(tile);

    create_header(tile, "Debug & Development Tools (Overflow)", 2);

    lv_obj_set_size(container, LV_PCT(100), CARDS_H);
    lv_obj_set_pos(container, 0, CARDS_Y);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_pad_left(container, 20, 0);
    lv_obj_set_style_pad_right(container, 20, 0);
    lv_obj_set_style_pad_top(container, 8, 0);
    lv_obj_set_style_pad_bottom(container, 8, 0);
    lv_obj_set_style_pad_gap(container, TOOL_CARD_GAP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = start_idx; i < end_idx; i++) {
        create_debug_tool_card(container, &s_tools[i]);
    }

    create_page_area(tile, 2);
    create_footer(tile, "ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 400MHz | Tools Overflow");
}

esp_err_t lvgl_dashboard_init(const dashboard_config_t *config)
{
    dashboard_config_t default_cfg = DASHBOARD_CONFIG_DEFAULT();

    if (s_dash.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_dash, 0, sizeof(s_dash));
    s_dash.overlay_tool = DEBUG_TOOL_MAX;

    if (config) {
        memcpy(&s_dash.config, config, sizeof(dashboard_config_t));
    } else {
        memcpy(&s_dash.config, &default_cfg, sizeof(dashboard_config_t));
    }

    init_peripheral_list();
    update_peripheral_status();

    if (!lvgl_port_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }

    s_dash.tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(s_dash.tileview, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
    lv_obj_set_style_bg_color(s_dash.tileview, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(s_dash.tileview, 0, 0);
    lv_obj_set_scrollbar_mode(s_dash.tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_dash.tileview, tileview_event_cb, LV_EVENT_SCROLL_END, NULL);

    s_dash.tile_peripherals = lv_tileview_add_tile(s_dash.tileview, 0, 0, LV_DIR_RIGHT);
    create_screen1_peripherals(s_dash.tile_peripherals);

    s_dash.tile_debugtools = lv_tileview_add_tile(
        s_dash.tileview,
        1,
        0,
        LV_DIR_LEFT | ((DEBUG_TOOL_MAX > DEBUG_TOOLS_PER_PAGE) ? LV_DIR_RIGHT : 0));
    create_screen2_debugtools(s_dash.tile_debugtools);

    if (DEBUG_TOOL_MAX > DEBUG_TOOLS_PER_PAGE) {
        s_dash.tile_debugtools_overflow = lv_tileview_add_tile(s_dash.tileview, 2, 0, LV_DIR_LEFT);
        create_screen3_debugtools_overflow(s_dash.tile_debugtools_overflow);
    } else {
        s_dash.tile_debugtools_overflow = NULL;
    }

    lv_obj_set_tile(s_dash.tileview, s_dash.tile_peripherals, LV_ANIM_OFF);
    s_dash.active_screen = 0;

    refresh_status_internal();

    if (s_dash.config.auto_refresh) {
        s_dash.refresh_timer = lv_timer_create(refresh_timer_cb, s_dash.config.refresh_interval_ms, NULL);
    }

    s_dash.env_timer = lv_timer_create(env_timer_cb, 1000, NULL);
    update_env_card_widgets();

    lvgl_port_unlock();

    s_dash.initialized = true;
    ESP_LOGI(TAG, "Dashboard initialized: %u peripherals, %u debug tools, %u pages", 
             s_dash.periph_count,
             DEBUG_TOOL_MAX,
             (unsigned int)(s_dash.tile_debugtools_overflow ? 3 : 2));

    return ESP_OK;
}

esp_err_t lvgl_dashboard_deinit(void)
{
    if (!s_dash.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_dash.refresh_timer) {
        lv_timer_delete(s_dash.refresh_timer);
        s_dash.refresh_timer = NULL;
    }

    if (s_dash.env_timer) {
        lv_timer_delete(s_dash.env_timer);
        s_dash.env_timer = NULL;
    }

    if (s_dash.overlay) {
#ifdef CONFIG_BSP_ENABLE_CAMERA
        if (s_dash.overlay_tool == DEBUG_TOOL_CAMERA_TEST) {
            bsp_camera_stop_preview();
        }
#endif
        lv_obj_del(s_dash.overlay);
        s_dash.overlay = NULL;
        s_dash.overlay_periph = NULL;
        s_dash.overlay_tool = DEBUG_TOOL_MAX;
        s_dash.camera_canvas = NULL;
        s_dash.camera_status_label = NULL;
    }

    if (s_dash.tileview) {
        lv_obj_del(s_dash.tileview);
        s_dash.tileview = NULL;
    }

    lvgl_port_unlock();

    memset(&s_dash, 0, sizeof(s_dash));
    return ESP_OK;
}

void lvgl_dashboard_load_screen(uint8_t screen_index, bool anim)
{
    lv_obj_t *target_tile;

    if (!s_dash.initialized) {
        return;
    }

    if (screen_index == 0) {
        target_tile = s_dash.tile_peripherals;
    } else if (screen_index == 1) {
        target_tile = s_dash.tile_debugtools;
    } else if (screen_index == 2 && s_dash.tile_debugtools_overflow) {
        target_tile = s_dash.tile_debugtools_overflow;
    } else {
        target_tile = s_dash.tile_debugtools;
    }

    if (!lvgl_port_lock(portMAX_DELAY)) {
        return;
    }

    lv_obj_set_tile(s_dash.tileview, target_tile, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    s_dash.active_screen = (screen_index == 2 && s_dash.tile_debugtools_overflow) ? 2 :
                           (screen_index <= 1 ? screen_index : 0);
    update_page_indicators();

    lvgl_port_unlock();
}

uint8_t lvgl_dashboard_get_active_screen(void)
{
    return s_dash.active_screen;
}

void lvgl_dashboard_refresh_status(void)
{
    if (!s_dash.initialized) {
        return;
    }

    if (!lvgl_port_lock(portMAX_DELAY)) {
        return;
    }

    refresh_status_internal();

    lvgl_port_unlock();
}

lv_obj_t *lvgl_dashboard_get_tileview(void)
{
    return s_dash.tileview;
}

#else

esp_err_t lvgl_dashboard_init(const dashboard_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t lvgl_dashboard_deinit(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void lvgl_dashboard_load_screen(uint8_t screen_index, bool anim)
{
    (void)screen_index;
    (void)anim;
}

uint8_t lvgl_dashboard_get_active_screen(void)
{
    return 0;
}

void lvgl_dashboard_refresh_status(void)
{
}

lv_obj_t *lvgl_dashboard_get_tileview(void)
{
    return NULL;
}

#endif
