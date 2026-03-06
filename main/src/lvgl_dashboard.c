#include "sdkconfig.h"

#include "lvgl_dashboard.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"

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

#define TEST_CARD_W           240
#define TEST_CARD_H           116
#define TEST_CARD_GAP         8

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
} peripheral_info_t;

typedef struct {
    const char *name;
    const char *description;
    const char *detail;
    const char *icon_symbol;
    debug_tool_t tool_id;
} tool_info_t;

typedef struct {
    const char *name;
    const char *description;
    const char *detail;
    const char *icon_symbol;
    test_tool_t test_id;
} test_info_t;

typedef struct {
    bool initialized;
    uint8_t active_screen;
    dashboard_config_t config;

    lv_obj_t *tileview;
    lv_obj_t *tile_peripherals;
    lv_obj_t *tile_debugtools;
    lv_obj_t *tile_tests;

    lv_obj_t *header_info[3];
    lv_obj_t *indicator_dot[3][3];
    lv_obj_t *swipe_hint[3];

    lv_timer_t *refresh_timer;

    peripheral_info_t peripherals[12];
    uint8_t periph_count;
} dashboard_state_t;

static const char *TAG = "lvgl_dashboard";
static dashboard_state_t s_dash = {0};

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

static const test_info_t s_tests[] = {
    {
        .name = "Pattern Test",
        .description = "Grid, checkerboard, lines",
        .detail = "Panel uniformity",
        .icon_symbol = LV_SYMBOL_IMAGE,
        .test_id = TEST_TOOL_DISPLAY_PATTERN,
    },
    {
        .name = "Color Test",
        .description = "RGB gradient sweep",
        .detail = "Dead pixels, color accuracy",
        .icon_symbol = LV_SYMBOL_EYE_OPEN,
        .test_id = TEST_TOOL_DISPLAY_COLOR,
    },
    {
        .name = "Gradient Test",
        .description = "Smooth color transitions",
        .detail = "Banding detection",
        .icon_symbol = LV_SYMBOL_REFRESH,
        .test_id = TEST_TOOL_DISPLAY_GRADIENT,
    },
    {
        .name = "Backlight Control",
        .description = "Brightness adjustment",
        .detail = "0-100% PWM duty",
        .icon_symbol = LV_SYMBOL_SETTINGS,
        .test_id = TEST_TOOL_DISPLAY_BACKLIGHT,
    },
    {
        .name = "Multi-Touch Test",
        .description = "5-point simultaneous",
        .detail = "GT911 full capability",
        .icon_symbol = LV_SYMBOL_EDIT,
        .test_id = TEST_TOOL_TOUCH_MULTITOUCH,
    },
    {
        .name = "Calibration Test",
        .description = "Corner + center accuracy",
        .detail = "X/Y coordinate validation",
        .icon_symbol = LV_SYMBOL_GPS,
        .test_id = TEST_TOOL_TOUCH_CALIBRATION,
    },
    {
        .name = "Gesture Detection",
        .description = "Swipe, pinch, rotate",
        .detail = "Multi-finger gestures",
        .icon_symbol = LV_SYMBOL_SHUFFLE,
        .test_id = TEST_TOOL_TOUCH_GESTURE,
    },
    {
        .name = "Palm Rejection",
        .description = "Large contact filtering",
        .detail = "Accidental touch prevention",
        .icon_symbol = LV_SYMBOL_WARNING,
        .test_id = TEST_TOOL_TOUCH_PALM_REJECTION,
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
    if (!periph->implemented) {
        periph->status = PERIPH_STATUS_NOT_IMPL;
        return;
    }

    if (!periph->enabled_in_config) {
        periph->status = PERIPH_STATUS_DISABLED;
        return;
    }

    if (strcmp(periph->name, "SD Card") == 0) {
        periph->status = PERIPH_STATUS_WARNING;
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
        .enabled_in_config = true,
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Touch",
        .description = "GT911 5-point capacitive",
        .detail = "I2C 0x14",
        .icon_symbol = LV_SYMBOL_EDIT,
        .enabled_in_config = true,
        .implemented = true,
    };

    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "I2C Bus",
        .description = "400kHz master",
        .detail = "Touch+Audio+RTC",
        .icon_symbol = LV_SYMBOL_SHUFFLE,
        .enabled_in_config = true,
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
        .detail = "Future",
        .icon_symbol = LV_SYMBOL_SETTINGS,
        .enabled_in_config = false,
        .implemented = false,
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

    for (uint8_t i = 0; i < 3; i++) {
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

        if (!periph->status_led || !periph->status_label || !periph->card) {
            continue;
        }

        lv_obj_set_style_bg_color(periph->status_led, status_to_color(periph->status), 0);
        lv_label_set_text(periph->status_label, status_to_text(periph->status));

        opacity = (periph->status == PERIPH_STATUS_DISABLED ||
                   periph->status == PERIPH_STATUS_NOT_IMPL) ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_opa(periph->card, opacity, 0);
    }
}

static void update_page_indicators(void)
{
    for (uint8_t screen = 0; screen < 3; screen++) {
        for (uint8_t dot = 0; dot < 3; dot++) {
            if (s_dash.indicator_dot[screen][dot]) {
                lv_obj_set_style_bg_color(
                    s_dash.indicator_dot[screen][dot],
                    s_dash.active_screen == dot ? lv_color_hex(COLOR_ACCENT) : lv_color_hex(COLOR_STATUS_OFF),
                    0);
            }
        }
    }

    if (s_dash.swipe_hint[0]) {
        lv_label_set_text(s_dash.swipe_hint[0], "Swipe left for Debug Tools ->");
    }
    if (s_dash.swipe_hint[1]) {
        lv_label_set_text(s_dash.swipe_hint[1], "<- Swipe for Tests ->");
    }
    if (s_dash.swipe_hint[2]) {
        lv_label_set_text(s_dash.swipe_hint[2], "<- Swipe right to return");
    }
}

static void refresh_status_internal(void)
{
    update_peripheral_status();
    update_header_info_labels();
    apply_peripheral_status_to_ui();
    update_page_indicators();
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status_internal();
}

static void tool_card_event_cb(lv_event_t *e)
{
    debug_tool_t tool_id;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    tool_id = (debug_tool_t)(uintptr_t)lv_event_get_user_data(e);

    if (s_dash.config.tool_callback) {
        s_dash.config.tool_callback(tool_id, s_dash.config.user_data);
    }
}

static void test_card_event_cb(lv_event_t *e)
{
    test_tool_t test_id;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    test_id = (test_tool_t)(uintptr_t)lv_event_get_user_data(e);

    if (s_dash.config.test_callback) {
        s_dash.config.test_callback(test_id, s_dash.config.user_data);
    }
}

static void tileview_event_cb(lv_event_t *e)
{
    lv_obj_t *active_tile;
    lv_obj_t *tv = lv_event_get_target(e);

    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) {
        return;
    }

    active_tile = lv_tileview_get_tile_active(tv);
    if (active_tile == s_dash.tile_tests) {
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

    periph->card = card;
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

    arrow = lv_label_create(card);
    lv_label_set_text(arrow, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(arrow, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, tool_card_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)tool->tool_id);

    return card;
}

static lv_obj_t *create_test_tool_card(lv_obj_t *parent, const test_info_t *test)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *icon_box;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *desc;
    lv_obj_t *detail;
    lv_obj_t *arrow;

    lv_obj_set_size(card, TEST_CARD_W, TEST_CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xff6600), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    icon_box = lv_obj_create(card);
    lv_obj_set_size(icon_box, 56, 56);
    lv_obj_set_style_bg_color(icon_box, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_color(icon_box, lv_color_hex(0xff6600), 0);
    lv_obj_set_style_border_width(icon_box, 1, 0);
    lv_obj_set_style_radius(icon_box, 5, 0);
    lv_obj_set_style_pad_all(icon_box, 0, 0);
    lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, 0, 0);

    icon = lv_label_create(icon_box);
    lv_label_set_text(icon, test->icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xff6600), 0);
    lv_obj_center(icon);

    name = lv_label_create(card);
    lv_label_set_text(name, test->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 66, 10);

    desc = lv_label_create(card);
    lv_label_set_text(desc, test->description);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 66, 36);

    detail = lv_label_create(card);
    lv_label_set_text(detail, test->detail);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 66, 58);

    arrow = lv_label_create(card);
    lv_label_set_text(arrow, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(0xff6600), 0);
    lv_obj_align(arrow, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, test_card_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)test->test_id);

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

    for (uint8_t i = 0; i < DEBUG_TOOL_MAX; i++) {
        create_debug_tool_card(container, &s_tools[i]);
    }

    create_page_area(tile, 1);
    create_footer(tile, "ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 400MHz | Touch: enabled");
}

static void create_screen3_tests(lv_obj_t *tile)
{
    lv_obj_t *container = lv_obj_create(tile);

    create_header(tile, "Interactive Display & Touch Tests", 2);

    lv_obj_set_size(container, LV_PCT(100), CARDS_H);
    lv_obj_set_pos(container, 0, CARDS_Y);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_pad_left(container, 20, 0);
    lv_obj_set_style_pad_right(container, 20, 0);
    lv_obj_set_style_pad_top(container, 8, 0);
    lv_obj_set_style_pad_bottom(container, 8, 0);
    lv_obj_set_style_pad_gap(container, TEST_CARD_GAP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < TEST_TOOL_TEST_MAX; i++) {
        create_test_tool_card(container, &s_tests[i]);
    }

    create_page_area(tile, 2);
    create_footer(tile, "ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 400MHz | Test Suite: Display + Touch");
}

esp_err_t lvgl_dashboard_init(const dashboard_config_t *config)
{
    dashboard_config_t default_cfg = DASHBOARD_CONFIG_DEFAULT();

    if (s_dash.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_dash, 0, sizeof(s_dash));

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

    s_dash.tile_debugtools = lv_tileview_add_tile(s_dash.tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    create_screen2_debugtools(s_dash.tile_debugtools);

    s_dash.tile_tests = lv_tileview_add_tile(s_dash.tileview, 2, 0, LV_DIR_LEFT);
    create_screen3_tests(s_dash.tile_tests);

    lv_obj_set_tile(s_dash.tileview, s_dash.tile_peripherals, LV_ANIM_OFF);
    s_dash.active_screen = 0;

    refresh_status_internal();

    if (s_dash.config.auto_refresh) {
        s_dash.refresh_timer = lv_timer_create(refresh_timer_cb, s_dash.config.refresh_interval_ms, NULL);
    }

    lvgl_port_unlock();

    s_dash.initialized = true;
    ESP_LOGI(TAG, "Dashboard initialized: %u peripherals, %u debug tools, %u tests", 
             s_dash.periph_count, DEBUG_TOOL_MAX, TEST_TOOL_TEST_MAX);

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

    if (s_dash.tileview) {
        lv_obj_del(s_dash.tileview);
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
    } else {
        target_tile = s_dash.tile_tests;
    }

    if (!lvgl_port_lock(portMAX_DELAY)) {
        return;
    }

    lv_obj_set_tile(s_dash.tileview, target_tile, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    s_dash.active_screen = (screen_index <= 2) ? screen_index : 0;
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
