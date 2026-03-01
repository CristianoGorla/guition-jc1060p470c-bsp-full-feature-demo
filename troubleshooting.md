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

## RTC RX8025T Initialization

### Best Practice: Direct Initialization (No Pre-Probe)

**Recommended Pattern:**
Apply the same philosophy as GT911 - let the driver validate device presence during initialization.

**Why No Pre-Probe:**
1. `i2c_master_probe()` can interfere with device state
2. Driver's first register read is a "gentle" detection method
3. Consistent pattern across all I2C devices (GT911, RTC, ES8311)
4. Cleaner code with less redundant I2C transactions

**Implementation:**
```c
// ❌ OLD: Pre-probe before init
ret = i2c_master_probe(bus_handle, 0x32, 500);
if (ret == ESP_OK) {
    ret = rtc_rx8025t_init(bus_handle);
}

// ✅ NEW: Direct init (driver validates internally)
ret = rtc_rx8025t_init(bus_handle);
if (ret == ESP_OK) {
    // RTC is present and working
} else {
    // RTC not responding (may not be populated)
}
```

**Driver Self-Validation:**
The `rtc_rx8025t_init()` function:
1. Creates I2C device handle at address 0x32
2. Performs non-invasive read of time registers
3. If read succeeds → RTC is present and responding
4. If read fails → Returns error (no harm done)

**Successful Output:**
```
=== RTC Initialization ===
I (1150) RX8025T: Initializing RX8025T RTC...
I (1151) RX8025T: I2C Address: 0x32
I (1152) RX8025T: Reading current time (gentle init)...
I (1156) RX8025T: ✓ RTC responding on I2C!
I (1157) RX8025T: Current RTC time: 2026-03-01 (wday=6) 18:27:45
I (1163) RX8025T: PON/VLF flags already clear - RTC time is valid
I (1169) RX8025T: Already in 24-hour format
I (1173) RX8025T: RX8025T initialized successfully
I (1178) GUITION_MAIN: ✓ RTC initialized successfully

I (1183) GUITION_MAIN: Current time: 2026-03-01 18:27:45
I (1188) GUITION_MAIN: PON Flag: CLEAR
I (1192) GUITION_MAIN: VLF Flag: CLEAR
```

### RTC Hardware Details

**I2C Configuration:**
- Address: 0x32 (7-bit)
- Clock Speed: 100kHz (safer for RTC)
- Shared bus with GT911 and ES8311

**Power-On Flags:**
- **PON (Power-On)**: Set when RTC loses power
- **VLF (Voltage Low)**: Set when backup voltage drops below threshold
- Driver automatically clears these flags if set
- If both flags are clear → RTC time is valid

**Time Format:**
- Driver ensures 24-hour format is enabled
- BCD encoding for all time registers
- Year range: 2000-2099 (stored as 00-99)

### Feature Flags for RTC

```c
#define ENABLE_RTC 1           // Enable RTC initialization
#define DEBUG_RTC 1            // Show detailed RTC logs
#define ENABLE_RTC_TEST 1      // Read and display current time
#define ENABLE_RTC_HW_TEST 0   // Advanced hardware diagnostic
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

## Complete Initialization Sequence (Final)

**Optimized Boot Order:**
```
1. NVS Flash Init
2. I2C Bus Init (GPIO7/8 @ 400kHz)
3. ❌ SKIP I2C Scan (prevents device wake-up)
4. ES8311 Audio Init (optional, address 0x18)
5. RTC RX8025T Init (direct init at 0x32)
6. Display JD9165 Init (MIPI DSI, no I2C conflict)
7. Touch GT911 Init (auto-reset, detects 0x14 or 0x5D)
8. SD Card (optional)
9. WiFi ESP-Hosted (optional)
```

**Key Principles:**
1. No I2C scan before device initialization
2. Each driver validates device presence internally
3. Direct init without pre-probe
4. MIPI DSI does not interfere with I2C
5. Consistent pattern across all devices

---

## Feature Flags Configuration

**Recommended Configuration for Full System Test:**
```c
#define ENABLE_I2C 1           // Required for I2C devices
#define ENABLE_I2C_SCAN 0      // ❌ MUST BE DISABLED

#define ENABLE_AUDIO 0         // ES8311 (optional)
#define ENABLE_RTC 1           // ✅ RTC enabled
#define ENABLE_RTC_TEST 1      // ✅ Show time on boot

#define ENABLE_DISPLAY 1       // ✅ MIPI DSI display
#define ENABLE_DISPLAY_TEST 0  // Disable RGB patterns

#define ENABLE_TOUCH 1         // ✅ GT911 touch
#define ENABLE_TOUCH_TEST 0    // Disable continuous read

#define ENABLE_SD_CARD 0       // SD card (optional)
#define ENABLE_WIFI 0          // WiFi (optional)
```

---

## Successful Boot Log (Complete System)

```
I (1124) GUITION_MAIN: ========================================
I (1125) GUITION_MAIN:    Guition JC1060P470C Initialization
I (1131) GUITION_MAIN: ========================================

I (1143) GUITION_MAIN: === I2C Bus Initialization ===
I (1144) GUITION_MAIN: ✓ I2C bus ready (SDA=GPIO7, SCL=GPIO8)

I (1150) GUITION_MAIN: === RTC Initialization ===
I (1151) RX8025T: Initializing RX8025T RTC...
I (1156) RX8025T: ✓ RTC responding on I2C!
I (1157) RX8025T: Current RTC time: 2026-03-01 (wday=6) 18:27:45
I (1178) GUITION_MAIN: ✓ RTC initialized successfully
I (1183) GUITION_MAIN: Current time: 2026-03-01 18:27:45

I (1141) GUITION_MAIN: === Display Initialization ===
I (1448) JD9165: Display initialized (1024x600 @ 52MHz, 2-lane DSI)
I (1448) GUITION_MAIN: ✓ Display ready (1024x600 MIPI DSI)

I (1449) GUITION_MAIN: === Touch Controller Initialization ===
I (1501) GT911: TouchPad_ID:0x39,0x31,0x31
I (1501) GT911: TouchPad_Config_Version:99
I (1501) GT911: ✓ GT911 initialized successfully
I (1514) GUITION_MAIN: ✓ Touch controller ready
I (1618) GUITION_MAIN: GT911 active at 0x14 (INT=HIGH during reset)

I (1626) GUITION_MAIN: ========================================
I (1632) GUITION_MAIN:    System Initialization Complete
I (1637) GUITION_MAIN: ========================================
```

---

## References

- **Guition Official Demo:** `JC1060P470C_I_W_Y/Demo_IDF/ESP-IDF/lvgl_demo_v9`
- **ESP-IDF GT911 Driver:** `components/esp_lcd_touch_gt911`
- **Board BSP:** `esp32_p4_function_ev_board.h`
- **RTC Datasheet:** Epson RX8025T Real-Time Clock Module

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

4. ❌ **Pre-Probe Before Init** - Removed
   - Redundant I2C transaction
   - Driver validates presence internally
   - Applied to both GT911 and RTC

### Evolution of Solution

```
Initial Approach → I2C scan → Probe → Init
                    ❌ Scan wakes devices
                    ❌ Probe is redundant
                    ❌ Breaks reset timing

Final Approach → I2C init → Direct device init (driver self-validates)
                  ✅ No scan interference
                  ✅ No redundant probes
                  ✅ Clean initialization
                  ✅ Consistent pattern
```
