# Prompt: LVGL Two-Screen Dashboard con Swipe Navigation

**Repository:** [CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/tree/feature/lvgl-v9-integration)  
**Branch:** `feature/lvgl-v9-integration`

## Obiettivo

Creare una **dashboard UI LVGL a 2 schermate** con navigazione swipe orizzontale:
- **Screen 1**: Hardware Peripherals Status (12 card in griglia 3×4)
- **Screen 2**: Debug Tools Launcher (9 card in griglia 3×3)

**Display**: 1024x600 pixels  
**Navigazione**: Swipe left/right con page indicators

---

## Mockup Visivi

![Screen 1: Peripherals](../docs/dashboard_screen1_peripherals.png)  
![Screen 2: Debug Tools](../docs/dashboard_screen2_debugtools.png)

---

## Layout Schermata 1: Peripherals

```
┌─────────────────────────────────────────────────────────────────┐
│ Guition JC1060P470C - System Dashboard   Heap: 203KB | PSRAM.. │ ← 70px
├─────────────────────────────────────────────────────────────────┤
│ Hardware Peripherals Status                                     │ ← 35px
├─────────────────────────────────────────────────────────────────┤
│ Row 1: Display | Touch | I2C | Audio                           │
│ Row 2: RTC | SD Card | WiFi | Camera                           │ ← 385px
│ Row 3: Sensors | GPIO Exp | SPI Flash | UART Ext              │
├─────────────────────────────────────────────────────────────────┤
│                        ⚫ ⚪                                      │
│              Swipe left for Debug Tools →                       │ ← 70px
├─────────────────────────────────────────────────────────────────┤
│   ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 360MHz | Refresh:2s│ ← 40px
└─────────────────────────────────────────────────────────────────┘
```

**Dimensioni**:
- Card: 240×110 px
- Gap: 15px
- Margin: 20px left/right

---

## Layout Schermata 2: Debug Tools

```
┌─────────────────────────────────────────────────────────────────┐
│ Guition JC1060P470C - System Dashboard   Heap: 203KB | PSRAM.. │ ← 70px
├─────────────────────────────────────────────────────────────────┤
│ Debug & Development Tools                                       │ ← 35px
├─────────────────────────────────────────────────────────────────┤
│ Row 1: Log Monitor | Camera Test | Sensor Monitor              │
│ Row 2: WiFi Scanner | SD Browser | I2C Scanner                 │ ← 385px
│ Row 3: System Info | GPIO Monitor | Performance                │
├─────────────────────────────────────────────────────────────────┤
│                        ⚪ ⚫                                      │
│                 ← Swipe right to return                         │ ← 70px
├─────────────────────────────────────────────────────────────────┤
│   ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 360MHz | Touch..  │ ← 40px
└─────────────────────────────────────────────────────────────────┘
```

**Dimensioni**:
- Card: 320×120 px
- Gap: 20px
- Margin: 20px

---

## Task 1: Header File

Crea **`main/include/lvgl_dashboard.h`**:

```c
#pragma once
#include "lvgl.h"
#include "esp_err.h"

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
lv_obj_t* lvgl_dashboard_get_tileview(void);

#ifdef __cplusplus
}
#endif
```

---

## Task 2: Implementation File

Crea **`main/src/lvgl_dashboard.c`**

### Color Scheme
```c
#define COLOR_BG_DARK       0x0a0a0a
#define COLOR_BG_CARD       0x1a1a2e
#define COLOR_BG_HEADER     0x16213e
#define COLOR_ACCENT        0x00d9ff
#define COLOR_TEXT_PRIMARY  0xffffff
#define COLOR_TEXT_SECONDARY 0x888888
#define COLOR_STATUS_OK      0x00ff88
#define COLOR_STATUS_WARN    0xffaa00
#define COLOR_STATUS_ERROR   0xff5555
#define COLOR_STATUS_OFF     0x444444
```

### Peripheral List (12 items)

```c
static void init_peripheral_list(void)
{
    s_dash.periph_count = 0;
    
    // Row 1
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
        .detail = "I2C 0x5D",
        .icon_symbol = LV_SYMBOL_EDIT,
        .enabled_in_config = true,
        .implemented = true,
    };
    
    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "I2C Bus",
        .description = "400kHz master",
        .detail = "5 devices",
        .icon_symbol = LV_SYMBOL_SHUFFLE,
        .enabled_in_config = true,
        .implemented = true,
    };
    
    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Audio",
        .description = "ES8311 + NS4150 PA",
        .detail = "Disabled",
        .icon_symbol = LV_SYMBOL_VOLUME_MAX,
#ifdef CONFIG_BSP_ENABLE_AUDIO
        .enabled_in_config = true,
#else
        .enabled_in_config = false,
#endif
        .implemented = true,
    };
    
    // Row 2
    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "RTC",
        .description = "RX8025T w/ battery",
        .detail = "Vbat: 3.1V",
        .icon_symbol = LV_SYMBOL_CLOCK,
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
        .detail = "SDIO mode",
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
    
    // Row 3 (future peripherals)
    s_dash.peripherals[s_dash.periph_count++] = (peripheral_info_t){
        .name = "Sensors",
        .description = "Temp/Humidity/Pressure",
        .detail = "Future",
        .icon_symbol = LV_SYMBOL_THERMOMETER,
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
```

### Debug Tools List (9 items)

```c
typedef struct {
    const char *name;
    const char *description;
    const char *detail;
    const char *icon_symbol;
    debug_tool_t tool_id;
} tool_info_t;

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
        .icon_symbol = LV_SYMBOL_THERMOMETER,
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
```

### Card Creation Functions

**Peripheral Card** (240×110 px):
```c
static lv_obj_t* create_peripheral_card(lv_obj_t *parent, peripheral_info_t *periph)
{
    // Card container
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 240, 110);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    
    // Icon box (55x55)
    lv_obj_t *icon_box = lv_obj_create(card);
    lv_obj_set_size(icon_box, 55, 55);
    lv_obj_set_style_bg_color(icon_box, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_color(icon_box, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(icon_box, 1, 0);
    lv_obj_set_style_radius(icon_box, 5, 0);
    lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Icon symbol
    lv_obj_t *icon = lv_label_create(icon_box);
    lv_label_set_text(icon, periph->icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_center(icon);
    
    // Name (13pt bold)
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, periph->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 65, 18);
    
    // Description (9pt)
    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, periph->description);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 65, 38);
    
    // Detail (8pt italic)
    lv_obj_t *detail = lv_label_create(card);
    lv_label_set_text(detail, periph->detail);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 65, 55);
    
    // Status LED (7px circle)
    periph->status_led = lv_obj_create(card);
    lv_obj_set_size(periph->status_led, 14, 14);
    lv_obj_set_style_radius(periph->status_led, 7, 0);
    lv_obj_set_style_border_width(periph->status_led, 0, 0);
    lv_obj_align(periph->status_led, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
    
    // Status text (11pt bold)
    periph->status_label = lv_label_create(card);
    lv_label_set_text(periph->status_label, "---");
    lv_obj_set_style_text_font(periph->status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(periph->status_label, LV_ALIGN_BOTTOM_RIGHT, -25, -5);
    
    // Opacity for disabled/not implemented
    lv_opa_t opacity = (periph->status == PERIPH_STATUS_DISABLED || 
                        periph->status == PERIPH_STATUS_NOT_IMPL) ? LV_OPA_30 : LV_OPA_COVER;
    lv_obj_set_style_opa(card, opacity, 0);
    
    periph->card = card;
    return card;
}
```

**Debug Tool Card** (320×120 px):
```c
static lv_obj_t* create_debug_tool_card(lv_obj_t *parent, const tool_info_t *tool)
{
    // Card container
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 320, 120);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    
    // Icon box (55x55)
    lv_obj_t *icon_box = lv_obj_create(card);
    lv_obj_set_size(icon_box, 55, 55);
    lv_obj_set_style_bg_color(icon_box, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_border_color(icon_box, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(icon_box, 1, 0);
    lv_obj_set_style_radius(icon_box, 5, 0);
    lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Icon symbol
    lv_obj_t *icon = lv_label_create(icon_box);
    lv_label_set_text(icon, tool->icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_center(icon);
    
    // Name (14pt bold)
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, tool->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 70, 22);
    
    // Description (10pt)
    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, tool->description);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 70, 48);
    
    // Detail (9pt italic)
    lv_obj_t *detail = lv_label_create(card);
    lv_label_set_text(detail, tool->detail);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 70, 70);
    
    // Arrow (play symbol)
    lv_obj_t *arrow = lv_label_create(card);
    lv_label_set_text(arrow, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(arrow, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    
    // Click event
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, tool_card_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)tool->tool_id);
    
    return card;
}
```

### Screen Creation Functions

**Screen 1: Peripherals**:
```c
static void create_screen1_peripherals(lv_obj_t *tile)
{
    // Header (70px)
    lv_obj_t *header = lv_obj_create(tile);
    lv_obj_set_size(header, LV_PCT(100), 70);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_BG_HEADER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 15, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Guition JC1060P470C - System Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    // System info (heap/PSRAM)
    lv_obj_t *sys_info = lv_label_create(header);
    size_t free_heap = esp_get_free_heap_size() / 1024;
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    lv_label_set_text_fmt(sys_info, "Heap: %zu KB | PSRAM: %zu KB | Uptime: %02lu:%02lu",
                          free_heap, free_psram, 
                          esp_log_timestamp() / 60000, 
                          (esp_log_timestamp() / 1000) % 60);
    lv_obj_set_style_text_font(sys_info, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sys_info, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(sys_info, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Subtitle (35px)
    lv_obj_t *subtitle = lv_label_create(tile);
    lv_label_set_text(subtitle, "Hardware Peripherals Status");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 20, 80);
    
    // Card container (flex layout)
    lv_obj_t *container = lv_obj_create(tile);
    lv_obj_set_size(container, LV_PCT(100), 385);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 125);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 20, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(container, 15, 0);
    
    // Create 12 peripheral cards
    for (uint8_t i = 0; i < s_dash.periph_count; i++) {
        create_peripheral_card(container, &s_dash.peripherals[i]);
    }
    
    // Page indicator area
    // ... (create at tile bottom)
    
    // Footer (40px)
    lv_obj_t *footer = lv_obj_create(tile);
    lv_obj_set_size(footer, LV_PCT(100), 40);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(footer, lv_color_hex(COLOR_BG_HEADER), 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    
    lv_obj_t *footer_text = lv_label_create(footer);
    lv_label_set_text(footer_text, "ESP-IDF v5.5.3 | LVGL v9.2.2 | ESP32-P4 @ 360MHz | Auto-refresh: 2s");
    lv_obj_set_style_text_font(footer_text, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(footer_text, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_center(footer_text);
}
```

**Screen 2: Debug Tools**:
```c
static void create_screen2_debugtools(lv_obj_t *tile)
{
    // Same header/subtitle/footer structure
    // ...
    
    // Card container
    lv_obj_t *container = lv_obj_create(tile);
    lv_obj_set_size(container, LV_PCT(100), 385);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 125);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 20, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(container, 20, 0);
    
    // Create 9 tool cards
    for (uint8_t i = 0; i < DEBUG_TOOL_MAX; i++) {
        create_debug_tool_card(container, &s_tools[i]);
    }
}
```

### Main Init Function

```c
esp_err_t lvgl_dashboard_init(const dashboard_config_t *config)
{
    if (s_dash.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Copy config
    if (config) {
        memcpy(&s_dash.config, config, sizeof(dashboard_config_t));
    } else {
        dashboard_config_t default_cfg = DASHBOARD_CONFIG_DEFAULT();
        memcpy(&s_dash.config, &default_cfg, sizeof(dashboard_config_t));
    }
    
    // Init data
    init_peripheral_list();
    update_peripheral_status();
    
    // Lock LVGL
    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Create tileview (2 horizontal tiles)
    s_dash.tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(s_dash.tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_dash.tileview, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_add_event_cb(s_dash.tileview, tileview_event_cb, LV_EVENT_SCROLL_END, NULL);
    
    // Tile 0,0: Peripherals
    s_dash.tile_peripherals = lv_tileview_add_tile(s_dash.tileview, 0, 0, LV_DIR_RIGHT);
    create_screen1_peripherals(s_dash.tile_peripherals);
    
    // Tile 1,0: Debug Tools
    s_dash.tile_debugtools = lv_tileview_add_tile(s_dash.tileview, 1, 0, LV_DIR_LEFT);
    create_screen2_debugtools(s_dash.tile_debugtools);
    
    // Set active screen to 0
    lv_obj_set_tile(s_dash.tileview, s_dash.tile_peripherals, LV_ANIM_OFF);
    s_dash.active_screen = 0;
    
    // Auto-refresh timer
    if (s_dash.config.auto_refresh) {
        s_dash.refresh_timer = lv_timer_create(refresh_timer_cb, 
                                               s_dash.config.refresh_interval_ms, NULL);
    }
    
    lvgl_port_unlock();
    s_dash.initialized = true;
    
    ESP_LOGI(TAG, "Dashboard initialized: %d peripherals, %d tools", 
             s_dash.periph_count, DEBUG_TOOL_MAX);
    
    return ESP_OK;
}
```

---

## Task 3: CMakeLists.txt

In **`main/CMakeLists.txt`**:
```cmake
set(srcs
    "main.c"
    "lvgl_demo.c"
    "lvgl_init.c"
    "src/lvgl_dashboard.c"    # ADD
)
```

---

## Task 4: Integration in main.c

```c
#include "lvgl_dashboard.h"

static void on_debug_tool_selected(debug_tool_t tool, void *user_data)
{
    ESP_LOGI(TAG, "Debug tool selected: %d", tool);
    
    switch (tool) {
        case DEBUG_TOOL_LOG_MONITOR:
            ESP_LOGI(TAG, "Launch log monitor");
            // TODO: lvgl_log_monitor_init()
            break;
        case DEBUG_TOOL_CAMERA_TEST:
            ESP_LOGI(TAG, "Camera test not implemented");
            break;
        // ... etc
        default:
            break;
    }
}

void app_main(void)
{
    // ... BSP init ...
    
    dashboard_config_t cfg = DASHBOARD_CONFIG_DEFAULT();
    cfg.tool_callback = on_debug_tool_selected;
    
    esp_err_t ret = lvgl_dashboard_init(&cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OK] Dashboard loaded");
    }
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Validation Checklist

✅ Build without errors  
✅ Screen 1 shows 12 peripheral cards in 3×4 grid  
✅ Status LEDs colored correctly (green/orange/gray)  
✅ Swipe left → transitions to Screen 2  
✅ Screen 2 shows 9 tool cards in 3×3 grid  
✅ Tap tool card → callback invoked  
✅ Swipe right → returns to Screen 1  
✅ Page indicator updates (⚫⚪ vs ⚪⚫)  
✅ Auto-refresh every 2s works  
✅ Heap/PSRAM info updates  

---

## Notes

- **LVGL v9 API**: Uses `lv_tileview_create()` and `lv_tileview_add_tile()`
- **Display**: Optimized for 1024×600 pixels
- **Memory**: ~60 KB LVGL objects (with PSRAM, no problem)
- **Scalability**: Easy to add 3rd screen (tile 2,0)

---

**Commit**: `feat(lvgl): add two-screen dashboard UI with swipe navigation`

🎯 **Ready for Copilot!**
