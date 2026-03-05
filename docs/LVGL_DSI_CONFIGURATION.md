# LVGL DSI Configuration Guide

**Date**: 2026-03-04  
**Status**: ✅ Resolved  
**Related Components**: LVGL, MIPI DSI, esp_lvgl_port

---

## 📋 Overview

This document explains the LVGL configuration requirements for MIPI DSI displays on ESP32-P4, specifically addressing the `avoid_tearing` flag behavior and its relationship with frame buffer allocation.

### Problem Summary

When using `esp_lvgl_port` with MIPI DSI displays, setting `.avoid_tearing = true` requires **2 hardware frame buffers** due to hardcoded behavior in `esp_lvgl_port_disp.c` (line 341). This causes a crash if the display driver is configured with `num_fbs = 1`.

### Solution

Set `.avoid_tearing = false` in the DSI configuration to allow LVGL to manage its own draw buffers independently from hardware frame buffers.

---

## 🔍 Root Cause Analysis

### esp_lvgl_port Hardcoded Behavior

In `esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c` (line 341):

```c
#if CONFIG_IDF_TARGET_ESP32P4 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    buffer_size = disp_cfg->hres * disp_cfg->vres;
    
    // ❌ HARDCODED: Always requests 2 frame buffers when avoid_tearing=true!
    ESP_GOTO_ON_ERROR(
        esp_lcd_dpi_panel_get_frame_buffer(
            disp_cfg->panel_handle, 
            2,  // ← HARDCODED! Cannot be configured
            (void *)&buf1, 
            (void *)&buf2
        ), 
        err, TAG, "Get RGB buffers failed"
    );
#endif
```

**This code path executes ONLY when**:
- Target is ESP32-P4
- `avoid_tearing = true` in DSI config
- ESP-IDF version ≥ 5.3.0

**Result**: If display driver has `num_fbs = 1`, the call fails with:
```
E (1464) lcd.dsi: esp_lcd_dpi_panel_get_frame_buffer(409): invalid frame buffer number
E (1464) LVGL: lvgl_port_add_disp_priv(341): Get RGB buffers failed
```

---

## 💡 Configuration Options

### Option 1: avoid_tearing = false (Recommended)

**Configuration**:
```c
const lvgl_port_display_dsi_cfg_t dsi_cfg = {
    .flags = {
        .avoid_tearing = false,  // ← LVGL manages its own buffers
    }
};
```

**Behavior**:
- LVGL allocates **2 draw buffers** in PSRAM (separate from hardware)
- LVGL renders to its own buffers
- Content is copied to the **single hardware frame buffer** using DMA2D
- Copy operation happens during vertical blanking (when configured correctly)

**Advantages**:
✅ Works with `num_fbs = 1` (lower memory usage)  
✅ LVGL has full control over draw buffers  
✅ Simpler configuration  
✅ No dependency on hardware frame buffer count  

**Disadvantages**:
⚠️ Theoretical risk of tearing (very rare in practice)  
⚠️ Copy overhead (mitigated by DMA2D acceleration)

---

### Option 2: avoid_tearing = true + num_fbs = 2

**Configuration**:
```c
// In display driver initialization
esp_lcd_dpi_panel_config_t dpi_config = {
    .num_fbs = 2,  // ← MUST be 2
    // ... other config
};

// In LVGL configuration
const lvgl_port_display_dsi_cfg_t dsi_cfg = {
    .flags = {
        .avoid_tearing = true,  // ← Uses hardware frame buffers
    }
};
```

**Behavior**:
- LVGL uses **hardware frame buffers** directly
- No intermediate copy needed
- Hardware-level buffer swapping synchronized with VSYNC

**Advantages**:
✅ No copy overhead  
✅ Perfect synchronization (hardware controlled)  
✅ Lowest CPU usage for rendering  

**Disadvantages**:
❌ Requires 2 full-screen frame buffers (higher memory usage)  
❌ `num_fbs` must match hardcoded value in esp_lvgl_port  
❌ Less flexible buffer management  

---

## 🎯 Recommended Configuration

For the Guition JC1060P470C (1024x600 display):

```c
// Display driver configuration
esp_lcd_dpi_panel_config_t dpi_config = {
    .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
    .dpi_clock_freq_mhz = 52,
    .virtual_channel = 0,
    .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
    .num_fbs = 1,  // ← Single frame buffer (saves memory)
    .video_timing = {
        .h_size = 1024,
        .v_size = 600,
        .hsync_back_porch = 160,
        .hsync_pulse_width = 24,
        .hsync_front_porch = 160,
        .vsync_back_porch = 21,
        .vsync_pulse_width = 2,
        .vsync_front_porch = 12,
    },
    .flags = {
        .use_dma2d = true,  // ← DMA2D acceleration for copy
    }
};

// LVGL display configuration
const lvgl_port_display_cfg_t disp_cfg = {
    .panel_handle = panel_handle,
    .buffer_size = 1024 * 200,  // ← LVGL draw buffer (1/3 screen)
    .double_buffer = 1,          // ← 2 LVGL draw buffers
    .hres = 1024,
    .vres = 600,
    .flags = {
        .buff_dma = true,      // ← Buffers in DMA-capable memory
        .buff_spiram = false,   // ← Use internal RAM for speed
    }
};

// DSI-specific configuration
const lvgl_port_display_dsi_cfg_t dsi_cfg = {
    .flags = {
        .avoid_tearing = false,  // ← KEY SETTING!
    }
};
```

**Memory Usage**:
- Hardware frame buffer: 1024 × 600 × 2 bytes = 1.2 MB
- LVGL draw buffers: 2 × (1024 × 200 × 2) = 0.8 MB
- **Total: ~2.0 MB** (vs 2.8 MB with num_fbs=2)

---

## 🔄 Comparison: avoid_tearing=true vs false

| Aspect | avoid_tearing=true | avoid_tearing=false |
|--------|-------------------|---------------------|
| **Hardware Frame Buffers** | 2 required (hardcoded) | 1 sufficient |
| **LVGL Draw Buffers** | N/A (uses HW buffers) | 2 allocated |
| **Memory Usage** | ~2.4 MB | ~2.0 MB |
| **Tearing Risk** | None (hardware sync) | Very low (DMA2D + VSYNC) |
| **CPU Overhead** | Minimal | Copy operation (DMA2D) |
| **Configuration Flexibility** | Limited (num_fbs=2) | High (num_fbs=1 or 2) |
| **esp_lvgl_port Dependency** | High (hardcoded) | Low (independent) |

---

## 🚨 Common Errors

### Error 1: "invalid frame buffer number"

```
E (1464) lcd.dsi: esp_lcd_dpi_panel_get_frame_buffer(409): invalid frame buffer number
E (1464) LVGL: lvgl_port_add_disp_priv(341): Get RGB buffers failed
```

**Cause**: `avoid_tearing = true` but `num_fbs = 1`

**Solution**: Change to `avoid_tearing = false` OR increase `num_fbs` to 2

---

### Error 2: Guru Meditation (NULL pointer)

```
Guru Meditation Error: Core 0 panic'ed (Load access fault)
MEPC: 0x4802dd8c  (lv_display_get_theme)
```

**Cause**: LVGL display object not properly initialized due to buffer allocation failure

**Solution**: Fix buffer configuration first (see Error 1)

---

## 📖 References

- ESP-IDF LCD DSI Documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html
- esp_lvgl_port Source: https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c
- LVGL Performance Guide: https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/docs/performance.md
- ESP-IDF v5.5.3 Release: https://github.com/espressif/esp-idf/releases/tag/v5.5.3

---

## 📝 Implementation Notes

### For New Projects

1. Start with `avoid_tearing = false` + `num_fbs = 1`
2. Test for visible tearing artifacts
3. If tearing observed, try:
   - Increasing DPI clock frequency
   - Adjusting VSYNC timing
   - As last resort: `avoid_tearing = true` + `num_fbs = 2`

### For Existing Projects

If you have `avoid_tearing = true` working:
- **Keep it** if memory is not a concern
- **Consider switching** if you need to reduce memory usage
- **Test thoroughly** after changing (rendering performance may differ)

### Performance Tuning

With `avoid_tearing = false`:
- Use `buff_dma = true` for faster copy operations
- Enable `use_dma2d = true` in DPI config for hardware acceleration
- Consider `buff_spiram = false` if internal RAM is available (faster access)
- Tune `buffer_size` to balance memory vs redraw frequency

---

## ✅ Verification Checklist

After applying configuration:

- [ ] System boots without LCD errors
- [ ] LVGL display initializes successfully
- [ ] UI renders correctly
- [ ] No visible tearing during animations
- [ ] Touch input works correctly
- [ ] Memory usage acceptable for application

---

## 🔗 Related Documentation

- [README.md](../README.md) - Main project documentation
- [troubleshooting.md](../troubleshooting.md) - System troubleshooting guide
- [PROJECT_STATUS.md](PROJECT_STATUS.md) - Current project status
- [BSP Component Architecture](../components/bsp/README.md) - BSP design principles

---

**Last Updated**: 2026-03-04  
**Applies To**: ESP-IDF v5.5.3, LVGL v9.x, ESP32-P4
