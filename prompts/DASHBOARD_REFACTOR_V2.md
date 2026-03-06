# Dashboard Refactor V2 - Specification (CORRECTED)

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

### BSP Public API (bsp_board.h)
```c
esp_lcd_panel_handle_t bsp_display_get_handle(void);
esp_lcd_touch_handle_t bsp_touch_get_handle(void);
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);
```
- **NOTA**: Questi sono gli unici modi pubblici per verificare lo stato hardware
- **NO** variabili globali `g_touch_handle` (è static in bsp_board.c)
- **NO** funzioni `bsp_*_available()` (non esistono)

### bootstrap_manager.h API
```c
sdmmc_card_t* bootstrap_manager_get_sd_card(bootstrap_manager_t *manager);
bool bootstrap_is_warm_boot(void);
```
- **NO** `g_wifi_ready` o API per query WiFi status
- WiFi status non è queryable via API pubblica

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
3. **Status logic**: calcolare stato reale usando **BSP public API**
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
┌──────────────────────────────────────────────────┐
│ [← Back]   Peripheral Name      [⚙ Settings]   │
├──────────────────────────────────────────────────┤
│                                                  │
│   Status: OK / WARN / ERROR                      │
│   Description: ...                               │
│   Configuration:                                 │
│     • Param 1: value                             │
│     • Param 2: value                             │
│                                                  │
│   [Test Peripheral] [View Logs] [Calibrate]     │
│                                                  │
└──────────────────────────────────────────────────┘
```

### 2. Dynamic Test Pages (MODIFIED)

**Attuale** (lvgl_dashboard.c:943-949):
```c
// Test cards fisse (TEST_TOOL_TEST_MAX = 8)
for (uint8_t i = 0; i < TEST_TOOL_TEST_MAX; i++) {
    create_test_tool_card(container, &s_tests[i]);
}
```

**Nuovo** (conditional rendering usando **#ifdef**):
```c
// Test cards dinamiche basate su Kconfig
static void create_screen3_tests(lv_obj_t *tile) {
    lv_obj_t *container = lv_obj_create(tile);
    // ... setup container ...
    
    // Display tests (sempre disponibili se LVGL attivo)
#ifdef CONFIG_BSP_ENABLE_DISPLAY
    create_test_tool_card(container, &s_tests[TEST_IDX_DISPLAY_PATTERN]);
    create_test_tool_card(container, &s_tests[TEST_IDX_DISPLAY_COLOR]);
    create_test_tool_card(container, &s_tests[TEST_IDX_DISPLAY_GRADIENT]);
    create_test_tool_card(container, &s_tests[TEST_IDX_DISPLAY_BACKLIGHT]);
#endif
    
    // Touch tests (solo se touch abilitato)
#ifdef CONFIG_BSP_ENABLE_TOUCH
    create_test_tool_card(container, &s_tests[TEST_IDX_TOUCH_MULTITOUCH]);
    create_test_tool_card(container, &s_tests[TEST_IDX_TOUCH_CALIBRATION]);
    create_test_tool_card(container, &s_tests[TEST_IDX_TOUCH_GESTURE]);
    create_test_tool_card(container, &s_tests[TEST_IDX_TOUCH_PALM_REJECTION]);
#endif
}
```

**NOTA**: Usare `#ifdef` è più robusto di `IS_ENABLED()` per questo codebase.

### 3. Peripheral Status Logic (ENHANCED - CORRECTED)

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

**Nuovo** (runtime checks usando **BSP public API**):
```c
#include "bsp_board.h"  // Per bsp_*_get_handle()

static void set_peripheral_status(peripheral_info_t *periph) {
    if (!periph->implemented) {
        periph->status = PERIPH_STATUS_NOT_IMPL;
        return;
    }
    
    if (!periph->enabled_in_config) {
        periph->status = PERIPH_STATUS_DISABLED;
        return;
    }
    
    // Runtime status checks usando BSP API pubbliche
    if (strcmp(periph->name, "Display") == 0) {
        // Display OK se handle valido
        esp_lcd_panel_handle_t disp = bsp_display_get_handle();
        periph->status = (disp != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "Touch") == 0) {
        // Touch OK se handle valido (CORRETTO: usa bsp_touch_get_handle())
        esp_lcd_touch_handle_t touch = bsp_touch_get_handle();
        periph->status = (touch != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "I2C Bus") == 0) {
        // I2C OK se handle valido (CORRETTO: usa bsp_i2c_get_bus_handle())
        i2c_master_bus_handle_t i2c = bsp_i2c_get_bus_handle();
        periph->status = (i2c != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "Audio") == 0) {
        // Audio: nessuna API pubblica per check, assume OK se config enabled
        periph->status = PERIPH_STATUS_OK;
    }
    else if (strcmp(periph->name, "RTC") == 0) {
        // RTC: nessuna API pubblica per check, assume OK se config enabled
        periph->status = PERIPH_STATUS_OK;
    }
    else if (strcmp(periph->name, "SD Card") == 0) {
        // SD Card sempre WARNING (errore 0x108 noto)
        periph->status = PERIPH_STATUS_WARNING;
    }
    else if (strcmp(periph->name, "WiFi") == 0) {
        // WiFi: NESSUNA API pubblica per query status
        // Opzioni:
        // 1. Lasciare OK (assume bootstrap success)
        // 2. Sempre UNKNOWN (onesto ma brutto)
        // Scelta: OK (se bootstrap_manager_init() ha successo, WiFi è up)
        periph->status = PERIPH_STATUS_OK;
    }
    else {
        // Default: OK se enabled (per periferiche senza API)
        periph->status = PERIPH_STATUS_OK;
    }
}
```

**Differenze critiche vs versione precedente**:
- ✅ **Touch**: usa `bsp_touch_get_handle()` (PUBLIC API), non `extern g_touch_handle` (PRIVATE)
- ✅ **I2C**: usa `bsp_i2c_get_bus_handle()` (PUBLIC API), non `IS_ENABLED()` (fragile)
- ✅ **WiFi**: OK di default (nessuna API pubblica per status), documentato come limitation
- ✅ **Include**: richiede `#include "bsp_board.h"` in lvgl_dashboard.c

---

## Implementation Checklist

### Phase 1: Include BSP API
- [ ] Aggiungere `#include "bsp_board.h"` in lvgl_dashboard.c
- [ ] Verificare che `components/guition_jc1060_bsp` sia nel include path

### Phase 2: Peripheral Card Click (NEW FEATURE)
- [ ] Aggiungere `periph_card_event_cb()` callback
- [ ] Modificare `create_peripheral_card()` per rendere card clickable se abilitata
- [ ] Implementare `show_peripheral_overlay()` con layout base
- [ ] Aggiungere back button → `lv_obj_del(overlay)` per tornare a dashboard

### Phase 3: Dynamic Test Pages (REFACTOR)
- [ ] Modificare `create_screen3_tests()` per render condizionale con `#ifdef`
- [ ] Definire indici array `s_tests[]` come costanti (TEST_IDX_DISPLAY_PATTERN, etc.)
- [ ] Rimuovere test cards per periferiche disabilitate
- [ ] Verificare che test array s_tests[] contenga tutte le entry (anche se non renderizzate)

### Phase 4: Enhanced Status Logic (REFACTOR)
- [ ] Rifattorizzare `set_peripheral_status()` con runtime checks
- [ ] Usare `bsp_display_get_handle()` per Display status
- [ ] Usare `bsp_touch_get_handle()` per Touch status (NON extern)
- [ ] Usare `bsp_i2c_get_bus_handle()` per I2C status (NON IS_ENABLED)
- [ ] Documentare WiFi limitation (no public API per status)

### Phase 5: Testing & Validation
- [ ] Testare dashboard con tutte le periferiche abilitate
- [ ] Testare con solo Display+Touch (config minimale)
- [ ] Verificare overlay navigation (click card → overlay → back)
- [ ] Verificare test pages dinamiche (solo test per HW presente)
- [ ] Verificare status LED corretto per Display/Touch/I2C (runtime check)

---

## Files Modified

1. **main/src/lvgl_dashboard.c** (MAIN TARGET)
   - Add: `#include "bsp_board.h"`
   - Refactor: `set_peripheral_status()` (BSP API calls)
   - New: `periph_card_event_cb()`, `show_peripheral_overlay()`
   - Refactor: `create_screen3_tests()` (dynamic rendering con #ifdef)
   - Refactor: `create_peripheral_card()` (add clickable flag)

2. **main/include/lvgl_dashboard.h** (se necessario)
   - New: enum per test indices (TEST_IDX_DISPLAY_PATTERN, etc.)

3. **NO CHANGES TO**:
   - `main/main.c` (already calls `lvgl_dashboard_init()` directly)
   - `main/Kconfig.projbuild` (no new symbols needed)
   - `components/guition_jc1060_bsp/` (no BSP API changes)

---

## Known Limitations

### WiFi Status Check
**Problema**: `bootstrap_manager.h` non esporta API per query WiFi readiness.

**Soluzioni possibili**:
1. **Attuale (SCELTA)**: assume OK dopo `bootstrap_manager_init()` success
2. **Future**: aggiungere `bootstrap_manager_get_wifi_status()` in bootstrap API
3. **Workaround**: leggere GPIO handshake (C6_IO2_HANDSHAKE), ma è hack

**Decisione**: lasciare OK, documentare come limitation. Se bootstrap fallisce, main.c fa restart.

### Audio/RTC Status Check
**Problema**: nessuna API pubblica per verificare ES8311/RX8025T init.

**Soluzione**: assume OK se Kconfig enabled. Per check reale, servirebbe:
- Audio: `bsp_audio_get_handle()` (non esiste)
- RTC: `bsp_rtc_get_handle()` (non esiste)

**Decisione**: acceptable, perché bsp_board_init() fa log ERROR se falliscono.

---

## Migration Notes

- **Backward compatible**: il dashboard continua a funzionare senza modifiche a main.c
- **Config-driven**: tutte le feature si abilitano/disabilitano via Kconfig esistente
- **Runtime safe**: check di nullptr su tutti gli handle BSP
- **No API breaking**: funzioni pubbliche `lvgl_dashboard_*()` invariate
- **Include dependency**: richiede `bsp_board.h` (già disponibile nel component)

---

## Success Criteria

✅ Card periferiche diventano clickable → overlay settings  
✅ Test pages mostrano solo test per periferiche abilitate (`#ifdef`)  
✅ Status periferiche usa **BSP public API** (no extern, no IS_ENABLED fragile)  
✅ Display/Touch/I2C status riflette handle validity (runtime check)  
✅ Back button da overlay torna a dashboard  
✅ Dashboard compila senza warning con tutte le combinazioni Kconfig  
✅ Nessuna modifica a main.c, solo refactor interno di lvgl_dashboard.c  
✅ WiFi limitation documentata (no public API per status)
