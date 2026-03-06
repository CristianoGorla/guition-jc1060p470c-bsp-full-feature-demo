# Dashboard Refactor V2 - Specification

**Repository**: `CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo`  
**Branch**: `develop/v1.3.0`  
**Target File**: `main/src/lvgl_dashboard.c` (existing, 34KB)  
**Method**: Extend existing architecture, don't rewrite

---

## Context Analysis (Codice Esistente)

### Current main.c Flow (line 235)
```c
ret = lvgl_dashboard_init(&dashboard_cfg);
```
- `main.c` chiama **direttamente** `lvgl_dashboard_init()`, non usa `lvgl_demo_run_from_config()`
- **Implicazione**: il refactor deve essere fatto DENTRO `lvgl_dashboard.c`, non in `lvgl_demo.c`

### Current Kconfig.projbuild (main/)
```
default LVGL_DEMO_SIMPLE_UI
```
- **Non esiste** `CONFIG_LVGL_DEMO_DASHBOARD`
- Opzioni esistenti: `SIMPLE_UI`, `HW_TEST`, `WIDGETS`
- **Implicazione**: non servono nuovi simboli Kconfig per il dashboard (già hardcoded in main.c)

### BSP Kconfig Symbols (reali)
```
CONFIG_BSP_ENABLE_DISPLAY     (default y)
CONFIG_BSP_ENABLE_I2C         (default y)
CONFIG_BSP_ENABLE_TOUCH       (default y, depends on I2C)
CONFIG_BSP_ENABLE_AUDIO       (default n, depends on I2C)
CONFIG_BSP_ENABLE_RTC         (default n, depends on I2C)
CONFIG_BSP_ENABLE_SDCARD      (default n, experimental)
CONFIG_BSP_ENABLE_WIFI        (default n)
```

### Current Dashboard Architecture (lvgl_dashboard.c)
- **12 periferiche fisse** (array `s_dash.peripherals[12]`):
  - Display, Touch, I2C Bus, Audio, RTC, SD Card, WiFi (attive)
  - Camera, Sensors, GPIO Exp, SPI Flash, UART Ext (future/placeholder)
- **3 tileview screens**: peripherals (0), debug tools (1), tests (2)
- **Card system**: `create_peripheral_card()`, `create_debug_tool_card()`, `create_test_tool_card()`
- **No overlay settings**: card click non implementato per periferiche
- **Test pages statiche**: non condizionate da periferiche attive

---

## Obiettivo Refactor

Trasformare il dashboard esistente da **status viewer statico** a **interactive control panel dinamico**:

1. **Peripheral Cards**: aggiungere callback click → overlay settings
2. **Test Pages**: renderizzare dinamicamente solo per periferiche abilitate
3. **Status logic**: calcolare stato reale (non solo config check)
4. **Navigation**: back button da overlay → main dashboard

---

## Design Changes

### 1. Peripheral Card Interaction (NEW)

**Attuale** (lvgl_dashboard.c:381-399):
```c
// Card periferiche: NO callback, solo display
create_peripheral_card(container, &s_dash.peripherals[i]);
```

**Nuovo**:
```c
// Card clickable con overlay settings
static void periph_card_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    
    peripheral_info_t *periph = lv_event_get_user_data(e);
    
    if (periph->status == PERIPH_STATUS_DISABLED || 
        periph->status == PERIPH_STATUS_NOT_IMPL) {
        return; // Non clickable se disabilitata
    }
    
    show_peripheral_overlay(periph);
}
```

**Overlay Layout**:
```
┌─────────────────────────────────────────────────┐
│ [← Back]   Peripheral Name      [⚙ Settings]   │
├─────────────────────────────────────────────────┤
│                                                  │
│   Status: OK / WARN / ERROR                      │
│   Description: ...                               │
│   Configuration:                                 │
│     • Param 1: value                             │
│     • Param 2: value                             │
│                                                  │
│   [Test Peripheral] [View Logs] [Calibrate]     │
│                                                  │
└─────────────────────────────────────────────────┘
```

### 2. Dynamic Test Pages (MODIFIED)

**Attuale** (lvgl_dashboard.c:943-949):
```c
// Test cards fisse (TEST_TOOL_TEST_MAX = 8)
for (uint8_t i = 0; i < TEST_TOOL_TEST_MAX; i++) {
    create_test_tool_card(container, &s_tests[i]);
}
```

**Nuovo** (conditional rendering):
```c
// Test cards dinamiche basate su periferiche attive
typedef struct {
    const char *name;
    test_tool_t test_id;
    bool (*is_available)(void); // Function pointer per check
} test_info_ext_t;

static bool is_display_available(void) {
    return IS_ENABLED(CONFIG_BSP_ENABLE_DISPLAY);
}

static bool is_touch_available(void) {
    return IS_ENABLED(CONFIG_BSP_ENABLE_TOUCH);
}

static const test_info_ext_t s_tests_ext[] = {
    // Display tests (sempre disponibili, display è obbligatorio)
    { "Pattern Test", TEST_TOOL_DISPLAY_PATTERN, is_display_available },
    { "Color Test", TEST_TOOL_DISPLAY_COLOR, is_display_available },
    { "Gradient Test", TEST_TOOL_DISPLAY_GRADIENT, is_display_available },
    { "Backlight Control", TEST_TOOL_DISPLAY_BACKLIGHT, is_display_available },
    
    // Touch tests (solo se CONFIG_BSP_ENABLE_TOUCH)
    { "Multi-Touch Test", TEST_TOOL_TOUCH_MULTITOUCH, is_touch_available },
    { "Calibration Test", TEST_TOOL_TOUCH_CALIBRATION, is_touch_available },
    { "Gesture Detection", TEST_TOOL_TOUCH_GESTURE, is_touch_available },
    { "Palm Rejection", TEST_TOOL_TOUCH_PALM_REJECTION, is_touch_available },
};

// Render loop
for (uint8_t i = 0; i < ARRAY_SIZE(s_tests_ext); i++) {
    if (s_tests_ext[i].is_available && s_tests_ext[i].is_available()) {
        create_test_tool_card(container, &s_tests_ext[i]);
    }
}
```

### 3. Peripheral Status Logic (ENHANCED)

**Attuale** (lvgl_dashboard.c:253-266):
```c
static void set_peripheral_status(peripheral_info_t *periph) {
    if (!periph->implemented) {
        periph->status = PERIPH_STATUS_NOT_IMPL;
        return;
    }
    
    if (!periph->enabled_in_config) {
        periph->status = PERIPH_STATUS_DISABLED;
        return;
    }
    
    // Hardcoded special case
    if (strcmp(periph->name, "SD Card") == 0) {
        periph->status = PERIPH_STATUS_WARNING;
        return;
    }
    
    periph->status = PERIPH_STATUS_OK;
}
```

**Nuovo** (runtime checks, senza bsp_*_available()):
```c
static void set_peripheral_status(peripheral_info_t *periph) {
    if (!periph->implemented) {
        periph->status = PERIPH_STATUS_NOT_IMPL;
        return;
    }
    
    if (!periph->enabled_in_config) {
        periph->status = PERIPH_STATUS_DISABLED;
        return;
    }
    
    // Runtime status checks (usando get_handle o global state)
    if (strcmp(periph->name, "Display") == 0) {
        // Display sempre OK se LVGL è attivo
        periph->status = PERIPH_STATUS_OK;
    }
    else if (strcmp(periph->name, "Touch") == 0) {
        // Touch OK se handle valido (da esp_lvgl_port)
        extern esp_lcd_touch_handle_t g_touch_handle; // Dichiarato altrove
        periph->status = (g_touch_handle != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "I2C Bus") == 0) {
        // I2C OK se bus attivo (check config)
        periph->status = IS_ENABLED(CONFIG_BSP_ENABLE_I2C) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "Audio") == 0) {
        // Audio: status dipende da ES8311 init (non verificabile facilmente, assume OK)
        periph->status = PERIPH_STATUS_OK;
    }
    else if (strcmp(periph->name, "RTC") == 0) {
        // RTC: status dipende da RX8025T probe (non verificabile facilmente, assume OK)
        periph->status = PERIPH_STATUS_OK;
    }
    else if (strcmp(periph->name, "SD Card") == 0) {
        // SD Card sempre WARNING (errore 0x108 noto)
        periph->status = PERIPH_STATUS_WARNING;
    }
    else if (strcmp(periph->name, "WiFi") == 0) {
        // WiFi: controllare se ESP-Hosted è connesso
        extern bool g_wifi_ready; // Global da bootstrap_manager
        periph->status = g_wifi_ready ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else {
        // Default: OK se enabled
        periph->status = PERIPH_STATUS_OK;
    }
}
```

**Note**: 
- **NON esistono** funzioni `bsp_display_available()` o simili nel BSP corrente
- Status checks devono usare **global handles** o **Kconfig symbols**
- Per periferiche complesse (WiFi, SD), check runtime usando variabili globali

---

## Implementation Checklist

### Phase 1: Peripheral Card Click (NEW FEATURE)
- [ ] Aggiungere `periph_card_event_cb()` callback
- [ ] Modificare `create_peripheral_card()` per rendere card clickable se abilitata
- [ ] Implementare `show_peripheral_overlay()` con layout base
- [ ] Aggiungere back button → `lv_obj_del(overlay)` per tornare a dashboard

### Phase 2: Dynamic Test Pages (REFACTOR)
- [ ] Creare `test_info_ext_t` struct con `is_available()` function pointer
- [ ] Definire array `s_tests_ext[]` con conditional checks
- [ ] Modificare `create_screen3_tests()` per render condizionale
- [ ] Rimuovere test cards per periferiche disabilitate

### Phase 3: Enhanced Status Logic (REFACTOR)
- [ ] Rifattorizzare `set_peripheral_status()` con runtime checks
- [ ] Dichiarare extern per global handles necessari (touch, wifi)
- [ ] Rimuovere hardcoded check "SD Card"
- [ ] Testare status updates con diverse configurazioni Kconfig

### Phase 4: Testing & Validation
- [ ] Testare dashboard con tutte le periferiche abilitate
- [ ] Testare con solo Display+Touch (config minimale)
- [ ] Verificare overlay navigation (click card → overlay → back)
- [ ] Verificare test pages dinamiche (solo test per HW presente)

---

## Files Modified

1. **main/src/lvgl_dashboard.c** (MAIN TARGET)
   - Refactor: `set_peripheral_status()` (enhanced logic)
   - New: `periph_card_event_cb()`, `show_peripheral_overlay()`
   - Refactor: `create_screen3_tests()` (dynamic rendering)
   - Refactor: `create_peripheral_card()` (add clickable flag)

2. **main/include/lvgl_dashboard.h** (se necessario)
   - New: `typedef bool (*periph_check_fn_t)(void);`
   - New: struct `test_info_ext_t` definition

3. **NO CHANGES TO**:
   - `main/main.c` (already calls `lvgl_dashboard_init()` directly)
   - `main/Kconfig.projbuild` (no new symbols needed)
   - `components/guition_jc1060_bsp/Kconfig.projbuild` (BSP config unchanged)

---

## Migration Notes

- **Backward compatible**: il dashboard continua a funzionare senza modifiche a main.c
- **Config-driven**: tutte le feature si abilitano/disabilitano via Kconfig esistente
- **Runtime safe**: check di nullptr su tutti gli handle globali
- **No API breaking**: funzioni pubbliche `lvgl_dashboard_*()` invariate

---

## Success Criteria

✅ Card periferiche diventano clickable → overlay settings  
✅ Test pages mostrano solo test per periferiche abilitate  
✅ Status periferiche riflette stato runtime (non solo Kconfig)  
✅ Back button da overlay torna a dashboard  
✅ Dashboard compila senza warning con tutte le combinazioni Kconfig  
✅ Nessuna modifica a main.c, solo refactor interno di lvgl_dashboard.c
