# I2C / MIPI-DSI Conflict - Critical Hardware Issue

## Problem Summary

The **JD9165 MIPI-DSI display controller corrupts the I2C peripheral hardware** during initialization. This causes **all subsequent I2C transactions to timeout**, breaking touch, audio codec, and RTC communication.

### Symptoms

```
I (1101) I2C_TEST: [0x14] ✓ GT911 Touch         ← Works BEFORE display init
I (1101) I2C_TEST: [0x18] ✓ ES8311 Audio Codec
I (1102) I2C_TEST: [0x32] ✓ RX8025T RTC

I (1386) BSP: [PHASE D] ✓ Display HW           ← Display initialized

E (1481) i2c.master: I2C hardware timeout detected  ← I2C BROKEN!
E (1481) GT911: touch_gt911_read_cfg(410): GT911 read error!
```

### Root Cause

**CRITICAL DISCOVERY:** GPIO pins remain HIGH and appear healthy, but the **I2C peripheral controller is corrupted internally** by the MIPI-DSI initialization sequence.

- ❌ **GPIO check alone is NOT sufficient** (pins show correct levels)
- ✅ **I2C bus reinitalization is REQUIRED** (resets peripheral hardware)
- ✅ **GT911 must be reset after I2C recovery** (restores I2C address 0x14)

## Solution: I2C Recovery + GT911 Reset

### Implementation

```c
#ifdef CONFIG_BSP_ENABLE_DISPLAY
    g_display_handle = bsp_display_init();
    ESP_LOGI(TAG, "[PHASE D] ✓ Display HW");
    
    /* CRITICAL: Display ALWAYS corrupts I2C - recovery required */
#ifdef CONFIG_DEBUG_I2C_AUTO_RECOVERY
    ESP_LOGW(TAG, "Performing I2C bus recovery...");
    
    if (i2c_reinit_bus(&g_i2c_bus_handle) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "✓ I2C bus hardware recovered");
        
        /* CRITICAL: Reset GT911 to restore I2C address */
#ifdef CONFIG_BSP_ENABLE_TOUCH
        if (bsp_touch_reset() == ESP_OK) {
            ESP_LOGI(TAG, "✓ GT911 reset complete (address 0x14)");
        }
#endif
        
        /* Re-test peripherals */
        i2c_test_peripherals(g_i2c_bus_handle);
    }
#endif
#endif
```

### Recovery Sequence

1. **Delete I2C bus handle** (`i2c_del_master_bus`)
2. **Reset GPIO pins** (SDA=7, SCL=8)
3. **Wait 100ms** for hardware to stabilize
4. **Re-create I2C bus** with same configuration
5. **✨ NEW: Reset GT911 touch controller** (restores address 0x14)
6. **Verify all peripherals respond**

### Why GT911 Reset is Required

The **GT911 touch controller uses a special reset sequence** to configure its I2C address:

- **INT pin state during reset determines address:**
  - INT = LOW → Address 0x14 ✅ (our target)
  - INT = HIGH → Address 0x5D ❌ (wrong)

When we reinitialize the I2C bus:
1. GPIO pins are reconfigured
2. GT911 INT pin state may change
3. **GT911 may revert to wrong I2C address**
4. Touch controller becomes unreachable at 0x14

**Solution:** Always call `bsp_touch_reset()` after I2C bus recovery to force address back to 0x14.

## Configuration

### Kconfig Options

```kconfig
CONFIG_DEBUG_I2C_AUTO_RECOVERY=y      # Enable unconditional recovery
CONFIG_DEBUG_I2C_GPIO_CHECK=y         # Optional: Diagnostic GPIO logs
CONFIG_DEBUG_I2C_TEST_PERIPHERALS=y   # Test devices before/after recovery
```

**Located in:** `menuconfig → Guition Board Configuration → Debug Logging → I2C Debug & Testing`

### Expected Boot Log (With Recovery + GT911 Reset)

```
I (1087) BSP: [I2C] ✓ Ready
I (1087) I2C_TEST: ========== I2C PERIPHERAL TEST ==========
I (1087) I2C_TEST: [0x14] ✓ GT911 Touch
I (1088) I2C_TEST: [0x18] ✓ ES8311 Audio Codec
I (1088) I2C_TEST: [0x32] ✓ RX8025T RTC
I (1088) I2C_TEST: Total devices: 3
I (1088) I2C_TEST: =========================================

I (1372) BSP: [PHASE D] ✓ Display HW
W (1372) BSP: ⚠ Display initialized - I2C recovery required
W (1382) BSP: Performing I2C bus recovery...

I (1382) I2C_TEST: === I2C BUS RE-INITIALIZATION ===
I (1382) I2C_TEST: Deleting existing I2C bus...
I (1532) I2C_TEST: Creating new I2C bus (SDA=7, SCL=8)...
I (1532) I2C_TEST: ✓ I2C bus re-initialized successfully
I (1642) BSP: ✓ I2C bus hardware recovered successfully

I (1642) BSP: Resetting GT911 to restore I2C address...
I (1642) BSP_GT911: Re-executing GT911 reset sequence (post I2C recovery)
I (1642) BSP_GT911: Starting GT911 reset sequence (forcing address 0x14)
I (1707) BSP_GT911: Reset sequence complete, GT911 address set to 0x14
I (1707) BSP: ✓ GT911 reset complete (address 0x14)

I (1707) I2C_TEST: ========== I2C PERIPHERAL TEST ==========
I (1707) I2C_TEST: [0x14] ✓ GT911 Touch           ← NOW WORKING!
I (1708) I2C_TEST: [0x18] ✓ ES8311 Audio Codec
I (1708) I2C_TEST: [0x32] ✓ RX8025T RTC
I (1708) I2C_TEST: Total devices: 3
I (1708) I2C_TEST: =========================================

I (1708) BSP_GT911: Initializing GT911 touch controller
I (1773) BSP: [PHASE D] ✓ Touch HW                ← SUCCESS!
```

## Technical Details

### Hardware Conflict Mechanism

The JD9165 MIPI-DSI controller shares **GPIO routing or clock domains** with the I2C peripheral on ESP32-P4. During display initialization:

1. MIPI-DSI reconfigures GPIO matrix
2. I2C peripheral hardware state becomes inconsistent
3. GPIO pins remain electrically correct (HIGH with pullups)
4. **I2C controller registers are corrupted** (timeout errors)

### Why GPIO Check Fails

```c
int sda_level = gpio_get_level(GPIO_NUM_7);  // Returns 1 (HIGH) ✓
int scl_level = gpio_get_level(GPIO_NUM_8);  // Returns 1 (HIGH) ✓
// GPIO pins are OK, but I2C peripheral is broken!
```

**Lesson:** Hardware conflicts can be internal to the chip, invisible to GPIO-level checks.

### GT911 I2C Address Selection

From GT911 datasheet:

```
Reset Sequence:
1. INT = LOW (hold)
2. RST = LOW (10ms)
3. RST = HIGH (release, 5ms)
4. INT = INPUT with pullup
5. Wait 50ms

Result: I2C Address = 0x14 (0x28 in 8-bit format)
```

If this sequence is not performed correctly, GT911 defaults to **address 0x5D** which breaks communication.

## Files Modified

### Core Implementation

- `components/guition_jc1060_bsp/src/bsp_board.c` - Recovery + GT911 reset logic
- `components/guition_jc1060_bsp/drivers/gt911_bsp.c` - Added `bsp_touch_reset()`
- `components/guition_jc1060_bsp/drivers/gt911_bsp.h` - Public reset API
- `components/guition_jc1060_bsp/utils/i2c_test.c` - Test utilities
- `components/guition_jc1060_bsp/utils/i2c_test.h` - Public API

### Configuration

- `components/guition_jc1060_bsp/Kconfig.projbuild` - Debug options
- `sdkconfig.defaults` - Default recovery enabled
- `components/guition_jc1060_bsp/CMakeLists.txt` - Conditional build

## Production Recommendations

### Enable for Production

```ini
CONFIG_DEBUG_I2C_AUTO_RECOVERY=y       # ✅ REQUIRED - minimal overhead
CONFIG_DEBUG_I2C_TEST_PERIPHERALS=y    # ✅ OPTIONAL - diagnostic value
```

### Disable for Production

```ini
CONFIG_DEBUG_I2C_GPIO_CHECK=n          # ❌ Optional diagnostic (not needed if recovery works)
CONFIG_DEBUG_I2C_VERBOSE=n             # ❌ High overhead (floods serial)
```

## Alternative Workarounds (NOT Recommended)

### 1. Initialize I2C AFTER Display (❌ Breaks abstraction)

```c
// BAD: Forces specific init order
bsp_display_init();  // First
bsp_i2c_bus_init();  // After display
```

**Why not:** Violates BSP phase model, makes peripheral init order fragile.

### 2. Keep I2C and Display on Separate GPIO Banks (❌ Not possible)

ESP32-P4 has limited MIPI-DSI and I2C routing options.

### 3. Use Software I2C (❌ Too slow)

Bit-banged I2C would work but causes:
- High CPU overhead
- Slower touch response
- No hardware timeout handling

## Conclusion

**I2C recovery + GT911 reset after display init is the complete solution.**

This approach:
- ✅ Reliable (always fixes I2C + GT911)
- ✅ Fast (150ms delay total)
- ✅ Clean (no init order dependencies)
- ✅ Safe for production

---

**Last Updated:** March 4, 2026  
**Issue First Observed:** March 4, 2026  
**Solution Verified:** March 4, 2026 (GT911 reset added)  
**Hardware:** Guition JC1060P470C (ESP32-P4 + JD9165 MIPI-DSI + GT911)
