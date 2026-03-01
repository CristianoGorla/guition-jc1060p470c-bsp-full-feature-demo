# Troubleshooting Guide - Guition JC1060P470C

## GT911 Touch Controller Initialization Issues

### Problem: GT911 Fails with "clear bus failed" Error

**Symptoms:**
```
E (6224) i2c.master: clear bus failed.
E (6224) i2c.master: s_i2c_transaction_start(686): reset hardware failed
E (6224) lcd_panel.io.i2c: panel_io_i2c_rx_buffer(145): i2c transaction failed
E (6229) GT911: touch_gt911_read_cfg(410): GT911 read error!
E (6235) GT911: esp_lcd_touch_new_i2c_gt911(161): GT911 init failed
```

**Root Cause:**
The GT911 touch controller requires a specific hardware reset sequence to configure its I2C address (0x14 or 0x5D). When an I2C bus scan is performed **before** the GT911 driver initialization, it wakes up the chip and interferes with the driver's reset sequence timing.

**Solution:**
Disable I2C bus scanning before GT911 initialization.

In `feature_flags.h`:
```c
#define ENABLE_I2C_SCAN 0  // Disable I2C scan before touch init
```

**Why This Works:**
1. GT911 uses a specific reset sequence to lock its I2C address
2. The address (0x14 vs 0x5D) is determined by the INT pin state during reset
3. I2C scan operations send probe commands that wake the chip prematurely
4. This breaks the driver's carefully timed reset sequence
5. Disabling scan allows the driver to execute a clean reset

**Correct Initialization Sequence:**
```
1. NVS Init
2. I2C Bus Init (GPIO7/8)
3. Display Init (MIPI DSI) - does NOT interfere with I2C
4. GT911 Init - driver performs hardware reset automatically
   └─> GPIO21 (RST) and GPIO22 (INT) timing sets address to 0x14
5. (Optional) I2C scan after touch init for verification
```

**Successful Output:**
```
I (1493) GT911: I2C address initialization procedure skipped - using default GT9xx setup
I (1520) GT911: TouchPad_ID:0x39,0x31,0x31
I (1520) GT911: TouchPad_Config_Version:99
I (1520) GT911: ✓ GT911 initialized successfully
I (1521) GT911:   Resolution: 1024x600
I (1525) GT911:   Driver auto-detected I2C address
I (1529) GT911:   Touch ready for reading
I (1637) GUITION_MAIN: GT911 detected at address 0x14 (INT=HIGH during reset)
```

### GT911 Address Configuration

The GT911 supports two I2C addresses based on INT pin state during reset:

| INT Pin During Reset | I2C Address | Board Configuration |
|---------------------|-------------|---------------------|
| HIGH (pulled up)    | 0x14 (0x28 write) | **Default for this board** |
| LOW (pulled down)   | 0x5D (0xBA write) | Alternative |

The driver automatically handles this through `ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()` macro, which tries both addresses.

### Hardware Connections

```
GT911 Pin | ESP32-P4 GPIO | Function
----------|---------------|----------
SDA       | GPIO7         | I2C Data
SCL       | GPIO8         | I2C Clock
RST       | GPIO21        | Hardware Reset
INT       | GPIO22        | Interrupt (sets I2C address)
```

### Driver Implementation Notes

**From Official ESP-IDF GT911 Driver:**
- Uses `ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()` for auto address detection
- Performs hardware reset sequence automatically via `rst_gpio_num`
- Reset sequence timing is critical (10ms LOW, 100ms HIGH)
- INT pin state during reset determines final I2C address
- No manual reset code needed in application

**touch_gt911.c Configuration:**
```c
esp_lcd_touch_config_t tp_cfg = {
    .x_max = 1024,
    .y_max = 600,
    .rst_gpio_num = GPIO_NUM_21,  // Driver pulses this for reset
    .int_gpio_num = GPIO_NUM_22,  // Driver reads this for address
    .levels = {
        .reset = 0,      // Active LOW reset
        .interrupt = 0,  // Active LOW interrupt
    },
};
```

---

## Display (MIPI DSI) and I2C Coexistence

### MIPI DSI Does NOT Interfere with I2C Bus

Earlier troubleshooting showed concerns about MIPI DSI display initialization corrupting the I2C bus. **This is NOT the case.**

**Evidence:**
- Display uses dedicated MIPI DSI interface (GPIO 45-52)
- I2C uses separate GPIO7/8 pins
- Display initialization completes successfully without I2C errors
- GT911 works correctly when initialized after display
- No GPIO conflicts between MIPI DSI and I2C peripherals

**Correct Understanding:**
The I2C bus issues were caused by **I2C scan timing**, not MIPI DSI initialization. The display and touch controller can be initialized in sequence without conflicts.

---

## Feature Flags Configuration for Touch Debugging

**Minimal Configuration for GT911 Testing:**
```c
#define ENABLE_I2C 1           // Required for touch
#define ENABLE_I2C_SCAN 0      // MUST BE DISABLED before touch init
#define ENABLE_DISPLAY 1       // Can be enabled (no conflict)
#define ENABLE_DISPLAY_TEST 0  // Disable to speed up testing
#define ENABLE_TOUCH 1         // Enable touch controller
#define DEBUG_TOUCH 1          // Show detailed touch logs
#define ENABLE_TOUCH_TEST 0    // Disable continuous touch reading loop
```

**Re-enable I2C Scan After Touch Init (Optional):**
You can add an I2C scan **after** GT911 initialization to verify all devices:
```c
// In main, after touch init:
#if ENABLE_I2C_SCAN_AFTER_TOUCH
    ESP_LOGI(TAG, "I2C scan after touch init:");
    i2c_scan_bus(bus_handle);
#endif
```

---

## References

- **Guition Official Demo:** `JC1060P470C_I_W_Y/Demo_IDF/ESP-IDF/lvgl_demo_v9`
- **ESP-IDF GT911 Driver:** `components/esp_lcd_touch_gt911`
- **Board BSP:** `esp32_p4_function_ev_board.h` (shows GPIO_NUM_NC for touch pins in 1024x600 config)
- **Hardware Reset Sequence:** Handled internally by `esp_lcd_touch_new_i2c_gt911()`

---

## Historical Notes

### Deprecated Approaches (No Longer Used)

1. ❌ **Manual Hardware Reset (`hw_init.c`)** - Removed
   - Driver handles reset automatically
   - Manual reset conflicted with driver's internal sequence

2. ❌ **Aggressive I2C Recovery** - Removed
   - Not needed when scan is disabled
   - MIPI DSI doesn't corrupt I2C bus

3. ❌ **GPIO State Monitoring** - Removed
   - Overcomplicated debugging
   - Issue was scan timing, not GPIO state

### Evolution of Solution

```
Initial Approach → I2C scan → Manual reset → Touch init
                    ❌ Scan wakes GT911
                    ❌ Manual reset conflicts with driver

Final Approach → I2C init → Display init → Touch init (driver auto-reset)
                  ✅ No scan before touch
                  ✅ Driver handles reset timing
                  ✅ Clean initialization
```
