# BSP Framebuffer Configuration Fix

## Problem

The BSP layer is currently configured with **2 hardware framebuffers** (`num_fbs=2`), while the vendor demo uses **1 framebuffer** for optimal performance.

## Impact

**Current configuration:**
- DSI Driver: 2 × 1228 KB = **2456 KB**
- LVGL Buffer: 1 × 100 KB = **100 KB** (after lvgl_init.c fix)
- **Total VRAM: 2556 KB**

**Vendor configuration:**
- DSI Driver: 1 × 1228 KB = **1228 KB**
- LVGL Buffer: 1 × 100 KB = **100 KB**
- **Total VRAM: 1328 KB**

**Savings: 1.2 MB of VRAM!**

## Fix Required

### Option 1: If BSP is a managed component

1. Find the BSP component in `managed_components/` or `.espressif/`
2. Locate the JD9165 display initialization file (likely `jd9165_bsp.c` or similar)
3. Find the `esp_lcd_dpi_panel_config_t` structure
4. Change:
   ```c
   // ❌ BEFORE
   .num_fbs = 2,
   
   // ✅ AFTER (match vendor)
   .num_fbs = 1,
   ```

### Option 2: If BSP is in your repository

1. Navigate to `components/bsp/` or wherever the BSP is located
2. Find the display initialization file
3. Search for `num_fbs`
4. Apply the same change as above

### Option 3: Add Kconfig override (if supported)

Add to `sdkconfig.defaults` or configure via `idf.py menuconfig`:
```
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1
```

## How to Verify

After making the change:

1. Clean build:
   ```bash
   idf.py fullclean
   idf.py build
   ```

2. Check boot logs for:
   ```
   BSP_JD9165: Display initialized successfully (single FB mode)
   ```
   
   Instead of:
   ```
   BSP_JD9165: Display initialized successfully (dual FB mode)
   ```

3. Verify no flickering on screen
4. Test touch responsiveness

## Why This Matters

**Single framebuffer mode** is sufficient because:
- LVGL handles double-buffering at the software level if needed
- Less memory pressure = better performance
- Vendor demo proves this configuration works reliably
- `avoid_tearing=false` in LVGL config means we don't need hardware double-buffering

## References

- Vendor demo: Uses `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1` (default)
- LVGL config: Now uses single 50-line draw buffer (100 KB)
- Combined optimization: Reduces total VRAM by ~1.9 MB

## Commit Status

- ✅ LVGL config fixed: commit `1979f8f`
- ⏳ BSP config: **Pending manual fix**

The BSP component may be managed externally (not in git), so this change
must be applied locally after component installation.
