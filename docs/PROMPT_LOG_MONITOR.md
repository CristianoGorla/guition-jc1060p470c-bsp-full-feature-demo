# Prompt: LVGL On-Screen Log Monitor con Auto-Posizionamento

**Repository:** [CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/tree/feature/lvgl-v9-integration)  
**Branch:** `feature/lvgl-v9-integration`

## Obiettivo

Creare un widget LVGL "Serial Monitor" che intercetta i log ESP-IDF e li mostra in tempo reale sul display touch. Il monitor si posiziona automaticamente in un angolo dello schermo evitando conflitti con il performance monitor LVGL nativo.

**Features principali:**
- Intercetta ESP_LOGx() in tempo reale
- Widget angolare scrollabile (400x300 pixel)
- Filtro per livello log (ERROR/WARN/INFO/DEBUG/VERBOSE)
- Auto-scroll, pause, clear
- Gesture 3-dita per nascondere/mostrare
- Save log su SD card
- Auto-posizionamento intelligente (evita perf monitor LVGL)

---

## Task 1: Creazione Header Log Monitor

Crea il file: **`main/include/lvgl_log_monitor.h`**

```c
/**
 * @file lvgl_log_monitor.h
 * @brief LVGL on-screen serial monitor widget
 * 
 * Displays ESP-IDF log messages in real-time on the touch display.
 * Automatically avoids LVGL performance monitor position.
 * 
 * Position is configured via menuconfig and defaults intelligently:
 *  - If LVGL perf monitor disabled → BOTTOM_RIGHT
 *  - If LVGL perf monitor in BOTTOM_RIGHT → BOTTOM_LEFT
 *  - Otherwise → BOTTOM_RIGHT
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log monitor filter levels
 */
typedef enum {
    LOG_MONITOR_FILTER_ERROR = 0,     ///< Show only ERROR
    LOG_MONITOR_FILTER_WARN,          ///< Show ERROR + WARN
    LOG_MONITOR_FILTER_INFO,          ///< Show ERROR + WARN + INFO (default)
    LOG_MONITOR_FILTER_DEBUG,         ///< Show ERROR + WARN + INFO + DEBUG
    LOG_MONITOR_FILTER_VERBOSE        ///< Show all including VERBOSE
} log_monitor_filter_t;

/**
 * @brief Initialize log monitor widget
 * 
 * Creates LVGL corner widget and hooks into ESP-IDF log system.
 * Position and size are configured via menuconfig.
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - ESP_ERR_INVALID_STATE if already initialized
 *      - ESP_ERR_NOT_SUPPORTED if CONFIG_APP_ENABLE_LOG_MONITOR disabled
 * 
 * @note Must be called after LVGL initialization
 * @note Automatically intercepts ESP_LOGx() calls
 * @note Call from main task AFTER lvgl_port_init()
 */
esp_err_t lvgl_log_monitor_init(void);

/**
 * @brief Deinitialize log monitor
 * 
 * Removes widget and restores default ESP-IDF log handler.
 * 
 * @return ESP_OK on success
 */
esp_err_t lvgl_log_monitor_deinit(void);

/**
 * @brief Show/hide log monitor widget
 * 
 * @param visible true to show, false to hide
 * 
 * @note When hidden, logs continue to be captured in background buffer
 */
void lvgl_log_monitor_set_visible(bool visible);

/**
 * @brief Check if log monitor is visible
 * 
 * @return true if visible, false otherwise
 */
bool lvgl_log_monitor_is_visible(void);

/**
 * @brief Toggle visibility (show if hidden, hide if shown)
 */
void lvgl_log_monitor_toggle(void);

/**
 * @brief Set log level filter
 * 
 * @param filter New filter level
 */
void lvgl_log_monitor_set_filter(log_monitor_filter_t filter);

/**
 * @brief Get current filter level
 * 
 * @return Current filter
 */
log_monitor_filter_t lvgl_log_monitor_get_filter(void);

/**
 * @brief Enable/disable auto-scroll
 * 
 * @param enable true to enable auto-scroll to latest log
 */
void lvgl_log_monitor_set_autoscroll(bool enable);

/**
 * @brief Check if auto-scroll is enabled
 * 
 * @return true if enabled, false otherwise
 */
bool lvgl_log_monitor_get_autoscroll(void);

/**
 * @brief Clear log buffer and display
 */
void lvgl_log_monitor_clear(void);

/**
 * @brief Save log buffer to file
 * 
 * @param filepath Path to save file (e.g., "/sdcard/logs.txt")
 * @return 
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if path is NULL
 *      - ESP_FAIL if file write fails
 * 
 * @note Requires SD card to be mounted at /sdcard
 */
esp_err_t lvgl_log_monitor_save_to_file(const char *filepath);

/**
 * @brief Get log monitor widget object
 * 
 * @return LVGL object pointer, or NULL if not initialized
 * 
 * @note Use to add custom event handlers or styling
 */
lv_obj_t* lvgl_log_monitor_get_obj(void);

#ifdef __cplusplus
}
#endif
```

---

## Task 2: Implementazione Log Monitor

Crea il file: **`main/src/lvgl_log_monitor.c`**

⚠️ **NOTA**: L'implementazione completa è molto lunga (~600 righe). Vedere il codice completo nel messaggio precedente della conversazione o riferimento commit.

**Struttura chiave**:
- `log_monitor_t` struct con stato monitor
- `log_msg_t` struct per queue cross-task
- `log_monitor_vprintf()` custom handler che intercetta ESP_LOGx()
- `log_update_timer_cb()` timer LVGL che processa queue
- UI widgets: container, header, buttons, textarea
- Color-coded log levels con LVGL recolor

---

## Task 3: Aggiornamento CMakeLists.txt Main

Nel file **`main/CMakeLists.txt`**, aggiungi `lvgl_log_monitor.c`:

```cmake
set(srcs
    "main.c"
    "lvgl_demo.c"
    "lvgl_init.c"
    "src/lvgl_log_monitor.c"    # <-- AGGIUNGI
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES 
        esp_lvgl_port
        lvgl
        guition_jc1060_bsp
)
```

---

## Task 4: Opzioni Kconfig

Nel file **`components/guition_jc1060_bsp/Kconfig.projbuild`**, aggiungi nel menu `"Debug Logging (Development)"`:

```kconfig
    comment "──────────────────────────────────────────────────────────────"
    comment "On-Screen Log Monitor"
    comment "──────────────────────────────────────────────────────────────"
    depends on BSP_ENABLE_DEBUG_MODE && BSP_ENABLE_LVGL
    
    config APP_ENABLE_LOG_MONITOR
        bool "Enable on-screen log monitor widget"
        default y
        depends on BSP_ENABLE_DEBUG_MODE && BSP_ENABLE_LVGL
        help
            Display ESP-IDF log messages in real-time on touch display.
    
    choice APP_LOG_MONITOR_POSITION
        prompt "Log monitor corner position"
        default APP_LOG_MONITOR_POS_BOTTOM_RIGHT if !LV_USE_PERF_MONITOR
        default APP_LOG_MONITOR_POS_BOTTOM_LEFT if LV_USE_PERF_MONITOR && LV_USE_PERF_MONITOR_POS = 8
        default APP_LOG_MONITOR_POS_BOTTOM_RIGHT
        depends on APP_ENABLE_LOG_MONITOR
        
        config APP_LOG_MONITOR_POS_TOP_LEFT
            bool "Top-Left corner"
            depends on !LV_USE_PERF_MONITOR || LV_USE_PERF_MONITOR_POS != 0
        
        config APP_LOG_MONITOR_POS_TOP_RIGHT
            bool "Top-Right corner"
            depends on !LV_USE_PERF_MONITOR || LV_USE_PERF_MONITOR_POS != 2
        
        config APP_LOG_MONITOR_POS_BOTTOM_LEFT
            bool "Bottom-Left corner"
            depends on !LV_USE_PERF_MONITOR || LV_USE_PERF_MONITOR_POS != 6
        
        config APP_LOG_MONITOR_POS_BOTTOM_RIGHT
            bool "Bottom-Right corner"
            depends on !LV_USE_PERF_MONITOR || LV_USE_PERF_MONITOR_POS != 8
    endchoice
    
    config APP_LOG_MONITOR_WIDTH
        int "Log monitor width (pixels)"
        default 400
        range 250 600
        depends on APP_ENABLE_LOG_MONITOR
    
    config APP_LOG_MONITOR_HEIGHT
        int "Log monitor height (pixels)"
        default 300
        range 150 500
        depends on APP_ENABLE_LOG_MONITOR
    
    config APP_LOG_MONITOR_MAX_LINES
        int "Maximum log lines in buffer"
        default 100
        range 50 500
        depends on APP_ENABLE_LOG_MONITOR
    
    choice APP_LOG_MONITOR_DEFAULT_FILTER
        prompt "Default log level filter"
        default APP_LOG_MONITOR_FILTER_INFO
        depends on APP_ENABLE_LOG_MONITOR
        
        config APP_LOG_MONITOR_FILTER_ERROR
            bool "ERROR only"
        config APP_LOG_MONITOR_FILTER_WARN
            bool "WARN and above"
        config APP_LOG_MONITOR_FILTER_INFO
            bool "INFO and above"
        config APP_LOG_MONITOR_FILTER_DEBUG
            bool "DEBUG and above"
        config APP_LOG_MONITOR_FILTER_VERBOSE
            bool "VERBOSE (all)"
    endchoice
    
    config APP_LOG_MONITOR_AUTOSCROLL
        bool "Enable auto-scroll by default"
        default y
        depends on APP_ENABLE_LOG_MONITOR
    
    config APP_LOG_MONITOR_SHOW_TIMESTAMP
        bool "Show timestamp in logs"
        default y
        depends on APP_ENABLE_LOG_MONITOR
    
    config APP_LOG_MONITOR_GESTURE_HIDE
        bool "Enable 3-finger swipe to hide/show"
        default n
        depends on APP_ENABLE_LOG_MONITOR

endmenu # Debug Logging
```

---

## Task 5: Integrazione in main.c

```c
#ifdef CONFIG_APP_ENABLE_LOG_MONITOR
#include "lvgl_log_monitor.h"
#endif

// In app_main() dopo lvgl_init():

#ifdef CONFIG_APP_ENABLE_LOG_MONITOR
    ESP_LOGI(TAG, "=== Log Monitor ===");
    ret = lvgl_log_monitor_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OK] Log monitor active\n");
    } else {
        ESP_LOGW(TAG, "[FAIL] Log monitor init failed\n");
    }
#endif
```

---

## Validazione

### Test 1: Default Positioning
```bash
# Con perf monitor OFF → log monitor in BOTTOM_RIGHT
idf.py menuconfig
# Component config → LVGL → [ ] Show CPU usage
# Guition → Debug → [*] Log monitor
idf.py build flash monitor
# Verifica: log monitor in angolo basso-destra
```

### Test 2: Auto-Avoidance
```bash
# Con perf monitor in BOTTOM_RIGHT → log monitor auto in BOTTOM_LEFT
idf.py menuconfig
# Component config → LVGL → [*] Show CPU usage (Bottom right)
# Guition → Debug → [*] Log monitor
# Verifica in menuconfig: default è BOTTOM_LEFT, BOTTOM_RIGHT nascosto
idf.py build flash monitor
# Verifica: perf monitor a destra, log monitor a sinistra
```

### Test 3: Runtime Features
- Tap pulsante "INFO" → cicla filtri
- Tap "||" → pausa auto-scroll
- Tap "Clear" → svuota buffer
- Tap "X" → nascondi monitor
- Verifica log colorati (rosso ERROR, arancione WARN, ecc.)

---

## Features Implementate

✅ Intercettazione log ESP-IDF via `esp_log_set_vprintf()`  
✅ Queue thread-safe per cross-task messaging  
✅ Widget angolare scrollabile (400x300 default)  
✅ 4 posizioni configurabili con auto-avoidance  
✅ Default intelligente (evita perf monitor)  
✅ Filtro livelli log runtime  
✅ Color-coded logs (5 colori)  
✅ Auto-scroll con toggle  
✅ Pulsanti: Clear, Filter, Pause, Hide  
✅ Buffer circolare configurabile  
✅ Save to SD card  
✅ Gesture 3-dita opzionale  
✅ Log UART parallelo non bloccato  

---

## Note Implementazione

- **Thread-safety**: Queue + mutex per log safe
- **Performance**: Max 5 log/tick, non blocca rendering
- **Memory**: ~10KB per 100 linee (configurabile)
- **Compatibilità**: Funziona con qualsiasi UI LVGL esistente

**Commit**: `feat(lvgl): add on-screen log monitor with auto-positioning`
