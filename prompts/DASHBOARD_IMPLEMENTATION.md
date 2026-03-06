# Dashboard Implementation Guide - ESEGUIBILE

**Target**: `main/src/lvgl_dashboard.c`  
**Goal**: Trasformare dashboard statico in interactive control panel  
**Approach**: Modifiche incrementali, testabili step-by-step

---

## STEP 1: Runtime Status Checks (BSP API)

### Obiettivo
Sostituire hardcoded status logic con runtime checks usando BSP public API.

### Modifiche

#### 1.1 Aggiungere include

**Posizione**: Dopo `#include "esp_lvgl_port.h"` (linea ~10)

```c
#include "esp_lvgl_port.h"
#include "bsp_board.h"  // <-- AGGIUNGERE
```

#### 1.2 Modificare `set_peripheral_status()`

**Posizione**: Sostituire funzione esistente (linee ~253-266)

**PRIMA** (codice attuale):
```c
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
```

**DOPO** (con runtime checks):
```c
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

    // Runtime checks usando BSP public API
    if (strcmp(periph->name, "Display") == 0) {
        esp_lcd_panel_handle_t disp = bsp_display_get_handle();
        periph->status = (disp != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "Touch") == 0) {
        esp_lcd_touch_handle_t touch = bsp_touch_get_handle();
        periph->status = (touch != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "I2C Bus") == 0) {
        i2c_master_bus_handle_t i2c = bsp_i2c_get_bus_handle();
        periph->status = (i2c != NULL) ? PERIPH_STATUS_OK : PERIPH_STATUS_ERROR;
    }
    else if (strcmp(periph->name, "SD Card") == 0) {
        periph->status = PERIPH_STATUS_WARNING;  // Errore 0x108 noto
    }
    else {
        // Audio, RTC, WiFi: assume OK (no public API per status check)
        periph->status = PERIPH_STATUS_OK;
    }
}
```

### Test
1. Compilare: `idf.py build`
2. Verificare: status LED Display/Touch/I2C devono riflettere handle validity
3. Disconnettere touch → status LED deve diventare rosso (ERROR)

---

## STEP 2: Clickable Peripheral Cards

### Obiettivo
Rendere le card periferiche clickable → mostrare overlay con dettagli.

### Modifiche

#### 2.1 Aggiungere stato overlay nel dashboard_state_t

**Posizione**: Dentro `typedef struct dashboard_state_t` (dopo `uint8_t periph_count;`, linea ~92)

```c
    peripheral_info_t peripherals[12];
    uint8_t periph_count;
    
    lv_obj_t *overlay;              // <-- AGGIUNGERE
    peripheral_info_t *overlay_periph;  // <-- AGGIUNGERE
} dashboard_state_t;
```

#### 2.2 Aggiungere callback per peripheral card click

**Posizione**: Dopo `test_card_event_cb()` (linea ~728)

```c
static void periph_card_event_cb(lv_event_t *e)
{
    peripheral_info_t *periph;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    periph = (peripheral_info_t *)lv_event_get_user_data(e);
    
    // Non aprire overlay se periferica disabilitata/non implementata
    if (periph->status == PERIPH_STATUS_DISABLED ||
        periph->status == PERIPH_STATUS_NOT_IMPL) {
        ESP_LOGW(TAG, "Peripheral '%s' not available", periph->name);
        return;
    }

    // Mostra overlay dettagli
    show_peripheral_overlay(periph);
}
```

#### 2.3 Implementare `show_peripheral_overlay()`

**Posizione**: Dopo `periph_card_event_cb()`

```c
static void overlay_back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_dash.overlay) {
        lv_obj_del(s_dash.overlay);
        s_dash.overlay = NULL;
        s_dash.overlay_periph = NULL;
    }
}

static void show_peripheral_overlay(peripheral_info_t *periph)
{
    lv_obj_t *back_btn, *back_label;
    lv_obj_t *title_label;
    lv_obj_t *status_container, *status_led, *status_text;
    lv_obj_t *info_label;
    char info_buf[256];

    if (s_dash.overlay) {
        lv_obj_del(s_dash.overlay);
    }

    if (!lvgl_port_lock(portMAX_DELAY)) {
        return;
    }

    // Crea overlay fullscreen
    s_dash.overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_dash.overlay, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
    lv_obj_set_style_bg_color(s_dash.overlay, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_border_width(s_dash.overlay, 0, 0);
    lv_obj_set_style_radius(s_dash.overlay, 0, 0);
    lv_obj_align(s_dash.overlay, LV_ALIGN_CENTER, 0, 0);

    // Back button (top-left)
    back_btn = lv_btn_create(s_dash.overlay);
    lv_obj_set_size(back_btn, 100, 50);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_add_event_cb(back_btn, overlay_back_btn_cb, LV_EVENT_CLICKED, NULL);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_center(back_label);

    // Title
    title_label = lv_label_create(s_dash.overlay);
    lv_label_set_text_fmt(title_label, "%s %s", periph->icon_symbol, periph->name);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 25);

    // Status indicator
    status_container = lv_obj_create(s_dash.overlay);
    lv_obj_set_size(status_container, 300, 60);
    lv_obj_align(status_container, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(status_container, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_border_width(status_container, 2, 0);
    lv_obj_set_style_border_color(status_container, status_to_color(periph->status), 0);
    lv_obj_set_style_radius(status_container, 8, 0);

    status_led = lv_obj_create(status_container);
    lv_obj_set_size(status_led, 20, 20);
    lv_obj_set_style_radius(status_led, 10, 0);
    lv_obj_set_style_bg_color(status_led, status_to_color(periph->status), 0);
    lv_obj_set_style_border_width(status_led, 0, 0);
    lv_obj_align(status_led, LV_ALIGN_LEFT_MID, 15, 0);

    status_text = lv_label_create(status_container);
    lv_label_set_text_fmt(status_text, "Status: %s", status_to_text(periph->status));
    lv_obj_set_style_text_font(status_text, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(status_text, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(status_text, LV_ALIGN_LEFT_MID, 50, 0);

    // Info section
    snprintf(info_buf, sizeof(info_buf),
             "Description: %s\n\n"
             "Details: %s\n\n"
             "Enabled in config: %s\n"
             "Implementation: %s",
             periph->description,
             periph->detail,
             periph->enabled_in_config ? "YES" : "NO",
             periph->implemented ? "Complete" : "Not implemented");

    info_label = lv_label_create(s_dash.overlay);
    lv_label_set_text(info_label, info_buf);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_width(info_label, 700);
    lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 170);

    s_dash.overlay_periph = periph;
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Overlay opened for: %s", periph->name);
}
```

#### 2.4 Rendere card clickable

**Posizione**: Dentro `create_peripheral_card()`, alla fine prima del `return card;` (linea ~790)

**AGGIUNGERE PRIMA DEL RETURN**:
```c
    periph->card = card;
    
    // Rendi card clickable se abilitata
    if (periph->enabled_in_config && periph->implemented) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, periph_card_event_cb, LV_EVENT_CLICKED, periph);
    }
    
    return card;
```

### Test
1. Compilare: `idf.py build`
2. Click su card Display → deve aprire overlay fullscreen
3. Click "Back" → deve tornare al dashboard
4. Click su card disabilitata (Camera) → nessuna azione

---

## STEP 3: Dynamic Test Pages

### Obiettivo
Mostrare solo test per periferiche abilitate (Display sempre, Touch solo se CONFIG_BSP_ENABLE_TOUCH).

### Modifiche

#### 3.1 Modificare `create_screen3_tests()`

**Posizione**: Sostituire il loop esistente (linee ~943-949)

**PRIMA**:
```c
    for (uint8_t i = 0; i < TEST_TOOL_TEST_MAX; i++) {
        create_test_tool_card(container, &s_tests[i]);
    }
```

**DOPO** (conditional rendering):
```c
    // Display tests (sempre disponibili)
#ifdef CONFIG_BSP_ENABLE_DISPLAY
    create_test_tool_card(container, &s_tests[0]);  // Pattern Test
    create_test_tool_card(container, &s_tests[1]);  // Color Test
    create_test_tool_card(container, &s_tests[2]);  // Gradient Test
    create_test_tool_card(container, &s_tests[3]);  // Backlight Control
#endif

    // Touch tests (solo se touch abilitato)
#ifdef CONFIG_BSP_ENABLE_TOUCH
    create_test_tool_card(container, &s_tests[4]);  // Multi-Touch Test
    create_test_tool_card(container, &s_tests[5]);  // Calibration Test
    create_test_tool_card(container, &s_tests[6]);  // Gesture Detection
    create_test_tool_card(container, &s_tests[7]);  // Palm Rejection
#endif
```

### Test
1. Config touch disabilitato: `idf.py menuconfig` → BSP → Touch → OFF
2. Compilare: `idf.py build`
3. Verificare: pagina test deve mostrare solo 4 card (display), non 8
4. Riabilitare touch → deve mostrare tutte 8 card

---

## STEP 4: Cleanup e refinement

### 4.1 Aggiungere dichiarazione forward per `show_peripheral_overlay()`

**Posizione**: Dopo le dichiarazioni di funzioni helper (linea ~270, prima di `init_peripheral_list()`)

```c
static void set_peripheral_status(peripheral_info_t *periph);
static void show_peripheral_overlay(peripheral_info_t *periph);  // <-- AGGIUNGERE
```

### 4.2 Gestire cleanup overlay in `lvgl_dashboard_deinit()`

**Posizione**: Dentro `lvgl_dashboard_deinit()`, dopo `if (s_dash.refresh_timer)`

```c
    if (s_dash.refresh_timer) {
        lv_timer_delete(s_dash.refresh_timer);
        s_dash.refresh_timer = NULL;
    }

    // Cleanup overlay se aperto
    if (s_dash.overlay) {
        lv_obj_del(s_dash.overlay);
        s_dash.overlay = NULL;
        s_dash.overlay_periph = NULL;
    }
```

---

## Checklist Completa

### STEP 1: Runtime Status
- [ ] `#include "bsp_board.h"` aggiunto
- [ ] `set_peripheral_status()` usa BSP API
- [ ] Compile OK
- [ ] Status LED Display/Touch/I2C corretti

### STEP 2: Clickable Cards
- [ ] `overlay` e `overlay_periph` aggiunti a `dashboard_state_t`
- [ ] `periph_card_event_cb()` implementato
- [ ] `show_peripheral_overlay()` implementato
- [ ] `overlay_back_btn_cb()` implementato
- [ ] `create_peripheral_card()` rende card clickable
- [ ] Compile OK
- [ ] Click card → overlay funziona
- [ ] Back button → torna a dashboard

### STEP 3: Dynamic Tests
- [ ] `create_screen3_tests()` usa `#ifdef`
- [ ] Compile OK con touch disabled → 4 card
- [ ] Compile OK con touch enabled → 8 card

### STEP 4: Cleanup
- [ ] Forward declaration aggiunta
- [ ] `lvgl_dashboard_deinit()` fa cleanup overlay
- [ ] Compile OK finale
- [ ] Test completo: navigation, overlay, status updates

---

## Risultato Atteso

### Prima (attuale)
- Dashboard statico con 12 card periferiche
- Status sempre OK (hardcoded)
- Card non clickable
- Test page mostra sempre 8 test

### Dopo (implementato)
- ✅ Status dinamico con BSP API (Display/Touch/I2C runtime check)
- ✅ Card periferiche clickable → overlay dettagli
- ✅ Back button da overlay → dashboard
- ✅ Test page dinamica (4 test se touch OFF, 8 se ON)
- ✅ Architettura pulita e testabile

---

## Commit Sequence

```bash
# Step 1
git add main/src/lvgl_dashboard.c
git commit -m "feat(dashboard): add runtime status checks with BSP API

- Use bsp_display_get_handle() for Display status
- Use bsp_touch_get_handle() for Touch status
- Use bsp_i2c_get_bus_handle() for I2C status
- Replace hardcoded OK with handle validity checks"

# Step 2
git add main/src/lvgl_dashboard.c
git commit -m "feat(dashboard): add clickable peripheral cards with overlay

- Add overlay state to dashboard_state_t
- Implement periph_card_event_cb for click handling
- Implement show_peripheral_overlay with fullscreen details
- Add back button navigation to return to dashboard
- Make cards clickable only if enabled and implemented"

# Step 3
git add main/src/lvgl_dashboard.c
git commit -m "feat(dashboard): dynamic test page rendering

- Conditional rendering based on CONFIG_BSP_ENABLE_TOUCH
- Display tests always shown (4 cards)
- Touch tests only if touch enabled (4 additional cards)
- Reduces UI clutter for minimal configs"

# Step 4
git add main/src/lvgl_dashboard.c
git commit -m "refactor(dashboard): cleanup and add overlay management

- Add forward declaration for show_peripheral_overlay
- Add overlay cleanup in lvgl_dashboard_deinit
- Ensure proper resource cleanup on dashboard teardown"
```

---

## Note Implementative

1. **BSP API dependency**: richiede che `bsp_board_init()` sia chiamato prima di `lvgl_dashboard_init()` (già così in main.c)
2. **LVGL lock**: tutte le operazioni UI devono essere dentro `lvgl_port_lock()`/`unlock()` (già gestito)
3. **Overlay z-index**: creato su `lv_scr_act()` quindi appare sopra tileview
4. **Memory**: overlay viene distrutto al click Back, nessun leak
5. **Thread safety**: callback LVGL già in LVGL task context, no race conditions
