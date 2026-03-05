# I2C / MIPI-DSI Conflict - SOLVED

## Problem Summary

The **JD9165 MIPI-DSI display controller corrupts the I2C peripheral hardware** during initialization. This causes **all subsequent I2C transactions to timeout**, breaking touch, audio codec, and RTC communication.

### Symptoms (When I2C Init BEFORE Display)

```
I (1096) BSP: [I2C] ✓ Ready
I (1096) I2C_TEST: [0x14] ✓ GT911 Touch         ← Works BEFORE display init
I (1096) I2C_TEST: [0x18] ✓ ES8311 Audio Codec
I (1097) I2C_TEST: [0x32] ✓ RX8025T RTC

I (1381) BSP: [PHASE D] ✓ Display HW           ← Display initialized

E (1481) i2c.master: I2C hardware timeout detected  ← I2C BROKEN!
E (1481) GT911: touch_gt911_read_cfg(410): GT911 read error!
```

### Root Cause

**GPIO pins remain HIGH and appear healthy, but the I2C peripheral controller is corrupted internally** by the MIPI-DSI initialization sequence.

- ❌ **GPIO check alone is NOT sufficient** (pins show correct levels)
- ❌ **I2C bus re-initialization DOES NOT FIX the issue** (hardware stays corrupted)
- ✅ **Solution: Initialize I2C AFTER display** (prevents corruption)

## Solution: Initialize I2C AFTER Display

### Why This Works

The vendor (Espressif) BSP initializes peripherals in this order:

```c
// 1. Display init (no I2C)
bsp_display_new_with_handles(NULL, &lcd_panels);

// 2. I2C init (AFTER display is stable)
bsp_i2c_init();  // Called from bsp_touch_new()

// 3. Touch and other I2C devices
bsp_touch_new();
```

**The display initialization cannot corrupt what doesn't exist yet.**

### Implementation

```c
static esp_err_t bsp_phase_d_peripheral_drivers(void)
{
    ESP_LOGI(TAG, "[PHASE D] Peripheral Drivers...");

    /* CRITICAL: Initialize display FIRST, then I2C */
#ifdef CONFIG_BSP_ENABLE_DISPLAY
    g_display_handle = bsp_display_init();
    ESP_LOGI(TAG, "[PHASE D] ✓ Display HW");
#endif

    /* NOW initialize I2C after display is stable */
    ESP_ERROR_CHECK(bsp_i2c_bus_init());

#ifdef CONFIG_BSP_ENABLE_TOUCH
    g_touch_handle = bsp_touch_init();  // Uses I2C
    ESP_LOGI(TAG, "[PHASE D] ✓ Touch HW");
#endif

    /* All other I2C devices work now */
#ifdef CONFIG_BSP_ENABLE_AUDIO
    bsp_audio_init(&audio_cfg);  // ES8311 via I2C
    ESP_LOGI(TAG, "[PHASE D] ✓ Audio");
#endif

#ifdef CONFIG_BSP_ENABLE_RTC
    bsp_rtc_init();  // RX8025T via I2C
    ESP_LOGI(TAG, "[PHASE D] ✓ RTC");
#endif

    return ESP_OK;
}
```

### Expected Boot Log (Correct Order)

```
I (1097) BSP_JD9165: Initializing JD9165 display (1024x600, 2-lane DSI)
I (1381) BSP_JD9165: Display initialized successfully
I (1381) BSP: [PHASE D] ✓ Display HW

I (1381) BSP: [I2C] ✓ Ready                     ← I2C AFTER display!

I (1381) I2C_TEST: ========== I2C PERIPHERAL TEST ==========
I (1381) I2C_TEST: [0x14] ✓ GT911 Touch         ← WORKS!
I (1382) I2C_TEST: [0x18] ✓ ES8311 Audio Codec  ← WORKS!
I (1382) I2C_TEST: [0x32] ✓ RX8025T RTC         ← WORKS!
I (1382) I2C_TEST: Total devices: 3

I (1382) BSP_GT911: Initializing GT911 touch controller
I (1447) BSP: [PHASE D] ✓ Touch HW              ← SUCCESS!
```

## Technical Details

### Hardware Conflict Mechanism

The JD9165 MIPI-DSI controller shares **GPIO routing or clock domains** with the I2C peripheral on ESP32-P4. During display initialization:

1. MIPI-DSI reconfigures GPIO matrix
2. If I2C is already initialized, its peripheral hardware state becomes inconsistent
3. GPIO pins remain electrically correct (HIGH with pullups)
4. **I2C controller registers are corrupted** (timeout errors)
5. **Re-initialization does NOT fix the issue** (corruption persists)

### Why I2C Recovery Failed

We attempted to fix this by re-initializing the I2C bus after display init:

```c
// This approach FAILED:
bsp_i2c_bus_init();           // I2C first
g_display_handle = bsp_display_init();  // Display corrupts I2C
i2c_reinit_bus(&g_i2c_bus_handle);      // Try to recover
bsp_touch_reset();            // Try to reset GT911
// RESULT: Still broken! No devices respond.
```

**Why it failed:**
- The corruption is too deep in the hardware
- Simple bus deletion and recreation doesn't reset the affected circuits
- GPIO state looks fine but internal I2C state machine is corrupted

### Why Init Order Works

```c
// This approach WORKS:
g_display_handle = bsp_display_init();  // Display first (no I2C to corrupt)
bsp_i2c_bus_init();           // I2C second (display is stable)
g_touch_handle = bsp_touch_init();      // Touch works immediately
// RESULT: Everything works!
```

**Why it works:**
- Display initialization happens when I2C peripheral is not yet active
- When I2C is created, display is already stable
- No conflict, no corruption

## Lessons Learned

1. **Initialization order matters** in hardware with shared resources
2. **GPIO-level checks are insufficient** for diagnosing hardware conflicts
3. **Sometimes the simplest solution is the best**: change init order instead of complex recovery
4. **Follow vendor BSP patterns** - they've already solved these issues

## Files Modified

### Core Implementation

- `components/guition_jc1060_bsp/src/bsp_board.c` - Changed init order

### Configuration (No longer needed)

- ~~`CONFIG_DEBUG_I2C_AUTO_RECOVERY`~~ - Recovery not needed
- ~~`CONFIG_DEBUG_I2C_GPIO_CHECK`~~ - GPIO check was misleading
- `CONFIG_DEBUG_I2C_TEST_PERIPHERALS` - Still useful for verification

## Production Recommendations

### Required

```c
// Initialize in this exact order:
1. Display hardware (MIPI-DSI)
2. I2C bus
3. I2C devices (touch, audio, RTC)
```

### Optional (for debugging)

```ini
CONFIG_DEBUG_I2C_TEST_PERIPHERALS=y    # ✅ Verify I2C devices on boot
```

### Not Needed

```ini
CONFIG_DEBUG_I2C_AUTO_RECOVERY=n       # ❌ Not needed with correct order
CONFIG_DEBUG_I2C_GPIO_CHECK=n          # ❌ GPIO check was misleading
```

## Comparison with Vendor BSP

### Espressif BSP (esp32_p4_function_ev_board)

```c
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    // 1. Display init
    disp = bsp_display_lcd_init(cfg);  // No I2C
    
    // 2. Touch init (creates I2C)
    disp_indev = bsp_display_indev_init(disp);  // Calls bsp_i2c_init()
    
    return disp;
}

esp_err_t bsp_touch_new(...)
{
    /* Initialize I2C */
    bsp_i2c_init();  // ← I2C created AFTER display
    
    /* Initialize touch */
    esp_lcd_touch_new_i2c_gt911(...);
}
```

### Our BSP (guition_jc1060_bsp)

Now matches vendor approach:

```c
static esp_err_t bsp_phase_d_peripheral_drivers(void)
{
    // 1. Display init (no I2C)
    g_display_handle = bsp_display_init();
    
    // 2. I2C init (after display stable)
    bsp_i2c_bus_init();
    
    // 3. Touch and I2C devices
    g_touch_handle = bsp_touch_init();
    bsp_audio_init(&audio_cfg);
    bsp_rtc_init();
}
```

## Conclusion

**The solution is simple: initialize I2C AFTER the display.**

This approach:
- ✅ Reliable (no corruption possible)
- ✅ Fast (no recovery delays)
- ✅ Clean (no complex workarounds)
- ✅ Matches vendor BSP pattern
- ✅ Safe for production

**No recovery, no GPIO checks, no GT911 reset - just correct initialization order.**

---

**Last Updated:** March 4, 2026  
**Issue First Observed:** March 4, 2026  
**Solution Found:** March 4, 2026 (analyzed vendor BSP)  
**Hardware:** Guition JC1060P470C (ESP32-P4 + JD9165 MIPI-DSI + GT911)
