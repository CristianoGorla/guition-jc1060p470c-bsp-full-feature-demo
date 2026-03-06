# LVGL Dashboard Refactoring - Complete Specification v2

## 🎯 OBJECTIVE
Refactor `main/lvgl_demo.c` to implement a hierarchical navigation system with:
1. **Tileview** with horizontal swipe navigation (Status Page + Test Tools Pages)
2. **Conditional rendering** based on peripheral status (enabled_in_config && implemented)
3. **Overlay Settings Pages** opened by clicking active peripheral cards

---

## 📊 CURRENT STATE ANALYSIS

### Current Code Structure (`main/lvgl_demo.c`)
- **Function**: `lvgl_demo_simple()` - Basic UI with title, slider, button
- **Function**: `lvgl_demo_hw_test()` - Touch calibration test with crosshair
- **Demo selection**: Kconfig-driven (`CONFIG_LVGL_DEMO_SIMPLE_UI`, `CONFIG_LVGL_DEMO_HW_TEST`)

### Peripherals in System (from sdkconfig/BSP)
```c
// Active peripherals (CONFIG_BSP_ENABLE_* = y)
Display      // JD9165 MIPI-DSI
Touch        // GT911
I2C Bus      // System bus
Audio        // ES8311 codec
RTC          // DS1307 or equivalent
Backlight    // PWM-controlled
UART         // Serial communication
Relay        // GPIO-controlled relays
Sensors      // Temperature/Humidity/Pressure/Motion

// Inactive peripherals (CONFIG_BSP_ENABLE_* = n OR not implemented)
SD Card      // SDMMC (disabled in config)
WiFi         // Network (disabled in config)
Camera       // MIPI-CSI (not implemented)
```

---

## 🏗️ NEW ARCHITECTURE

### 1️⃣ TILEVIEW STRUCTURE

#### Page 1: STATUS PAGE
**Purpose**: Show status of ALL peripherals (12 cards total)

**Layout**: 3 rows × 4 columns grid
- Card size: 240×116px (with margins)
- Display resolution: 1024×600 (landscape default)

**Card Types**:

**Active Peripheral Card** (9 cards):
```c
// Properties
- Opacity: LV_OPA_COVER (100%)
- Background: #ECF0F1 (light gray)
- Border: #2ECC71 (green), 2px
- Status: "OK" / "WARN" / "ERR" (runtime status)
- Flags: LV_OBJ_FLAG_CLICKABLE
- Event: LV_EVENT_CLICKED → open_settings_page(peripheral_id)
```

**Inactive Peripheral Card** (3 cards):
```c
// Properties
- Opacity: LV_OPA_40 (40% dimmed)
- Background: #D5DBDB (gray)
- Border: #95A5A6 (gray), 2px
- Status: "OFF" (disabled) / "N/A" (not implemented)
- Flags: ~LV_OBJ_FLAG_CLICKABLE (NOT clickable)
- Event: None
```

**Card Content Template**:
```
┌────────────────────┐
│  [Icon/Symbol]     │
│  Peripheral Name   │ ← Bold, 18px
│  Status: OK        │ ← Color-coded (green/orange/red)
└────────────────────┘
```

**Peripheral List**:
| ID | Name | Config Check | Implementation Check | Card State |
|----|------|-------------|---------------------|------------|
| 0 | Display | `CONFIG_BSP_ENABLE_DISPLAY` | `bsp_display_available()` | Active |
| 1 | Touch | `CONFIG_BSP_ENABLE_TOUCH` | `bsp_touch_available()` | Active |
| 2 | I2C Bus | `CONFIG_BSP_ENABLE_I2C_BUS` | Always true | Active |
| 3 | Audio | `CONFIG_BSP_ENABLE_AUDIO` | `bsp_audio_available()` | Active |
| 4 | SD Card | `CONFIG_BSP_ENABLE_SDCARD` | `bsp_sdcard_available()` | **Inactive** |
| 5 | WiFi | `CONFIG_BSP_ENABLE_WIFI` | `bsp_wifi_available()` | **Inactive** |
| 6 | RTC | `CONFIG_BSP_ENABLE_RTC` | `bsp_rtc_available()` | Active |
| 7 | Camera | `CONFIG_BSP_ENABLE_CAMERA` | `false` (not impl) | **Inactive** |
| 8 | Backlight | `CONFIG_BSP_ENABLE_BACKLIGHT` | Always true | Active |
| 9 | UART | `CONFIG_BSP_ENABLE_UART` | `bsp_uart_available()` | Active |
| 10 | Relay | `CONFIG_BSP_ENABLE_RELAY` | `bsp_relay_available()` | Active |
| 11 | Sensors | `CONFIG_BSP_ENABLE_SENSORS` | `bsp_sensors_available()` | Active |

---

#### Pages 2+: TEST TOOLS PAGES
**Purpose**: Provide test/debug controls ONLY for active peripherals

**Conditional Rendering Rule**:
```c
if (peripheral.enabled_in_config == true && peripheral.implemented == true) {
    create_test_card(peripheral);
}
// NO card created if inactive
```

**Dynamic Page Calculation**:
```c
int active_count = count_active_peripherals();  // Example: 9
int test_cards_per_peripheral = 3;              // Average
int total_test_cards = active_count * test_cards_per_peripheral;  // 27 cards
int cards_per_page = 9;  // 3×3 grid
int test_pages = (total_test_cards + cards_per_page - 1) / cards_per_page;  // ceil(27/9) = 3
```

**Test Card Examples**:
- **Display**: Pattern Test, Color Test, Rotation Test
- **Touch**: Multi-Touch, Calibration, Gesture Test
- **I2C**: Scanner, Device Info, Speed Test
- **Audio**: Volume, Tone Generator, Mic Test
- **RTC**: Time Set, Alarm, Sync
- **Backlight**: Dimming, Auto-Dim, PWM Frequency
- **UART**: Loopback, Echo Test, Baud Rate
- **Relay**: ON/OFF Toggle, Pulse Mode, Safety Test
- **Sensors**: Read Values, Polling Rate, Alert Threshold

**NO TEST CARDS FOR**:
- SD Card (disabled)
- WiFi (disabled)
- Camera (not implemented)

---

### 2️⃣ OVERLAY SETTINGS PAGES

**Trigger**: Click on **active** peripheral card in Status Page

**Implementation**:
```c
// NOT part of tileview
lv_obj_t *settings_overlay = lv_obj_create(lv_screen_active());
lv_obj_set_size(settings_overlay, LV_PCT(100), LV_PCT(100));
lv_obj_add_flag(settings_overlay, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
```

**Layout Template**:
```
┌─────────────────────────────────┐
│ ← Back    [Peripheral Name]      │  ← Header (60px)
├─────────────────────────────────┤
│                                  │
│  Setting 1:  [Slider/Dropdown]   │  ← Content area
│  Setting 2:  [Switch/Spinner]    │
│  Setting 3:  [Button/Input]      │
│  ...                             │
│                                  │
└─────────────────────────────────┘
```

**Settings Pages to Create** (9 overlays for active peripherals):

1. **Display Settings**
   - Brightness (slider 0-100%)
   - Rotation (dropdown: 0°/90°/180°/270°)
   - Sleep timeout (spinner)
   - Color inversion (switch)
   - Resolution (read-only: 1024x600)
   - Refresh rate (read-only: 60 Hz)

2. **Touch Settings**
   - Sensitivity (slider)
   - Calibration (button)
   - Multi-touch enable (switch)
   - Swap X/Y (switch)
   - Invert X (switch)
   - Invert Y (switch)

3. **I2C Bus Settings**
   - Clock speed (dropdown: 100kHz/400kHz/1MHz)
   - Pull-up SDA (switch: Internal/External/None)
   - Pull-up SCL (switch: Internal/External/None)
   - Device scan (button)
   - Devices found (list: addresses)
   - Bus status (indicator)

4. **Audio Settings**
   - Volume (slider 0-100%)
   - Sample rate (dropdown: 8kHz/16kHz/44.1kHz/48kHz)
   - Codec type (read-only: ES8311)
   - Test tone (button: Play 1kHz)
   - Mute (switch)
   - Input source (dropdown: Mic/Line In)

5. **RTC Settings**
   - Date (date picker)
   - Time (time picker)
   - Timezone (dropdown)
   - Auto sync NTP (switch)
   - Battery voltage (read-only)
   - Last sync (read-only timestamp)

6. **Backlight Settings**
   - Brightness (slider 0-100%)
   - Auto-dim (switch)
   - Dim timeout (spinner: 10s/30s/1min/5min)
   - Dim level (slider 0-50%)
   - Fade speed (dropdown: Fast/Normal/Slow)
   - PWM frequency (read-only: 5kHz)

7. **UART Settings**
   - Baud rate (dropdown: 9600/115200/230400)
   - Data bits (dropdown: 7/8)
   - Parity (dropdown: None/Even/Odd)
   - Stop bits (dropdown: 1/2)
   - Flow control (dropdown: None/RTS-CTS/XON-XOFF)
   - RX buffer (spinner: 256/512/1024/2048 bytes)

8. **Relay Settings**
   - Relay 1 state (switch: ON/OFF)
   - Relay 2 state (switch: ON/OFF)
   - Auto mode (dropdown: Manual/Timed/Sensor-triggered)
   - Pulse duration (spinner: 100ms/500ms/1s/5s)
   - Safety timeout (spinner: 10s/30s/1min/None)
   - Status LED (switch)

9. **Sensors Settings**
   - Temperature (read-only: °C)
   - Humidity (read-only: %)
   - Pressure (read-only: hPa)
   - Motion detect (switch)
   - Polling rate (dropdown: 100ms/500ms/1s/5s)
   - Alert thresholds (spinners: Temp/Humidity limits)

**DO NOT CREATE Settings Pages for**:
- SD Card (disabled)
- WiFi (disabled)
- Camera (not implemented)

**Navigation**:
```c
// Open overlay
void open_settings_page(int peripheral_id) {
    lv_obj_clear_flag(settings_pages[peripheral_id], LV_OBJ_FLAG_HIDDEN);
}

// Back button callback
void settings_back_cb(lv_event_t *e) {
    lv_obj_t *settings_page = lv_event_get_user_data(e);
    lv_obj_add_flag(settings_page, LV_OBJ_FLAG_HIDDEN);
}
```

---

## 💻 IMPLEMENTATION REQUIREMENTS

### File Structure
```
main/lvgl_demo.c  (REFACTOR THIS FILE)
├─ lvgl_demo_dashboard()         // NEW: Main dashboard entry point
│  ├─ create_tileview()
│  ├─ create_status_page()       // Page 1: 12 peripheral cards
│  ├─ create_test_tools_pages()  // Pages 2+: Dynamic test cards
│  └─ create_settings_overlays() // 9 overlay pages (outside tileview)
│
├─ Peripheral detection helpers
│  ├─ is_peripheral_active(id)   // Returns: config && implementation check
│  ├─ count_active_peripherals()
│  └─ get_peripheral_status(id)  // Returns: OK/WARN/ERR/OFF/N_A
│
└─ Event handlers
   ├─ status_card_click_cb()     // Opens settings overlay
   ├─ settings_back_cb()         // Hides settings overlay
   └─ test_card_action_cb()      // Executes test action
```

### Kconfig Integration
Update `main/Kconfig.projbuild`:
```kconfig
choice LVGL_DEMO
    prompt "Select LVGL Demo"
    default LVGL_DEMO_DASHBOARD

    config LVGL_DEMO_DASHBOARD
        bool "Dashboard (Peripheral Status + Test Tools)"
        help
            Multi-page dashboard with:
            - Status page (12 peripheral cards)
            - Test tools pages (active peripherals only)
            - Settings overlays (click to configure)

    config LVGL_DEMO_SIMPLE_UI
        bool "Simple UI (Original)"
        help
            Basic LVGL demo with minimal UI elements.
            Good for verifying display and touch functionality.

    config LVGL_DEMO_HW_TEST
        bool "Hardware Test UI"
        help
            Interactive hardware test demo with center button and crosshair.
            Useful for precise touch calibration and testing.
endchoice
```

### Code Template Structure
```c
// Peripheral definition structure
typedef struct {
    int id;
    const char *name;
    const char *config_symbol;
    bool (*check_impl)(void);
    lv_obj_t *status_card;
    lv_obj_t *settings_page;
} peripheral_t;

// Peripheral array
static peripheral_t peripherals[12] = {
    {0, "Display", "CONFIG_BSP_ENABLE_DISPLAY", bsp_display_available, NULL, NULL},
    {1, "Touch", "CONFIG_BSP_ENABLE_TOUCH", bsp_touch_available, NULL, NULL},
    {2, "I2C Bus", "CONFIG_BSP_ENABLE_I2C_BUS", NULL, NULL, NULL},
    {3, "Audio", "CONFIG_BSP_ENABLE_AUDIO", bsp_audio_available, NULL, NULL},
    {4, "SD Card", "CONFIG_BSP_ENABLE_SDCARD", bsp_sdcard_available, NULL, NULL},
    {5, "WiFi", "CONFIG_BSP_ENABLE_WIFI", bsp_wifi_available, NULL, NULL},
    {6, "RTC", "CONFIG_BSP_ENABLE_RTC", bsp_rtc_available, NULL, NULL},
    {7, "Camera", "CONFIG_BSP_ENABLE_CAMERA", NULL, NULL, NULL},
    {8, "Backlight", "CONFIG_BSP_ENABLE_BACKLIGHT", NULL, NULL, NULL},
    {9, "UART", "CONFIG_BSP_ENABLE_UART", bsp_uart_available, NULL, NULL},
    {10, "Relay", "CONFIG_BSP_ENABLE_RELAY", bsp_relay_available, NULL, NULL},
    {11, "Sensors", "CONFIG_BSP_ENABLE_SENSORS", bsp_sensors_available, NULL, NULL},
};

// Main dashboard function
esp_err_t lvgl_demo_dashboard(void) {
    ESP_LOGI(TAG, "Starting dashboard demo...");
    
    if (!lvgl_port_lock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    // Create tileview container
    lv_obj_t *tileview = lv_tileview_create(lv_screen_active());
    lv_obj_set_size(tileview, LV_PCT(100), LV_PCT(100));

    // Page 1: Status page
    lv_obj_t *status_page = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    create_status_page(status_page);

    // Pages 2+: Test tools (dynamic based on active peripherals)
    int active_count = count_active_peripherals();
    int test_pages = calculate_test_pages(active_count);
    ESP_LOGI(TAG, "Active peripherals: %d, Test pages: %d", active_count, test_pages);
    
    for (int i = 0; i < test_pages; i++) {
        lv_dir_t dir = LV_DIR_LEFT | (i < test_pages - 1 ? LV_DIR_RIGHT : 0);
        lv_obj_t *test_page = lv_tileview_add_tile(tileview, i + 1, 0, dir);
        create_test_tools_page(test_page, i);
    }

    // Settings overlays (outside tileview)
    create_settings_overlays();

    // Load first page
    lv_obj_set_tile(tileview, status_page, LV_ANIM_OFF);

    lvgl_port_unlock();
    
    ESP_LOGI(TAG, "Dashboard demo started successfully");
    return ESP_OK;
}

// Helper: Check if peripheral is active
bool is_peripheral_active(int id) {
    peripheral_t *p = &peripherals[id];
    
    #ifdef CONFIG_BSP_ENABLE_DISPLAY
        if (id == 0) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_TOUCH
        if (id == 1) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_I2C_BUS
        if (id == 2) return true;
    #endif
    #ifdef CONFIG_BSP_ENABLE_AUDIO
        if (id == 3) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_SDCARD
        if (id == 4) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_WIFI
        if (id == 5) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_RTC
        if (id == 6) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_CAMERA
        if (id == 7) return false;  // Not implemented
    #endif
    #ifdef CONFIG_BSP_ENABLE_BACKLIGHT
        if (id == 8) return true;
    #endif
    #ifdef CONFIG_BSP_ENABLE_UART
        if (id == 9) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_RELAY
        if (id == 10) return (p->check_impl ? p->check_impl() : true);
    #endif
    #ifdef CONFIG_BSP_ENABLE_SENSORS
        if (id == 11) return (p->check_impl ? p->check_impl() : true);
    #endif
    
    return false;
}

// Helper: Count active peripherals
int count_active_peripherals(void) {
    int count = 0;
    for (int i = 0; i < 12; i++) {
        if (is_peripheral_active(i)) {
            count++;
        }
    }
    return count;
}

// Helper: Get peripheral status string
const char* get_peripheral_status(int id) {
    if (!is_peripheral_active(id)) {
        // Check if disabled in config or not implemented
        return "OFF";  // Simplified, can add logic to distinguish OFF vs N/A
    }
    
    // Runtime status check (placeholder)
    // In real implementation, query actual peripheral status
    return "OK";
}

// Status page creator
void create_status_page(lv_obj_t *parent) {
    // Header
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, "Peripheral Status");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(0x00d9ff), 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);

    // 12 cards (3 rows × 4 cols)
    for (int i = 0; i < 12; i++) {
        int row = i / 4;
        int col = i % 4;
        int x = 50 + col * 250;
        int y = 80 + row * 140;

        bool active = is_peripheral_active(i);
        
        // Card container
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 240, 116);
        lv_obj_set_pos(card, x, y);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        
        // Styling based on active state
        if (active) {
            lv_obj_set_style_bg_color(card, lv_color_hex(0xECF0F1), 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0x2ECC71), 0);
            lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, status_card_click_cb, LV_EVENT_CLICKED, &peripherals[i]);
        } else {
            lv_obj_set_style_bg_color(card, lv_color_hex(0xD5DBDB), 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0x95A5A6), 0);
            lv_obj_set_style_opa(card, LV_OPA_40, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
        }
        
        // Card content
        lv_obj_t *name_label = lv_label_create(card);
        lv_label_set_text(name_label, peripherals[i].name);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_18, 0);
        lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 20);
        
        lv_obj_t *status_label = lv_label_create(card);
        const char *status = get_peripheral_status(i);
        lv_label_set_text_fmt(status_label, "Status: %s", status);
        lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
        
        // Status color coding
        if (strcmp(status, "OK") == 0) {
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x2ECC71), 0);
        } else if (strcmp(status, "WARN") == 0) {
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xF39C12), 0);
        } else if (strcmp(status, "ERR") == 0) {
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xE74C3C), 0);
        } else {
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x95A5A6), 0);
        }
        
        lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -15);
        
        peripherals[i].status_card = card;
    }
}

// Test tools page creator
void create_test_tools_page(lv_obj_t *parent, int page_num) {
    // Header
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text_fmt(header, "Test & Debug Tools - Page %d", page_num + 1);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(0x00d9ff), 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);

    // Only create test cards for ACTIVE peripherals
    int card_idx = 0;
    for (int i = 0; i < 12; i++) {
        if (!is_peripheral_active(i)) continue;  // Skip inactive
        
        // Calculate cards for this peripheral (3 test cards per peripheral)
        for (int test = 0; test < 3; test++) {
            int global_card_num = card_idx;
            int cards_per_page = 9;  // 3×3 grid
            
            if (global_card_num / cards_per_page != page_num) {
                card_idx++;
                continue;  // This card belongs to a different page
            }
            
            int local_idx = global_card_num % cards_per_page;
            int row = local_idx / 3;
            int col = local_idx % 3;
            int x = 100 + col * 300;
            int y = 80 + row * 160;
            
            // Create test card
            lv_obj_t *card = lv_obj_create(parent);
            lv_obj_set_size(card, 240, 116);
            lv_obj_set_pos(card, x, y);
            lv_obj_set_style_bg_color(card, lv_color_hex(0xECF0F1), 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0x2ECC71), 0);
            lv_obj_set_style_border_width(card, 2, 0);
            lv_obj_set_style_radius(card, 10, 0);
            
            lv_obj_t *label = lv_label_create(card);
            lv_label_set_text_fmt(label, "%s\nTest %d", peripherals[i].name, test + 1);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
            lv_obj_center(label);
            
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, test_card_action_cb, LV_EVENT_CLICKED, NULL);
            
            card_idx++;
        }
    }
}

// Settings overlays creator
void create_settings_overlays(void) {
    for (int i = 0; i < 12; i++) {
        if (!is_peripheral_active(i)) continue;  // Only active peripherals
        
        // Create overlay (full screen)
        lv_obj_t *overlay = lv_obj_create(lv_screen_active());
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay, lv_color_white(), 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
        
        // Header
        lv_obj_t *header = lv_obj_create(overlay);
        lv_obj_set_size(header, LV_PCT(100), 60);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x3498DB), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        
        // Back button
        lv_obj_t *back_btn = lv_button_create(header);
        lv_obj_set_size(back_btn, 120, 40);
        lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xE74C3C), 0);
        lv_obj_add_event_cb(back_btn, settings_back_cb, LV_EVENT_CLICKED, overlay);
        
        lv_obj_t *back_label = lv_label_create(back_btn);
        lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
        lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
        lv_obj_center(back_label);
        
        // Title
        lv_obj_t *title = lv_label_create(header);
        lv_label_set_text_fmt(title, "%s Settings", peripherals[i].name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);
        
        // Content area (populate with peripheral-specific settings)
        create_settings_content(overlay, i);
        
        peripherals[i].settings_page = overlay;
        
        ESP_LOGI(TAG, "Created settings page for %s", peripherals[i].name);
    }
}

// Create settings content for specific peripheral
void create_settings_content(lv_obj_t *parent, int peripheral_id) {
    // Content container
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_set_size(content, LV_PCT(90), LV_PCT(80));
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 20, 0);
    
    // Add placeholder settings (to be implemented per peripheral)
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text_fmt(label, "Settings for %s\n\n(To be implemented)", peripherals[peripheral_id].name);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

// Event handler: Status card clicked
void status_card_click_cb(lv_event_t *e) {
    peripheral_t *p = (peripheral_t *)lv_event_get_user_data(e);
    if (p->settings_page) {
        ESP_LOGI(TAG, "Opening settings for %s", p->name);
        lv_obj_clear_flag(p->settings_page, LV_OBJ_FLAG_HIDDEN);
    }
}

// Event handler: Settings back button
void settings_back_cb(lv_event_t *e) {
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Closing settings page");
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

// Event handler: Test card action
void test_card_action_cb(lv_event_t *e) {
    lv_obj_t *card = lv_event_get_target(e);
    // Placeholder for test action
    ESP_LOGI(TAG, "Test card clicked");
}

// Helper: Calculate number of test pages
int calculate_test_pages(int active_count) {
    int test_cards_per_peripheral = 3;
    int total_test_cards = active_count * test_cards_per_peripheral;
    int cards_per_page = 9;
    return (total_test_cards + cards_per_page - 1) / cards_per_page;  // Ceiling division
}

// Update lvgl_demo_run_from_config() to support new dashboard
esp_err_t lvgl_demo_run_from_config(void) {
#ifdef CONFIG_LVGL_DEMO_DASHBOARD
    ESP_LOGI(TAG, "Config: Dashboard UI");
    return lvgl_demo_dashboard();
#elif defined(CONFIG_LVGL_DEMO_SIMPLE_UI)
    ESP_LOGI(TAG, "Config: Simple UI (default)");
    return lvgl_demo_simple();
#elif defined(CONFIG_LVGL_DEMO_HW_TEST)
    ESP_LOGI(TAG, "Config: Hardware Test UI");
    return lvgl_demo_hw_test();
#else
    ESP_LOGW(TAG, "No demo selected in menuconfig, using Simple UI");
    return lvgl_demo_simple();
#endif
}
```

---

## ✅ ACCEPTANCE CRITERIA

### Functional Requirements
- [ ] Tileview with horizontal swipe navigation works
- [ ] Status page shows ALL 12 peripheral cards
- [ ] Active peripheral cards are clickable (opacity 100%, green border)
- [ ] Inactive peripheral cards are NOT clickable (opacity 40%, gray border)
- [ ] Test Tools pages contain ONLY cards for active peripherals
- [ ] Number of Test Tools pages is dynamic (calculated from active peripheral count)
- [ ] Settings overlay opens when clicking active status card
- [ ] Settings overlay hides when clicking "Back" button
- [ ] Settings overlay is full-screen and NOT part of tileview

### Visual Requirements
- [ ] Status page layout: 3 rows × 4 columns (12 cards)
- [ ] Test Tools layout: 3 rows × 3 columns (9 cards per page)
- [ ] Card dimensions: 240×116px
- [ ] Active card colors: Light gray bg (#ECF0F1), green border (#2ECC71)
- [ ] Inactive card colors: Gray bg (#D5DBDB), gray border (#95A5A6), 40% opacity
- [ ] Settings overlay: White background, blue header (#3498DB), red back button (#E74C3C)

### Configuration Requirements
- [ ] Kconfig option `CONFIG_LVGL_DEMO_DASHBOARD` added
- [ ] Peripheral active state checks both CONFIG and implementation
- [ ] No compilation errors if peripheral BSP functions are missing

### Code Quality Requirements
- [ ] Preserve existing `lvgl_demo_simple()` and `lvgl_demo_hw_test()` functions
- [ ] Add `lvgl_demo_dashboard()` as new function
- [ ] Update `lvgl_demo_run_from_config()` to handle new demo type
- [ ] Use consistent naming conventions
- [ ] Add comments for complex logic
- [ ] Log ESP_LOGI messages for navigation events

---

## 📝 NOTES

1. **DO NOT** delete or modify existing demo functions (`lvgl_demo_simple`, `lvgl_demo_hw_test`)
2. **DO** add new `lvgl_demo_dashboard()` function alongside existing ones
3. **DO** use `lvgl_port_lock()` / `lvgl_port_unlock()` for thread safety
4. **DO** check return values and handle errors
5. **DO** use `ESP_LOGI` for debugging navigation flow
6. **DO** implement placeholder settings content (sliders, switches, buttons) for all 9 active peripheral settings pages
7. **DO NOT** create settings pages for SD Card, WiFi, Camera (inactive peripherals)
8. **DO** make test card actions log messages (real functionality can be implemented later)
9. **DO** test with different peripheral configurations (enable/disable in menuconfig)
10. **DO** ensure smooth animations and responsive touch feedback

---

## 🚀 IMPLEMENTATION PRIORITY

### Phase 1: Core Structure
1. Create tileview container
2. Implement peripheral detection helpers (`is_peripheral_active`, `count_active_peripherals`)
3. Create Status Page with 12 cards (correct styling for active/inactive)
4. Test Status Page rendering with current config

### Phase 2: Navigation
5. Implement conditional rendering for Test Tools pages
6. Calculate dynamic page count
7. Create test cards grid layout
8. Test horizontal swipe navigation

### Phase 3: Settings Overlays
9. Create settings overlay structure for active peripherals
10. Implement header with back button
11. Add click event handlers for status cards
12. Test overlay show/hide navigation

### Phase 4: Content Population
13. Populate settings content for each of 9 active peripherals
14. Add placeholder controls (sliders, switches, dropdowns)
15. Implement test card action callbacks (logging)
16. Add status color coding (OK=green, WARN=orange, ERR=red)

### Phase 5: Integration & Testing
17. Add Kconfig integration
18. Update `lvgl_demo_run_from_config()`
19. Test with different peripheral configurations
20. Verify no compilation errors with missing BSP functions
21. Final polish: animations, colors, spacing

---

## 📸 VISUAL REFERENCE

See diagrams:
- `lvgl_dashboard_complete_ui.png` - Full UI structure overview
- `lvgl_dashboard_all_settings_pages.png` - All 12 settings pages (9 active + 3 inactive)

---

**END OF SPECIFICATION**
