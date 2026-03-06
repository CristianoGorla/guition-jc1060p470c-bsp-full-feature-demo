# Prompt: LVGL Serial Log Monitor UI

**Repository:** [CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/tree/feature/lvgl-v9-integration)  
**Branch:** `feature/lvgl-v9-integration`

## Obiettivo

Creare un **LVGL-based serial log monitor** che cattura i log ESP-IDF (ESP_LOGI, ESP_LOGW, ESP_LOGE, ecc.) e li visualizza in una finestra scrollabile con:
- Filtro per tag e livello log
- Color coding per severità
- Auto-scroll
- Pause/Resume
- Clear buffer
- Export a file (SD card o UART)

**Display**: 1024x600 pixels  
**Framework**: LVGL v9.2.2  
**Target**: ESP32-P4 con ESP-IDF v5.5.3

---

## Architettura

### 1. Custom vprintf Hook

Esp-IDF permette di registrare un custom `vprintf` handler per intercettare tutti i log prima che vengano inviati alla UART:

```c
#include "esp_log.h"

static int custom_vprintf(const char *fmt, va_list args)
{
    // 1. Format string into buffer
    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    
    // 2. Send to ring buffer for LVGL UI
    log_monitor_add_line(buffer, len);
    
    // 3. Forward to default UART output
    return vprintf(fmt, args);
}

void log_monitor_install_hook(void)
{
    esp_log_set_vprintf(custom_vprintf);
}
```

### 2. Ring Buffer per Log Lines

Usa un ring buffer thread-safe per memorizzare le ultime N righe di log:

```c
#define LOG_BUFFER_SIZE 1000  // Max 1000 lines

typedef struct {
    char text[256];
    uint32_t timestamp;
    esp_log_level_t level;
    char tag[16];
} log_line_t;

static log_line_t s_log_buffer[LOG_BUFFER_SIZE];
static uint16_t s_log_head = 0;
static uint16_t s_log_tail = 0;
static SemaphoreHandle_t s_log_mutex;
```

### 3. LVGL UI Components

- **Textarea scrollabile**: Mostra log lines con auto-scroll
- **Filter bar**: Dropdown per tag + checkboxes per livelli
- **Control buttons**: Pause/Resume, Clear, Export
- **Status bar**: Conta righe, RAM usage, scroll position

---

## Task 1: Creazione Header

Crea il file: **`main/include/lvgl_log_monitor.h`**

```c
/**
 * @file lvgl_log_monitor.h
 * @brief LVGL-based serial log monitor with ESP-IDF log interception
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log monitor configuration
 */
typedef struct {
    uint16_t max_lines;              ///< Max lines in ring buffer (default 1000)
    bool auto_scroll;                ///< Auto-scroll to bottom (default true)
    bool show_timestamp;             ///< Show timestamps (default true)
    bool show_tag;                   ///< Show log tags (default true)
    bool color_by_level;             ///< Color code by log level (default true)
    esp_log_level_t min_level;       ///< Minimum log level to display (default ESP_LOG_VERBOSE)
} log_monitor_config_t;

/**
 * @brief Default log monitor configuration
 */
#define LOG_MONITOR_CONFIG_DEFAULT() { \
    .max_lines = 1000, \
    .auto_scroll = true, \
    .show_timestamp = true, \
    .show_tag = true, \
    .color_by_level = true, \
    .min_level = ESP_LOG_VERBOSE, \
}

/**
 * @brief Initialize log monitor
 * 
 * Creates LVGL UI and installs ESP-IDF log hook.
 * 
 * @param config Configuration (NULL for defaults)
 * @return 
 *      - ESP_OK on success
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - ESP_ERR_INVALID_STATE if already initialized
 * 
 * @note Must be called after LVGL initialization
 */
esp_err_t lvgl_log_monitor_init(const log_monitor_config_t *config);

/**
 * @brief Deinitialize log monitor
 * 
 * @return ESP_OK on success
 */
esp_err_t lvgl_log_monitor_deinit(void);

/**
 * @brief Show/hide log monitor window
 * 
 * @param visible true to show, false to hide
 */
void lvgl_log_monitor_set_visible(bool visible);

/**
 * @brief Check if log monitor is visible
 * 
 * @return true if visible, false otherwise
 */
bool lvgl_log_monitor_is_visible(void);

/**
 * @brief Pause/resume log capture
 * 
 * @param paused true to pause, false to resume
 */
void lvgl_log_monitor_set_paused(bool paused);

/**
 * @brief Clear log buffer
 */
void lvgl_log_monitor_clear(void);

/**
 * @brief Export logs to file
 * 
 * @param filepath Path to output file (e.g., "/sdcard/logs.txt")
 * @return 
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if SD card not mounted
 *      - ESP_FAIL on write error
 */
esp_err_t lvgl_log_monitor_export(const char *filepath);

/**
 * @brief Set log level filter
 * 
 * @param min_level Minimum log level to display
 */
void lvgl_log_monitor_set_level_filter(esp_log_level_t min_level);

/**
 * @brief Set tag filter
 * 
 * @param tag Tag to filter (NULL to show all)
 */
void lvgl_log_monitor_set_tag_filter(const char *tag);

#ifdef __cplusplus
}
#endif
```

---

[Rest of original content continues...]
