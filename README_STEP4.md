# Step 4: Driver Isolation and Standardization - COMPLETE

## Summary

This commit completes Step 4 of the refactoring roadmap: **isolation of hardware drivers into the BSP component** with full Kconfig integration.

## Changes Made

### 1. Driver Migration to BSP Component

All hardware-specific drivers have been moved from `main/` to `components/guition_jc1060_bsp/drivers/`:

#### Display Driver (`jd9165_bsp.c/h`)
- **Hardware**: JD9165 MIPI-DSI controller
- **Resolution**: 1024x600 RGB565
- **Interface**: 2-lane MIPI DSI @ 750Mbps
- **Pixel Clock**: 51.2MHz
- **Backlight**: PWM on GPIO 23 (10-bit LEDC)
- **Features**:
  - LDO power management for DSI PHY
  - Complete initialization sequence from vendor BSP
  - Brightness control API

#### Touch Driver (`gt911_bsp.c/h`)
- **Hardware**: GT911 capacitive touch controller
- **I2C Address**: 0x14 (forced via reset sequence)
- **Reset GPIO**: 21
- **Interrupt GPIO**: 22
- **Max Touch Points**: 5 simultaneous touches
- **Features**:
  - Proper reset sequence for address selection
  - **CRITICAL**: No I2C bus scan before initialization
  - Interrupt-driven touch detection

#### Audio Driver (`es8311_bsp.c/h`)
- **Hardware**: ES8311 audio codec + NS4150 power amplifier
- **I2C Address**: 0x18 (codec)
- **PA Control GPIO**: 11 (amplifier enable)
- **I2S Configuration**:
  - MCLK: GPIO 9
  - BCLK: GPIO 13 (I2S_SCLK)
  - WS: GPIO 12 (I2S_LRCK)
  - DOUT: GPIO 10 (I2S_DSDIN - to codec)
- **Features**:
  - Unified ES8311 + NS4150 initialization
  - I2S interface configuration
  - PA enable/disable control
  - Volume control API (placeholder)

#### RTC Driver (`rx8025t_bsp.c/h`)
- **Hardware**: RX8025T real-time clock
- **I2C Address**: 0x32
- **Interrupt GPIO**: 0
- **Crystal**: 32.768 kHz with temperature compensation
- **Features**:
  - BCD time/date conversion
  - Power-on flag detection
  - Voltage-low flag detection
  - Conversion helpers for `struct tm`

### 2. BSP Orchestrator Updates

`components/guition_jc1060_bsp/src/bsp_board.c` now implements:

- **Phase A**: Power Manager (SD card sequencing)
- **Phase D**: Peripheral Drivers initialization
- **I2C Bus Sharing**: Single `i2c_master_bus_handle_t` shared by touch, audio, RTC
- **Kconfig Guards**: Conditional compilation based on `CONFIG_BSP_ENABLE_*` flags

#### Initialization Flow

```c
bsp_board_init()
  ↓
  Phase A: Power Manager
    ├─ Selective hard reset (crash/watchdog/brownout)
    ├─ GPIO isolation
    └─ SD card power sequencing
  ↓
  Phase D: Peripheral Drivers
    ├─ I2C bus initialization (shared)
    ├─ Display init (if CONFIG_BSP_ENABLE_DISPLAY)
    ├─ Touch init (if CONFIG_BSP_ENABLE_TOUCH)
    ├─ Audio init (if CONFIG_BSP_ENABLE_AUDIO)
    └─ RTC init (if CONFIG_BSP_ENABLE_RTC)
```

### 3. Kconfig Configuration

Updated `components/guition_jc1060_bsp/Kconfig` with unified options:

```
CONFIG_BSP_ENABLE_DISPLAY       - JD9165 display (default: y)
CONFIG_BSP_ENABLE_I2C_BUS       - I2C bus (default: y)
CONFIG_BSP_ENABLE_TOUCH         - GT911 touch (default: y, depends on I2C)
CONFIG_BSP_ENABLE_AUDIO         - ES8311 + NS4150 (default: y, depends on I2C)
CONFIG_BSP_ENABLE_RTC           - RX8025T RTC (default: y, depends on I2C)
CONFIG_BSP_ENABLE_SDIO          - SDIO controller (default: y)
CONFIG_BSP_ENABLE_WIFI          - ESP-Hosted WiFi (default: y, depends on SDIO)
CONFIG_BSP_ENABLE_SDCARD        - SD card (default: y, depends on SDIO+WiFi)
```

### 4. Main Application Cleanup

`main/main.c` is now drastically simplified:

**BEFORE** (~400 lines):
```c
app_main() {
    // Manual I2C bus init
    // Manual display init (display_jd9165.c)
    // Manual touch init (touch_gt911.c)
    // Manual audio init (es8311_audio.c)
    // Manual RTC init (rtc_rx8025t.c)
    // Bootstrap manager
    // Application logic
}
```

**AFTER** (~200 lines):
```c
app_main() {
    bsp_board_init();  // ← ONE CALL handles all hardware
    // NVS init
    // Bootstrap manager (Phase C: WiFi, Phase B: SD)
    // Application logic
}
```

### 5. Files Removed from main/

The following driver files are now **obsolete** and should be deleted:

- `main/display_jd9165.c` / `.h` → Moved to `drivers/jd9165_bsp.c/h`
- `main/touch_gt911.c` / `.h` → Moved to `drivers/gt911_bsp.c/h`
- `main/es8311_audio.c` / `.h` → Moved to `drivers/es8311_bsp.c/h`
- `main/rtc_rx8025t.c` / `.h` → Moved to `drivers/rx8025t_bsp.c/h`
- `main/i2c_utils.c` / `.h` → No longer needed (I2C in BSP)

**Note**: These files should be manually deleted or removed in a follow-up commit to avoid breaking existing code during migration.

### 6. CMakeLists.txt Updates

`components/guition_jc1060_bsp/CMakeLists.txt` now includes all driver sources:

```cmake
idf_component_register(
    SRCS 
        "src/bsp_board.c"
        "drivers/jd9165_bsp.c"    # Display
        "drivers/gt911_bsp.c"     # Touch
        "drivers/es8311_bsp.c"    # Audio
        "drivers/rx8025t_bsp.c"   # RTC
    INCLUDE_DIRS 
        "include"
        "drivers"
    REQUIRES
        driver
        esp_lcd
        esp_lcd_touch_gt911
        esp_lcd_jd9165
        freertos
)
```

## Architecture Benefits

### Clear Separation of Concerns

| Component | Responsibility |
|-----------|----------------|
| **BSP** | Hardware initialization, power management, driver orchestration |
| **Bootstrap Manager** | Phase C (WiFi) + Phase B (SD card) sequential startup |
| **main.c** | Application logic only |

### Conditional Compilation

All peripherals can now be disabled via `idf.py menuconfig`:
```
Component config → Guition JC1060 BSP Configuration
```

This enables:
- Minimal builds for testing
- Power-saving configurations
- Custom hardware variants

### Testability

Each driver can now be tested independently:
```c
#include "jd9165_bsp.h"
esp_lcd_panel_handle_t display = bsp_display_init();
bsp_display_set_brightness(50);  // Test brightness control
```

### Maintainability

- **Single Source of Truth**: Hardware configuration in `drivers/` only
- **No Duplication**: Removed `bsp_extra.h` macros, I2C utilities
- **Kconfig-Driven**: All pin assignments and timing from menuconfig

## Hardware Configuration Summary

### Pin Mapping

| Function | GPIO | Notes |
|----------|------|-------|
| **Display** | | |
| LCD Backlight | 23 | PWM (LEDC) |
| LCD Reset | 0 | Active low |
| MIPI DSI | Internal | 2-lane @ 750Mbps |
| **Touch** | | |
| Touch Reset | 21 | Reset sequence sets I2C addr |
| Touch INT | 22 | Interrupt (active low) |
| **I2C Bus** | | |
| I2C SDA | 3 | Shared bus @ 400kHz |
| I2C SCL | 8 | Shared bus @ 400kHz |
| **Audio** | | |
| I2S MCLK | 9 | Master clock |
| I2S BCLK | 13 | Bit clock |
| I2S WS | 12 | Word select |
| I2S DOUT | 10 | Data out (to codec) |
| PA Enable | 11 | NS4150 amplifier |
| **Power** | | |
| SD Power EN | 36 | Active high |

### I2C Address Map

| Device | Address | Notes |
|--------|---------|-------|
| GT911 Touch | 0x14 | Forced via reset sequence |
| ES8311 Codec | 0x18 | Audio codec |
| RX8025T RTC | 0x32 | Real-time clock |

## Testing Recommendations

### Build Verification

```bash
idf.py build
```

Expected: Clean build with no driver-related errors.

### Kconfig Testing

Disable peripherals one by one:
```bash
idf.py menuconfig
# Component config → Guition JC1060 BSP Configuration
# Disable CONFIG_BSP_ENABLE_DISPLAY
idf.py build  # Should build without display code
```

### Runtime Testing

1. **Power-on**: Verify Phase A executes (SD power sequencing)
2. **Display**: Backlight should illuminate, display should show content
3. **Touch**: Verify no I2C errors, touch should respond
4. **Audio**: Verify I2S initialized, PA control works
5. **RTC**: Verify I2C communication at 0x32

### Expected Log Output

```
I (1234) BSP: ========================================
I (1234) BSP:   Guition BSP v1.2.0-dev
I (1234) BSP:   Phase A: Power Manager
I (1234) BSP:   Phase D: Peripheral Drivers
I (1234) BSP: ========================================
I (1234) BSP: [PHASE A] Power Manager starting...
I (1234) BSP: [PHASE A] ✓ POWER_READY
I (1234) BSP: [I2C] Initializing I2C bus (SCL=8, SDA=3, 400000 Hz)
I (1234) BSP: [I2C] ✓ Bus initialized
I (1234) BSP: [PHASE D] Initializing display...
I (1234) BSP_JD9165: Initializing JD9165 display (1024x600, 2-lane DSI)
I (1234) BSP_JD9165: Display initialized successfully
I (1234) BSP: [PHASE D] ✓ Display ready
I (1234) BSP: [PHASE D] Initializing touch controller...
I (1234) BSP_GT911: Starting GT911 reset sequence (forcing address 0x14)
I (1234) BSP_GT911: GT911 initialized (address 0x14, 1024x600, 5 points)
I (1234) BSP: [PHASE D] ✓ Touch ready
I (1234) BSP: [PHASE D] ✓ All enabled peripherals initialized
I (1234) BSP: ========================================
I (1234) BSP:   BSP Initialization Complete
I (1234) BSP: ========================================
```

## Next Steps

### Immediate

1. Test build: `idf.py build`
2. Flash and verify: `idf.py flash monitor`
3. Verify all peripherals respond correctly

### Follow-up Commits

1. **Delete obsolete main/ driver files**:
   ```bash
   git rm main/display_jd9165.{c,h}
   git rm main/touch_gt911.{c,h}
   git rm main/es8311_audio.{c,h}
   git rm main/rtc_rx8025t.{c,h}
   git rm main/i2c_utils.{c,h}
   ```

2. **Update main/CMakeLists.txt**: Remove deleted files from SRCS

3. **Step 5**: Refactor Phase B (SD card) and Phase C (WiFi) into BSP

## Migration Notes for Developers

### Old API (Step 3)

```c
// main.c had to manually initialize everything
i2c_master_bus_handle_t bus = ...;
init_jd9165_display();
init_touch_gt911(bus);
es8311_init(bus);
rtc_rx8025t_init(bus);
```

### New API (Step 4)

```c
// Single call handles everything
bsp_board_init();

// Peripherals are ready to use
// Access via BSP public API if needed:
#include "jd9165_bsp.h"
bsp_display_set_brightness(75);
```

## Known Issues / TODOs

- [ ] ES8311 codec register configuration incomplete (placeholder)
- [ ] Volume control API needs full ES8311 register implementation
- [ ] RTC alarm/interrupt functionality not yet exposed
- [ ] Display brightness control could be exposed via BSP public API
- [ ] Touch calibration not implemented

## References

- **Guition JC1060P470C V1.0 Schematics**: Pin assignments verified
- **JD9165 Datasheet**: MIPI configuration, timing parameters
- **GT911 Datasheet**: Reset sequence for address selection
- **ES8311 Datasheet**: I2C registers (partial implementation)
- **RX8025T Datasheet**: BCD time format, control registers

---

**Commit**: Refactor Phase 3 - Step 4: Relocated hardware drivers to BSP component and unified Kconfig management  
**Date**: 2026-03-02  
**Author**: Cristiano Gorla
