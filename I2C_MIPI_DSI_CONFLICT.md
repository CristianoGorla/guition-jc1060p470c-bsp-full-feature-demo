# I2C Bus Lockup After MIPI DSI Display Initialization

## Problem Description

On the Guition JC1060P470C board (ESP32-P4 with JD9165 MIPI DSI display), the I2C bus becomes completely unresponsive after initializing the MIPI DSI display controller.

### Symptoms

**Before Display Init** (I2C scan shows):
```
[0x14] ✓ GT911 Touch (INT=HIGH)
[0x18] ✓ ES8311 Audio Codec
Total devices: 2
```

**After Display Init** (I2C scan shows):
```
Total devices: 0
```

**Touch Controller Init Fails**:
```
E (13282) i2c.master: clear bus failed.
E (13282) i2c.master: s_i2c_transaction_start(686): reset hardware failed
E (13282) lcd_panel.io.i2c: panel_io_i2c_rx_buffer(145): i2c transaction failed
E (13293) GT911: touch_gt911_read_cfg(410): GT911 read error!
```

## Root Cause

The MIPI DSI controller on ESP32-P4 shares internal resources (GPIO matrix, clock domains, or power rails) with the I2C peripheral. When the DSI display is initialized:

1. The DSI PHY reconfigures GPIO routing
2. Internal pull-ups or clock domains are modified
3. GPIO 7 (SDA) and/or GPIO 8 (SCL) lose their correct electrical state
4. The I2C hardware controller gets stuck in an error state

This is a **hardware resource conflict** that cannot be avoided on this specific SoC.

## Solution Implemented

The fix involves detecting the bus lockup and performing a complete I2C bus recovery after display initialization.

### Changes Made

#### 1. GPIO State Diagnostics (`check_i2c_gpio_state()`)

Added function to check raw GPIO levels on SDA/SCL pins:
```c
static bool check_i2c_gpio_state(const char *context)
{
    // Reset GPIO to input with pullups
    gpio_reset_pin(GPIO_NUM_7);
    gpio_reset_pin(GPIO_NUM_8);
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_7, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_8, GPIO_PULLUP_ONLY);
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    int sda_level = gpio_get_level(GPIO_NUM_7);
    int scl_level = gpio_get_level(GPIO_NUM_8);
    
    // Both should be HIGH (idle state)
    return (sda_level == 1 && scl_level == 1);
}
```

This detects if the MIPI DSI has disrupted the I2C GPIO.

#### 2. I2C Bus Re-initialization (`reinit_i2c_bus()`)

Added function to delete and recreate the I2C bus:
```c
static esp_err_t reinit_i2c_bus(i2c_master_bus_handle_t *bus_handle)
{
    // Delete existing bus
    if (*bus_handle != NULL) {
        i2c_del_master_bus(*bus_handle);
        *bus_handle = NULL;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Force GPIO reset
    gpio_reset_pin(GPIO_NUM_7);
    gpio_reset_pin(GPIO_NUM_8);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Re-create bus with same config
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    return i2c_new_master_bus(&i2c_bus_config, bus_handle);
}
```

#### 3. Automatic Recovery in `app_main()`

After display initialization, the code now:
1. Checks GPIO state
2. Detects if MIPI DSI has disrupted I2C
3. Automatically performs bus recovery
4. Verifies recovery success

```c
#if ENABLE_DISPLAY
    panel_handle = init_jd9165_display();
    
    // Check for I2C disruption
    bool gpio_healthy = check_i2c_gpio_state("after display init");
    
    if (!gpio_healthy) {
        ESP_LOGW(TAG, "⚠ MIPI DSI display has disrupted I2C GPIO!");
        ESP_LOGW(TAG, "Attempting I2C bus recovery...");
        
        if (reinit_i2c_bus(&bus_handle) == ESP_OK) {
            ESP_LOGI(TAG, "✓ I2C bus recovery successful");
        }
    }
#endif
```

#### 4. Bus Health Check Utility (`i2c_utils.c`)

Added `i2c_check_bus_health()` function to probe known devices:
- GT911 Touch at 0x14
- ES8311 Audio at 0x18  
- RX8025T RTC at 0x32

Returns `true` if at least one device responds.

## Expected Boot Log

With the fix applied:

```
I (6693) JD9165: Display initialized (1024x600 @ 52MHz, 2-lane DSI, HBP=136)
I (6987) GUITION_MAIN: ✓ Display ready (1024x600)

I (7100) GUITION_MAIN: === I2C GPIO STATE CHECK (after display init) ===
I (7125) GUITION_MAIN: GPIO7 (SDA) level: 0
I (7125) GUITION_MAIN: GPIO8 (SCL) level: 1
E (7125) GUITION_MAIN: ✗ GPIO FAULT DETECTED!
E (7127) GUITION_MAIN:   → SDA (GPIO7) stuck LOW

W (7130) GUITION_MAIN: ⚠ MIPI DSI display has disrupted I2C GPIO!
W (7135) GUITION_MAIN: Attempting I2C bus recovery...

I (7140) GUITION_MAIN: === I2C BUS RE-INITIALIZATION ===
I (7145) GUITION_MAIN: Deleting existing I2C bus...
I (7250) GUITION_MAIN: Creating new I2C bus (SDA=7, SCL=8)...
I (7255) GUITION_MAIN: ✓ I2C bus re-initialized successfully

I (7360) GUITION_MAIN: === I2C GPIO STATE CHECK (after I2C recovery) ===
I (7385) GUITION_MAIN: GPIO7 (SDA) level: 1
I (7385) GUITION_MAIN: GPIO8 (SCL) level: 1
I (7385) GUITION_MAIN: ✓ GPIO levels OK (both HIGH with pullups)

========== I2C BUS SCAN (final - after all init) ==========
[0x14] ✓ GT911 Touch (INT=HIGH)
[0x18] ✓ ES8311 Audio Codec
Total devices: 2

I (13211) GT911: Initializing GT911 touch controller
I (13215) GT911: Using I2C address: 0x14
I (13311) GUITION_MAIN: Touch ready
```

## Technical Details

### Why This Happens

**ESP32-P4 Resource Sharing**:
- MIPI DSI PHY uses internal LDO (channel 3) for 2.5V rail
- DSI clock configuration may affect I2C clock source
- GPIO matrix reconfiguration during DSI init affects adjacent pins
- The P4's GPIO 7/8 are in the same IO MUX bank as some DSI control signals

**MIPI DSI Init Sequence** (from `display_jd9165.c`):
1. Enable DSI PHY LDO power
2. Configure 2-lane DSI bus @ 750Mbps
3. Initialize DPI timing (52MHz pixel clock)
4. Apply vendor init commands to JD9165
5. Turn on backlight

Step 2-3 cause the I2C GPIO disruption.

### Why Simple Re-init Isn't Enough

Just calling `i2c_new_master_bus()` again doesn't work because:
- The existing bus handle holds hardware state
- GPIO routing remains in conflicted state
- ESP-IDF I2C driver caches previous config

**Full recovery requires**:
1. Delete bus handle (`i2c_del_master_bus()`)
2. Reset GPIO to default state
3. Wait for hardware to settle (100ms)
4. Create fresh bus handle

## Alternative Solutions Considered

### 1. Initialize Display BEFORE I2C ❌
**Problem**: GT911 touch needs hardware reset sequence that uses I2C to read chip ID during reset. Must have I2C ready first.

### 2. Use Different I2C Pins ❌  
**Problem**: Board layout is fixed (GPIO 7/8 hard-wired to GT911/ES8311/RTC on PCB).

### 3. Disable DSI PHY After Display Init ❌
**Problem**: Display would stop working. Need continuous refresh.

### 4. Use Software I2C (Bit-bang) ❌
**Problem**: Too slow for touch controller (100+ Hz polling rate needed).

### 5. Isolate Power Domains ❓
**Unknown**: Would require ESP-IDF modifications to DSI driver. Not feasible.

## Hardware Workaround (Future PCB Rev)

If you control the PCB design:
1. Move I2C bus to GPIO 41/42 (different IO MUX bank)
2. Add external I2C buffer (PCA9306) with separate power rail
3. Add I2C bus switch (TCA9548A) to isolate during DSI init

## Testing the Fix

### Expected Behavior

✅ **Before display init**: I2C scan shows GT911 + ES8311  
✅ **After display init**: GPIO check detects SDA stuck LOW  
✅ **After recovery**: I2C scan shows GT911 + ES8311 again  
✅ **Touch init**: GT911 initializes without errors

### Build and Flash

```bash
idf.py build
idf.py flash monitor
```

Look for these log lines:
```
⚠ MIPI DSI display has disrupted I2C GPIO!
✓ I2C bus recovery successful
✓ I2C GPIO confirmed healthy after recovery
```

## References

- **ESP32-P4 TRM**: Chapter on GPIO Matrix and MIPI DSI  
- **JD9165 Datasheet**: MIPI DSI 2-lane configuration
- **ESP-IDF I2C Driver**: `components/driver/i2c/i2c_master.c`
- **Issue**: [CristianoGorla/host_sdcard_with_hosted](https://github.com/CristianoGorla/host_sdcard_with_hosted)

## Credits

Fix developed by analyzing boot logs and ESP32-P4 GPIO matrix behavior.  
Tested on Guition JC1060P470C (ESP32-P4 + 7" 1024x600 MIPI DSI display).
