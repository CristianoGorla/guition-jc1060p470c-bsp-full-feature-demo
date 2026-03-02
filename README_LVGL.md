# LVGL 9.2.2 Support for Guition JC1060P470C

## Overview

This BSP provides full integration of **LVGL v9.2.2** (Light and Versatile Graphics Library) for the Guition JC1060P470C board with ESP32-P4, featuring:

- **Display**: JD9165 1024x600 MIPI-DSI controller
- **Touch**: GT911 capacitive multi-touch (up to 5 points)
- **Graphics**: Hardware-accelerated rendering with DMA
- **Configuration**: 100% Kconfig-driven (no hardcoded defines)

## Features

✅ **LVGL 9.2.2** - Latest version with modern C API  
✅ **Kconfig Integration** - All settings via `idf.py menuconfig`  
✅ **Flexible Buffering** - Single/double buffer, DMA/SPIRAM options  
✅ **Software Rotation** - 0°/90°/180°/270° runtime rotation  
✅ **Touch Support** - GT911 multi-touch integrated with LVGL  
✅ **Demo Apps** - Simple, Widgets, Benchmark, Stress test  
✅ **Thread-Safe** - Mutex-protected LVGL operations  
✅ **Performance Stats** - FPS counter and memory monitoring  

---

## Quick Start

### 1. Enable LVGL in Menuconfig

```bash
idf.py menuconfig
```

Navigate to:
```
Guition JC1060P470C Board Configuration
  └─> LVGL Graphics Library Configuration
       └─> [*] Enable LVGL Graphics Library
```

### 2. Configure LVGL Settings

#### Display Buffer Configuration

```
LVGL Display Configuration
  ├─> Display buffer size (lines): 100        # 100 lines = ~246KB per buffer
  ├─> [*] Enable double buffering             # Smoother rendering
  ├─> [*] Use DMA-capable buffers             # Faster transfers
  └─> [ ] Use SPIRAM for buffers              # Save internal RAM (slower)
```

**Buffer Size Recommendations:**
- **50 lines** (~123KB) - Minimal memory, slower
- **100 lines** (~246KB) - **Recommended balance**
- **200 lines** (~492KB) - Smoother, more memory
- **300 lines** (~738KB) - Maximum smoothness

#### Rotation Configuration

```
LVGL Display Configuration
  ├─> [*] Enable software rotation
  └─> Initial display rotation
       ├─> ( ) 0 degrees (Landscape)
       ├─> (*) 90 degrees (Portrait)           # Default
       ├─> ( ) 180 degrees (Landscape inverted)
       └─> ( ) 270 degrees (Portrait inverted)
```

#### Touch Configuration

```
LVGL Touch Configuration
  └─> [*] Enable touch input for LVGL         # Requires BSP_ENABLE_TOUCH
```

#### Demo Applications

```
LVGL Demo Applications
  ├─> [*] Enable LVGL demo at startup
  └─> Demo type
       ├─> (*) Simple test screen              # Minimal memory
       ├─> ( ) LVGL Widgets demo               # ~200KB+ required
       ├─> ( ) LVGL Benchmark                  # Performance test
       └─> ( ) LVGL Stress test                # Stability test
```

### 3. Add LVGL to Your Code

#### Basic Initialization

```c
#include "bsp_lvgl.h"
#include "lvgl_demo.h"

void app_main(void)
{
    // Initialize LVGL with default config from menuconfig
    lv_display_t *display = bsp_lvgl_init_default();
    if (display == NULL) {
        ESP_LOGE(TAG, "LVGL initialization failed");
        return;
    }
    
    // Auto-run demo if enabled in menuconfig
    lvgl_demo_run_from_config();
    
    // Your custom UI code here...
}
```

#### Custom Configuration

```c
#include "bsp_lvgl.h"

void app_main(void)
{
    // Override menuconfig with custom settings
    bsp_lvgl_config_t config = BSP_LVGL_CONFIG_DEFAULT();
    config.buffer.buffer_lines = 150;           // Custom buffer size
    config.buffer.double_buffer = true;
    config.rotation.initial_rotation = 90;      // Portrait mode
    
    lv_display_t *display = bsp_lvgl_init(&config);
    
    // Create your UI
    if (bsp_lvgl_lock(1000)) {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "Hello LVGL!");
        lv_obj_center(label);
        bsp_lvgl_unlock();
    }
}
```

---

## API Reference

### Display Functions

```c
// Initialize LVGL (reads config from menuconfig)
lv_display_t *bsp_lvgl_init_default(void);

// Initialize with custom config
lv_display_t *bsp_lvgl_init(const bsp_lvgl_config_t *config);

// Deinitialize and free resources
esp_err_t bsp_lvgl_deinit(void);

// Get display object
lv_display_t *bsp_lvgl_get_display(void);

// Get touch input device
lv_indev_t *bsp_lvgl_get_touch_input(void);
```

### Rotation Functions

```c
// Set display rotation (requires CONFIG_BSP_LVGL_ENABLE_SW_ROTATE)
esp_err_t bsp_lvgl_set_rotation(bsp_lvgl_rotation_t rotation);

// Get current rotation (0, 90, 180, or 270)
int bsp_lvgl_get_rotation(void);

// Rotation values
typedef enum {
    BSP_LVGL_ROTATION_0   = 0,    // Landscape
    BSP_LVGL_ROTATION_90  = 90,   // Portrait
    BSP_LVGL_ROTATION_180 = 180,  // Landscape inverted
    BSP_LVGL_ROTATION_270 = 270   // Portrait inverted
} bsp_lvgl_rotation_t;
```

### Thread-Safe Operations

```c
// Lock LVGL for thread-safe access
bool bsp_lvgl_lock(uint32_t timeout_ms);

// Unlock LVGL
void bsp_lvgl_unlock(void);

// Example usage
if (bsp_lvgl_lock(-1)) {  // Wait forever
    lv_obj_set_pos(my_obj, 100, 50);
    lv_label_set_text(my_label, "Updated!");
    bsp_lvgl_unlock();
}
```

### Demo Functions

```c
// Run demo from menuconfig selection
esp_err_t lvgl_demo_run_from_config(void);

// Run specific demo
esp_err_t lvgl_demo_run(lvgl_demo_type_t demo);
esp_err_t lvgl_demo_simple(void);
esp_err_t lvgl_demo_widgets(void);
esp_err_t lvgl_demo_benchmark(void);
esp_err_t lvgl_demo_stress(void);

// Stop demo
esp_err_t lvgl_demo_stop(void);
```

### Statistics and Debugging

```c
// Get buffer size in bytes
size_t bsp_lvgl_get_buffer_size(void);

// Print configuration and memory stats
void bsp_lvgl_print_stats(void);
```

---

## Code Examples

### Example 1: Simple Label

```c
#include "bsp_lvgl.h"

void create_simple_ui(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Guition JC1060P470C");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_center(label);
    
    bsp_lvgl_unlock();
}
```

### Example 2: Button with Callback

```c
static void button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI("UI", "Button clicked!");
    }
}

void create_button_ui(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    
    lv_obj_t *btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, 200, 80);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Click Me!");
    lv_obj_center(label);
    
    bsp_lvgl_unlock();
}
```

### Example 3: Change Rotation at Runtime

```c
void rotate_display(void)
{
    int current = bsp_lvgl_get_rotation();
    int next = (current + 90) % 360;
    
    bsp_lvgl_rotation_t rotation;
    switch (next) {
        case 0:   rotation = BSP_LVGL_ROTATION_0; break;
        case 90:  rotation = BSP_LVGL_ROTATION_90; break;
        case 180: rotation = BSP_LVGL_ROTATION_180; break;
        case 270: rotation = BSP_LVGL_ROTATION_270; break;
    }
    
    if (bsp_lvgl_set_rotation(rotation) == ESP_OK) {
        ESP_LOGI("UI", "Rotated to %d degrees", next);
    }
}
```

### Example 4: Custom Task Integration

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void ui_update_task(void *arg)
{
    int counter = 0;
    lv_obj_t *label = (lv_obj_t *)arg;
    
    while (1) {
        if (bsp_lvgl_lock(100)) {
            lv_label_set_text_fmt(label, "Count: %d", counter++);
            bsp_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    bsp_lvgl_init_default();
    
    lv_obj_t *label;
    if (bsp_lvgl_lock(1000)) {
        label = lv_label_create(lv_screen_active());
        lv_obj_center(label);
        bsp_lvgl_unlock();
    }
    
    xTaskCreate(ui_update_task, "ui_task", 4096, label, 5, NULL);
}
```

---

## Performance Tuning

### Memory Optimization

#### Option 1: SPIRAM Buffers (Save Internal RAM)
```
LVGL Display Configuration
  └─> [*] Use SPIRAM for buffers
```
**Pros**: Saves ~500KB internal RAM  
**Cons**: ~20% slower rendering  
**Use when**: RAM-constrained, 30+ FPS acceptable

#### Option 2: DMA Buffers (Best Performance)
```
LVGL Display Configuration
  └─> [*] Use DMA-capable buffers
```
**Pros**: Fastest rendering (50+ FPS)  
**Cons**: Uses internal RAM  
**Use when**: Performance critical

### Buffer Size Trade-offs

| Buffer Lines | Memory (x2 for double) | FPS Range | Use Case |
|--------------|------------------------|-----------|----------|
| 50           | ~123 KB                | 25-35     | Minimal memory |
| **100**      | **~246 KB**            | **40-55** | **Recommended** |
| 150          | ~369 KB                | 45-60     | Smooth animations |
| 200          | ~492 KB                | 50-65     | Maximum smoothness |

### Display Settings for Maximum FPS

```c
bsp_lvgl_config_t config = BSP_LVGL_CONFIG_DEFAULT();
config.buffer.buffer_lines = 150;          // Larger buffers
config.buffer.double_buffer = true;        // Enable double buffering
config.buffer.use_dma = true;              // DMA transfers
config.buffer.use_spiram = false;          // Use internal RAM
```

**Expected Performance:**
- Simple UI: 60+ FPS
- Complex animations: 45-60 FPS
- Benchmark demo: 50-55 FPS average

---

## Troubleshooting

### 🚨 **Issue: Black Screen (Backlight ON, Display Black)**

**Symptoms:**
- Backlight is ON (screen glowing)
- Display shows solid black (no content)
- Hardware BSP logs show success:
  ```
  I (1709) BSP_JD9165: Display initialized successfully
  I (1794) BSP_GT911: GT911 initialized
  I (1797) BSP: ✓ BSP Ready
  ```
- **Missing** LVGL initialization logs:
  ```
  I (XXXX) bsp_lvgl: ========================================
  I (XXXX) bsp_lvgl: Initializing LVGL v9.2.2
  I (XXXX) bsp_lvgl: LVGL initialization complete!
  ```

**Root Cause Analysis:**

1. **LVGL not initialized in main.c** (most common)
   - `main.c` missing `bsp_lvgl_init_default()` call
   - Code tries to create UI without LVGL context

2. **CONFIG_BSP_ENABLE_LVGL=n** in sdkconfig
   - LVGL code compiled out via `#ifdef`
   - Check: `grep CONFIG_BSP_ENABLE_LVGL sdkconfig`

3. **Memory allocation failure** (silent fail)
   - Buffer too large for available RAM
   - No error log if allocation fails early

**Fix Procedure:**

**Step 1: Enable LVGL in menuconfig**
```bash
idf.py menuconfig
# Navigate to:
# Guition JC1060P470C Board Configuration
#   └─> LVGL Graphics Library Configuration
#        └─> [*] Enable LVGL Graphics Library

# Save and exit
idf.py fullclean build
```

**Step 2: Verify main.c has initialization**

Your `main.c` **MUST** include:
```c
#include "bsp_lvgl.h"
#include "lvgl_demo.h"

void app_main(void)
{
    // ... BSP init ...
    bsp_board_init();
    
    // === ADD THIS BLOCK ===
#ifdef CONFIG_BSP_ENABLE_LVGL
    ESP_LOGI(TAG, "=== LVGL Initialization ===");
    lv_display_t *display = bsp_lvgl_init_default();
    if (display == NULL) {
        ESP_LOGE(TAG, "❌ LVGL init FAILED");
        // Check memory
        ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Free DMA: %lu", heap_caps_get_free_size(MALLOC_CAP_DMA));
    } else {
        ESP_LOGI(TAG, "✅ LVGL initialized");
        
        // Option A: Auto-run demo from menuconfig
        lvgl_demo_run_from_config();
        
        // Option B: Manual UI
        // create_my_ui();
    }
#endif
    // === END BLOCK ===
    
    // ... rest of code ...
}
```

**Step 3: Test with minimal config**
```bash
idf.py menuconfig
# LVGL Display Configuration:
#   Buffer size: 50 lines
#   [*] Enable double buffering: NO
#   [*] Use SPIRAM for buffers: YES
# LVGL Demo Applications:
#   [*] Enable LVGL demo at startup: YES
#   Demo type: Simple test screen

idf.py build flash monitor
```

**Expected SUCCESS logs:**
```
I (1797) BSP: ✓ BSP Ready
I (1798) GUITION_MAIN: === LVGL Initialization ===
I (1820) bsp_lvgl: ========================================
I (1820) bsp_lvgl: Initializing LVGL v9.2.2
I (1821) bsp_lvgl: Display: JD9165 (1024x600)
I (1821) bsp_lvgl: Buffer: 50 lines (single, SPIRAM)
I (1895) bsp_lvgl: ✅ LVGL initialization complete!
I (1896) GUITION_MAIN: ✅ LVGL initialized
I (1897) lvgl_demo: Starting simple LVGL demo
I (1920) lvgl_demo: Simple demo started successfully
```

**Screen should show:**
- Blue background (#003a57)
- White title "Guition JC1060P470C"
- Green subtitle "LVGL 9.2.2 Demo"
- FPS counter (top-left)
- Touch test area (bottom)

**If still black after fix:**

Check CMakeLists.txt includes LVGL sources:
```cmake
# components/guition_jc1060_bsp/CMakeLists.txt
set(srcs
    "src/bsp_board.c"
    "src/bsp_display.c"
    "src/bsp_touch.c"
    "src/bsp_lvgl.c"        # ← MUST BE HERE
)

idf_component_register(
    SRCS ${srcs}
    REQUIRES driver esp_lcd esp_lcd_touch_gt911 
             esp_lvgl_port lvgl      # ← MUST INCLUDE
)
```

Rebuild:
```bash
idf.py fullclean
idf.py build
```

---

### Issue: LVGL doesn't initialize

**Check:**
1. Display enabled: `CONFIG_BSP_ENABLE_DISPLAY=y`
2. LVGL enabled: `CONFIG_BSP_ENABLE_LVGL=y`
3. Sufficient heap memory (check `esp_get_free_heap_size()`)

**Solution:**
```bash
idf.py menuconfig
# Enable: Guition JC1060P470C Board Configuration -> Hardware Peripherals -> Display
# Enable: Guition JC1060P470C Board Configuration -> LVGL Graphics Library
```

### Issue: Low FPS / Slow rendering

**Causes:**
- SPIRAM buffers (slower than internal RAM)
- Small buffer size (<50 lines)
- Single buffering
- Complex UI with many objects

**Solutions:**
1. Enable DMA buffers:
   ```
   LVGL Display Configuration -> [*] Use DMA-capable buffers
   ```

2. Increase buffer size:
   ```
   LVGL Display Configuration -> Display buffer size (lines): 150
   ```

3. Enable double buffering:
   ```
   LVGL Display Configuration -> [*] Enable double buffering
   ```

### Issue: Touch not working

**Check:**
1. I2C bus enabled: `CONFIG_BSP_ENABLE_I2C_BUS=y`
2. Touch enabled: `CONFIG_BSP_ENABLE_TOUCH=y`
3. LVGL touch enabled: `CONFIG_BSP_LVGL_TOUCH_ENABLE=y`

**Test touch manually:**
```c
esp_lcd_touch_handle_t touch;
bsp_touch_new(NULL, &touch);

uint16_t x[1], y[1];
uint8_t count;
esp_lcd_touch_read_data(touch);
bool pressed = esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 1);
ESP_LOGI(TAG, "Touch: %s at (%d, %d)", pressed ? "YES" : "NO", x[0], y[0]);
```

### Issue: Display rotation not working

**Check:**
```
LVGL Display Configuration -> [*] Enable software rotation
```

**Verify in code:**
```c
ESP_LOGI(TAG, "SW Rotate enabled: %d", CONFIG_BSP_LVGL_ENABLE_SW_ROTATE);
ESP_LOGI(TAG, "Current rotation: %d", bsp_lvgl_get_rotation());
```

### Issue: Out of memory errors

**Symptoms:**
```
E (12345) bsp_lvgl: Failed to allocate LVGL buffer 1 (245760 bytes)
```

**Solutions:**
1. Use SPIRAM:
   ```
   LVGL Display Configuration -> [*] Use SPIRAM for buffers
   ```

2. Reduce buffer size:
   ```
   LVGL Display Configuration -> Display buffer size (lines): 50
   ```

3. Disable double buffering:
   ```
   LVGL Display Configuration -> [ ] Enable double buffering
   ```

4. Check available memory:
   ```c
   ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
   ESP_LOGI(TAG, "Free SPIRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
   ```

### Issue: Demo not running

**Check:**
```
LVGL Demo Applications -> [*] Enable LVGL demo at startup
LVGL Demo Applications -> Demo type -> (*) Simple test screen
```

**Manually run demo:**
```c
#include "lvgl_demo.h"

void app_main(void) {
    bsp_lvgl_init_default();
    lvgl_demo_simple();  // Force simple demo
}
```

---

## Configuration Reference

All LVGL settings are in:
```
idf.py menuconfig
  └─> Guition JC1060P470C Board Configuration
       └─> LVGL Graphics Library Configuration
```

### Kconfig Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_BSP_ENABLE_LVGL` | `n` | Master enable for LVGL |
| `CONFIG_BSP_LVGL_BUFFER_LINES` | `100` | Buffer height in lines |
| `CONFIG_BSP_LVGL_DOUBLE_BUFFER` | `y` | Enable double buffering |
| `CONFIG_BSP_LVGL_USE_DMA_BUFFER` | `y` | Use DMA-capable memory |
| `CONFIG_BSP_LVGL_USE_SPIRAM_BUFFER` | `n` | Use SPIRAM for buffers |
| `CONFIG_BSP_LVGL_ENABLE_SW_ROTATE` | `y` | Enable software rotation |
| `CONFIG_BSP_LVGL_ROTATION_DEGREE` | `0` | Initial rotation (0/90/180/270) |
| `CONFIG_BSP_LVGL_TOUCH_ENABLE` | `y` | Enable touch integration |
| `CONFIG_BSP_LVGL_ENABLE_DEMO` | `n` | Auto-run demo on startup |
| `CONFIG_BSP_LVGL_DEMO_TYPE` | `SIMPLE` | Demo type (SIMPLE/WIDGETS/BENCHMARK/STRESS) |

### Accessing Config in Code

```c
#ifdef CONFIG_BSP_ENABLE_LVGL
    // LVGL is enabled
    int buffer_lines = CONFIG_BSP_LVGL_BUFFER_LINES;
    bool double_buf = CONFIG_BSP_LVGL_DOUBLE_BUFFER;
    int rotation = CONFIG_BSP_LVGL_ROTATION_DEGREE;
#endif
```

---

## Best Practices

### 1. Always Use Lock/Unlock for Thread Safety

❌ **Wrong:**
```c
void my_task(void *arg) {
    lv_label_set_text(label, "Hello");  // NOT THREAD-SAFE!
}
```

✅ **Correct:**
```c
void my_task(void *arg) {
    if (bsp_lvgl_lock(1000)) {
        lv_label_set_text(label, "Hello");
        bsp_lvgl_unlock();
    }
}
```

### 2. Minimize Lock Duration

❌ **Wrong:**
```c
if (bsp_lvgl_lock(-1)) {
    expensive_calculation();  // Don't hold lock during long operations
    lv_label_set_text(label, result);
    bsp_lvgl_unlock();
}
```

✅ **Correct:**
```c
char *result = expensive_calculation();  // Calculate outside lock
if (bsp_lvgl_lock(100)) {
    lv_label_set_text(label, result);
    bsp_lvgl_unlock();
}
```

### 3. Check Return Values

```c
lv_display_t *display = bsp_lvgl_init_default();
if (display == NULL) {
    ESP_LOGE(TAG, "LVGL init failed");
    // Handle error (retry, reboot, etc.)
    return;
}
```

### 4. Use Kconfig for Deployment Configs

Create `sdkconfig.defaults` for your project:

```ini
# Production config
CONFIG_BSP_ENABLE_LVGL=y
CONFIG_BSP_LVGL_BUFFER_LINES=100
CONFIG_BSP_LVGL_DOUBLE_BUFFER=y
CONFIG_BSP_LVGL_USE_DMA_BUFFER=y
CONFIG_BSP_LVGL_ROTATION_DEGREE=90
CONFIG_BSP_LVGL_TOUCH_ENABLE=y
CONFIG_BSP_LVGL_ENABLE_DEMO=n
```

---

## Additional Resources

### LVGL Documentation
- [LVGL Official Docs](https://docs.lvgl.io/9.2/)
- [LVGL Examples](https://docs.lvgl.io/9.2/examples.html)
- [LVGL Widgets](https://docs.lvgl.io/9.2/widgets/index.html)

### ESP-IDF LVGL Port
- [ESP LVGL Port GitHub](https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port)
- [ESP32-P4 Examples](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/lcd)

### Board-Specific
- [JD9165 Display Driver](components/guition_jc1060_bsp/include/bsp_display.h)
- [GT911 Touch Driver](components/guition_jc1060_bsp/include/bsp_touch.h)
- [BSP Main README](README.md)

---

## License

SPDX-License-Identifier: Apache-2.0

Copyright 2024 Cristiano Gorla
