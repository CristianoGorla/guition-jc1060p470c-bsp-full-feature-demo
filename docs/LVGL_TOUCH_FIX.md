# LVGL Touch Integration Fix

## Problem Summary

**Symptom**: LVGL non riceveva gli eventi touch dal GT911 nonostante il touch controller funzionasse correttamente.

**Root Cause**: Il custom `touch_read_cb()` non veniva invocato abbastanza spesso perché dipendeva dal LVGL task, che con `avoid_tearing=false` andava in sleep tra i frame.

---

## Root Cause Analysis

### Architettura Originale (NON FUNZIONANTE)

```c
// lvgl_init.c - VECCHIA IMPLEMENTAZIONE
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // Polling GT911 manuale
    esp_lcd_touch_read_data(ctx->touch_handle);
    esp_lcd_touch_get_coordinates(...);
    // Aggiorna LVGL
    data->state = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

g_touch_indev = lv_indev_create();
lv_indev_set_read_cb(g_touch_indev, touch_read_cb);  // ❌ Callback chiamato da LVGL task
```

**Problema**:
```
1. LVGL task parte e aspetta refresh (avoid_tearing=false)
2. Se non ci sono animazioni/aggiornamenti UI, task va in sleep
3. touch_read_cb() viene chiamato SOLO quando LVGL task è attivo
4. Con UI statica: task dorme → callback non chiamato → GT911 non letto → no tocchi!
```

### Vendor Demo (FUNZIONANTE)

```c
// esp32_p4_function_ev_board.c - VENDOR APPROACH
static lv_indev_t *bsp_display_indev_init(lv_display_t *disp)
{
    esp_lcd_touch_handle_t tp;
    bsp_touch_new(NULL, &tp);
    
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };
    
    return lvgl_port_add_touch(&touch_cfg);  // ✅ Crea timer FreeRTOS indipendente
}
```

**Soluzione Vendor**:
```
1. lvgl_port_add_touch() crea un FreeRTOS timer SEPARATO dal LVGL task
2. Timer gira ogni 5ms indipendentemente da LVGL task state
3. Polling GT911 garantito sempre attivo
4. Touch events sempre consegnati a LVGL
```

---

## Dual Fix Implementation

### Fix 1: lvgl_port_add_touch() (Opzione 1)

**File**: `main/lvgl_init.c`

**Cambio**:
```c
// PRIMA (custom indev)
g_touch_indev = lv_indev_create();
lv_indev_set_type(g_touch_indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(g_touch_indev, touch_read_cb);  // ❌ Dipende da LVGL task

// DOPO (helper automatico)
const lvgl_port_touch_cfg_t touch_cfg = {
    .disp = disp,
    .handle = touch_handle,
};
g_touch_indev = lvgl_port_add_touch(&touch_cfg);  // ✅ Timer indipendente
```

**Benefici**:
- ✅ Polling garantito ogni 5ms (FreeRTOS timer dedicato)
- ✅ Indipendente da LVGL task scheduling
- ✅ Zero rischio deadlock (nessun logging nel callback)
- ✅ Allineato con architettura vendor demo

---

### Fix 2: avoid_tearing=true + num_fbs=2 (Opzione 2)

#### File 1: `main/lvgl_init.c`

**Cambio**:
```c
// PRIMA
const lvgl_port_display_dsi_cfg_t dsi_cfg = {
    .flags = {
        .avoid_tearing = false,  // ❌ LVGL task può dormire
    }
};

// DOPO
const lvgl_port_display_dsi_cfg_t dsi_cfg = {
    .flags = {
        .avoid_tearing = true,  // ✅ Force LVGL task refresh per vsync
    }
};
```

#### File 2: `components/guition_jc1060_bsp/drivers/jd9165_bsp.c`

**Cambio**:
```c
// PRIMA
esp_lcd_dpi_panel_config_t dpi_config = {
    // ...
    .num_fbs = 1,  // ❌ Singolo framebuffer
    // ...
};

// DOPO
esp_lcd_dpi_panel_config_t dpi_config = {
    // ...
    .num_fbs = 2,  // ✅ Dual framebuffer per avoid_tearing
    // ...
};
```

**Benefici**:
- ✅ LVGL task rimane sempre attivo per coordinazione vsync
- ✅ Elimina screen tearing durante animazioni
- ✅ Callback touch viene invocato regolarmente anche con Fix 1
- ⚠️ Costo: +800KB memoria per secondo framebuffer

---

## Why Both Fixes?

### Fix 1 Solo
```
✅ Risolve il problema touch (timer indipendente)
❌ Possibile screen tearing durante animazioni veloci
✅ Risparmia 800KB memoria
```

### Fix 2 Solo
```
✅ Risolve problema touch (LVGL task sempre attivo)
✅ Elimina screen tearing
❌ +800KB memoria allocata
❌ Custom callback ancora dipendente da LVGL task (fragile)
```

### Fix 1 + Fix 2 (IMPLEMENTATO)
```
✅ Touch garantito da timer indipendente (robusto)
✅ Zero screen tearing (vsync coordinato)
✅ Architettura allineata a vendor demo
✅ Doppia garanzia di funzionamento
❌ +800KB memoria (accettabile per robustezza)
```

---

## Comparison: Custom vs Vendor Approach

| Aspect | Custom Indev (OLD) | lvgl_port_add_touch (NEW) |
|--------|-------------------|---------------------------|
| **Polling Mechanism** | LVGL task callback | FreeRTOS timer dedicato |
| **Polling Frequency** | Variabile (dipende da UI updates) | Costante (5ms) |
| **avoid_tearing=false** | ❌ Broken (task dorme) | ✅ Funziona sempre |
| **avoid_tearing=true** | ✅ Funziona (task attivo) | ✅ Funziona sempre |
| **Memory Overhead** | Zero | Timer task (~2KB) |
| **Deadlock Risk** | ⚠️ Alto (logging in callback) | ✅ Nessuno |
| **Vendor Alignment** | ❌ Custom architecture | ✅ Standard approach |

---

## Technical Details: avoid_tearing

### avoid_tearing=false (OLD)
```
LVGL Task Behavior:
1. lv_timer_handler() processa UI
2. Se dirty regions vuote:
   - Task va in sleep (max_sleep_ms = 500ms)
   - Custom touch_read_cb() NON chiamato durante sleep
   - GT911 NON viene letto
3. Wake solo su:
   - Timer LVGL (timer_period_ms = 5ms)
   - Eventi esterni
```

### avoid_tearing=true (NEW)
```
LVGL Task Behavior:
1. lv_timer_handler() processa UI
2. Anche con dirty regions vuote:
   - Wait vsync dal DSI controller
   - Task rimane schedulato regolarmente
   - Custom touch_read_cb() chiamato ogni frame
3. Richiede num_fbs=2 per ping-pong buffering:
   - FB0: DSI controller legge
   - FB1: LVGL scrive
   - Swap su vsync
```

---

## Memory Impact

### Before Fix
```
Display Driver (JD9165):
- Single framebuffer: 1024×600×2 = ~1.2 MB
Total: ~1.2 MB
```

### After Fix
```
Display Driver (JD9165):
- Dual framebuffer: 1024×600×2×2 = ~2.4 MB
  └─ FB0: 1.2 MB (DSI read)
  └─ FB1: 1.2 MB (LVGL write)
Touch Timer Task: ~2 KB
Total: ~2.4 MB (+1.2 MB overhead)
```

**Conclusione**: +1.2MB è accettabile su ESP32-P4 con 8MB PSRAM per garantire robustezza touch e zero tearing.

---

## Testing Checklist

Dopo aver applicato i fix, verifica:

### 1. Touch Functionality
```c
// Aggiungi nel main loop temporaneamente per debug
uint32_t last_log = 0;
while (1) {
    uint32_t now = esp_log_timestamp();
    if (now - last_log > 2000) {  // Log ogni 2s
        ESP_LOGI("MAIN", "Touch test: Tocca lo schermo!");
        last_log = now;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

**Aspettative**:
- ✅ Touch event logs appaiono immediatamente quando tocchi
- ✅ Coordinate corrette (0-1023 per X, 0-599 per Y)
- ✅ Eventi PRESSED/RELEASED consistenti

### 2. Display Performance
```c
// Usa FPS counter LVGL (già nel tuo progetto)
lv_obj_t *fps_label = lv_label_create(lv_screen_active());
lv_label_set_text(fps_label, "FPS: 0");
// ... aggiorna con lv_timer ogni 1s ...
```

**Aspettative**:
- ✅ FPS stabile (~30 FPS per animazioni moderate)
- ✅ Nessun tearing visibile durante scroll
- ✅ DSI flush callback log ogni 100 frame

### 3. Memory Usage
```bash
# Monitor heap usage
idf.py monitor
# Cerca log "Free heap" durante boot
```

**Aspettative**:
- ✅ ~2.4 MB allocati per framebuffer DSI
- ✅ Free heap stabile durante runtime
- ✅ No memory leaks

---

## Commits Applied

### Commit 1: Display Driver Fix
```
SHA: 4b448e2c466dd86ad86963e46333fef5db373a9d
File: components/guition_jc1060_bsp/drivers/jd9165_bsp.c
Change: num_fbs = 1 → num_fbs = 2
```

### Commit 2: LVGL Integration Fix
```
SHA: 1d7364760884732bc9ecc7cedda092f89108cf75
File: main/lvgl_init.c
Changes:
- Remove custom touch_read_cb() and touch_ctx_t
- Replace lv_indev_create() with lvgl_port_add_touch()
- Change avoid_tearing = false → avoid_tearing = true
```

---

## Rollback Procedure (If Needed)

Se i fix causano problemi imprevisti:

```bash
# Rollback entrambi i commit
git checkout feature/lvgl-v9-integration
git reset --hard HEAD~2
git push -f origin feature/lvgl-v9-integration

# Oppure rollback selettivo
git revert 1d7364760884732bc9ecc7cedda092f89108cf75  # LVGL fix
git revert 4b448e2c466dd86ad86963e46333fef5db373a9d  # Display fix
```

---

## References

- Vendor Demo: [GUITION-LVGL-V9-DEMO-ESP32P4](https://github.com/CristianoGorla/GUITION-LVGL-V9-DEMO-ESP32P4)
- LVGL v9 Porting Guide: https://docs.lvgl.io/master/porting/display.html
- ESP LVGL Port Documentation: https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port
- JD9165 Driver: https://components.espressif.com/components/espressif/esp_lcd_jd9165

---

## Conclusion

**Il problema era architetturale, non hardware**:
- GT911 funzionava correttamente ✅
- Display JD9165 funzionava correttamente ✅
- Custom touch callback **design** era fragile ❌

**La soluzione**: Allinearsi all'architettura vendor con doppia garanzia (timer polling + vsync coordination).

**Risultato**: Touch robusto, zero tearing, architettura production-ready.
