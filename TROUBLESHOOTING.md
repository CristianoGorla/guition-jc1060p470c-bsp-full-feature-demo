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
#define ENABLE_I2C_SCAN 0  // Disable I2C scan before device init
```

**Why This Works:**
1. GT911 uses a specific reset sequence to lock its I2C address
2. The address (0x14 vs 0x5D) is determined by the INT pin state during reset
3. I2C scan operations send probe commands that wake the chip prematurely
4. This breaks the driver's carefully timed reset sequence
5. Disabling scan allows the driver to execute a clean reset

**Successful Output:**
```
I (1686) GT911: I2C address initialization procedure skipped - using default GT9xx setup
I (1713) GT911: TouchPad_ID:0x39,0x31,0x31
I (1713) GT911: TouchPad_Config_Version:99
I (1713) GT911: ✓ GT911 initialized successfully
I (1714) GT911:   Resolution: 1024x600
I (1718) GT911:   Driver auto-detected I2C address
I (1722) GT911:   Touch ready for reading
I (1830) GUITION_MAIN: GT911 active at 0x14 (INT=HIGH during reset)
```

---

## ESP-Hosted WiFi/BLE Interrupt Configuration

### Hardware Schematic Analysis (JC1060P470C Board)

**Control Lines Between ESP32-C6 and ESP32-P4:**

```
Function            C6 Pin      P4 Pin      Description
─────────────────────────────────────────────────────────
Interrupt/OOB       GPIO 2  →   GPIO 6      Data ready signal
Reset               CHIP_PU →   GPIO 54     Hardware reset
Debug (unused)      GPIO 9  →   JP1 Pin 18  External header only
```

**Important:** The C6 GPIO9 signal is **NOT** internally connected to any P4 GPIO. It routes only to external header JP1 for debug purposes.

### Configuration Required

In `sdkconfig.defaults`:
```
CONFIG_ESP_HOSTED_DATA_READY_GPIO=6
CONFIG_ESP_HOSTED_RESETPIN_GPIO=54
```

**Why GPIO6 Matters:**
- Without interrupt configuration, ESP-Hosted uses polling mode (less efficient)
- With GPIO6 configured, the C6 can wake the P4 when data is ready
- Improves WiFi/BLE performance and reduces power consumption

**Expected Boot Log:**
```
I (2287) os_wrapper_esp: GPIO [54] configured  // Reset pin
I (xxxx) os_wrapper_esp: GPIO [6] configured   // Interrupt pin (with fix)
```

---

## WiFi Connection Success (ESP-Hosted)

### Successful WiFi Connection Test

**Configuration:**
```c
// feature_flags.h
#define ENABLE_WIFI 1
#define ENABLE_WIFI_CONNECT 1  // Enable connection test

// wifi_config.h (gitignored)
#define WIFI_SSID "YourSSID"
#define WIFI_PASSWORD "YourPassword"
```

**Successful Connection Log:**
```
I (4430) GUITION_MAIN: ✓ WiFi initialized (ESP-Hosted via C6)

I (6430) GUITION_MAIN: === WiFi Connection Test ===
I (6430) GUITION_MAIN: Connecting to: FRITZ!Box 7530 WL
I (6449) H_API: esp_wifi_remote_connect
I (6470) GUITION_MAIN: Waiting for IP address (15s timeout)...
I (6836) RPC_WRAP: ESP Event: Station mode: Connected
I (7868) esp_netif_handlers: sta ip: 192.168.188.88, mask: 255.255.255.0, gw: 192.168.188.1
I (7868) GUITION_MAIN: ✓ WiFi connected!
I (7868) GUITION_MAIN:    IP Address: 192.168.188.88
I (7873) GUITION_MAIN:    Netmask:    255.255.255.0
I (7878) GUITION_MAIN:    Gateway:    192.168.188.1
I (7885) GUITION_MAIN:    RSSI: -81 dBm
```

**Key Timestamps:**
- **T+6430ms**: Connection initiated
- **T+6836ms**: Connected event (406ms connection time)
- **T+7868ms**: IP address assigned via DHCP (1032ms DHCP negotiation)
- **Total**: ~1.4 seconds from connect to IP ready

**Signal Strength (RSSI):**
```
-30 to -50 dBm: Excellent
-50 to -70 dBm: Good
-70 to -85 dBm: Fair (usable)
-85 to -90 dBm: Poor
< -90 dBm: No connection expected
```

**Note:** ESP32-C6 supports **2.4GHz only**. Ensure your router broadcasts a 2.4GHz SSID.

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

**Successful Output:**
```
I (1300) RX8025T: Initializing RX8025T RTC...
I (1304) RX8025T: I2C Address: 0x32
I (1307) RX8025T: Reading current time (gentle init)...
I (1313) RX8025T: ✓ RTC responding on I2C!
I (1316) RX8025T: Current RTC time: 2026-03-01 (wday=6) 19:16:45
I (1322) RX8025T: PON/VLF flags already clear - RTC time is valid
I (1328) RX8025T: Already in 24-hour format
I (1331) RX8025T: RX8025T initialized successfully
I (1336) GUITION_MAIN: ✓ RTC initialized successfully
```

---

## ES8311 Audio Codec Initialization

### Best Practice: Direct Initialization via Chip ID Read

**Same Pattern as GT911 and RTC:**
Validate device presence by reading chip ID register, not by pre-probing.

**Implementation:**
```c
// ❌ OLD: Pre-probe before init
ret = i2c_master_probe(bus_handle, 0x18, 500);
if (ret == ESP_OK) {
    ret = es8311_read_chip_id(bus_handle, &chip_id);
}

// ✅ NEW: Direct init via chip ID read
ret = es8311_read_chip_id(bus_handle, &chip_id);
if (ret == ESP_OK) {
    // ES8311 is present (chip_id should be 0x83)
}
```

**Successful Output:**
```
I (1148) GUITION_MAIN: === ES8311 Audio Codec ===
I (1152) ES8311: Initializing ES8311 audio codec...
I (1157) ES8311: I2C Address: 0x18 (direct init, no pre-probe)
I (1163) ES8311: ✓ ES8311 responding on I2C!
I (1167) ES8311: ES8311 Chip ID: 0x83 (expected: 0x83)
I (1172) ES8311: Performing soft reset...
I (1276) ES8311: Setting codec to power-down mode...
I (1276) ES8311: ✓ ES8311 initialized successfully (powered down, safe state)
I (1276) ES8311: Note: PA power pin GPIO11 not configured (needs I2S setup)
I (1283) GUITION_MAIN: ✓ ES8311 initialized (powered down)
```

**Hardware Configuration:**
- I2C Address: 0x18 (7-bit)
- PA Power Amplifier: GPIO11 (NS4150B enable, active HIGH)
- Expected Chip ID: 0x83

**For Full Audio:**
This driver only performs I2C initialization. For audio playback/recording:
```bash
idf.py add-dependency "espressif/esp_codec_dev^1.5.4"
```

Official BSP provides:
- I2S data interface
- Volume control
- Sample rate config
- PA power management
- Microphone/Speaker routing

---

## Complete Hardware Pinout

### I2C Bus (Shared)
```
GPIO7  - SDA (all I2C devices)
GPIO8  - SCL (all I2C devices)
```

### I2C Devices
```
0x14 - GT911 Touch (INT=HIGH config)
0x18 - ES8311 Audio Codec
0x32 - RX8025T RTC
```

### GT911 Touch Controller
```
GPIO7  - SDA
GPIO8  - SCL
GPIO21 - RST (hardware reset)
GPIO22 - INT (interrupt, determines I2C address)
```

### ES8311 Audio Codec
```
GPIO7  - SDA
GPIO8  - SCL
GPIO11 - PA_CTRL (power amplifier enable)
```

### Display JD9165 (MIPI DSI)
```
GPIO45-52 - MIPI DSI interface (dedicated)
```

### SDMMC Slot 0 (SD Card)
```
GPIO43 - CLK
GPIO44 - CMD
GPIO39 - D0
GPIO40 - D1
GPIO41 - D2
GPIO42 - D3
GPIO45 - Power enable
```

### SDMMC Slot 1 (ESP-Hosted WiFi/BLE)
```
GPIO18 - CLK
GPIO19 - CMD
GPIO14 - D0
GPIO15 - D1
GPIO16 - D2
GPIO17 - D3
GPIO54 - ESP32-C6 reset
```

---

## Complete Initialization Sequence (Final)

**Optimized Boot Order:**
```
1. NVS Flash Init
2. I2C Bus Init (GPIO7/8 @ 400kHz)
3. ❌ SKIP I2C Scan (prevents device wake-up)
4. ES8311 Audio Init (direct init at 0x18)
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

## Complete System Boot Log (All Devices Working)

**Date: 2026-03-01 19:16 CET**

```
I (1120) GUITION_MAIN: ========================================
I (1126) GUITION_MAIN:    Guition JC1060P470C Initialization
I (1132) GUITION_MAIN: ========================================

I (1141) GUITION_MAIN: === I2C Bus Initialization ===
I (1142) GUITION_MAIN: ✓ I2C bus ready (SDA=GPIO7, SCL=GPIO8)

I (1148) GUITION_MAIN: === ES8311 Audio Codec ===
I (1152) ES8311: Initializing ES8311 audio codec...
I (1157) ES8311: I2C Address: 0x18 (direct init, no pre-probe)
I (1163) ES8311: ✓ ES8311 responding on I2C!
I (1167) ES8311: ES8311 Chip ID: 0x83 (expected: 0x83)
I (1172) ES8311: Performing soft reset...
I (1276) ES8311: Setting codec to power-down mode...
I (1276) ES8311: ✓ ES8311 initialized successfully (powered down, safe state)
I (1276) ES8311: Note: PA power pin GPIO11 not configured (needs I2S setup)
I (1283) GUITION_MAIN: ✓ ES8311 initialized (powered down)

I (1288) GUITION_MAIN: === RTC Initialization ===
I (1293) GUITION_MAIN: RTC driver will validate device at 0x32 (no pre-probe)
I (1300) RX8025T: Initializing RX8025T RTC...
I (1304) RX8025T: I2C Address: 0x32
I (1307) RX8025T: Reading current time (gentle init)...
I (1313) RX8025T: ✓ RTC responding on I2C!
I (1316) RX8025T: Current RTC time: 2026-03-01 (wday=6) 19:16:45
I (1322) RX8025T: PON/VLF flags already clear - RTC time is valid
I (1328) RX8025T: Already in 24-hour format
I (1331) RX8025T: RX8025T initialized successfully
I (1336) GUITION_MAIN: ✓ RTC initialized successfully
I (1342) GUITION_MAIN: Current time: 2026-03-01 19:16:45

I (1353) GUITION_MAIN: === Display Initialization ===
I (1358) JD9165: Initializing JD9165 display
I (1660) JD9165: Display initialized (1024x600 @ 52MHz, 2-lane DSI, HBP=136)
I (1660) GUITION_MAIN: ✓ Display ready (1024x600 MIPI DSI)

I (1661) GUITION_MAIN: === Touch Controller Initialization ===
I (1666) GUITION_MAIN: GT911 driver will auto-reset and detect I2C address (0x14 or 0x5D)
I (1674) GT911: Initializing GT911 touch controller
I (1686) GT911: I2C address initialization procedure skipped - using default GT9xx setup
I (1713) GT911: TouchPad_ID:0x39,0x31,0x31
I (1713) GT911: TouchPad_Config_Version:99
I (1713) GT911: ✓ GT911 initialized successfully
I (1714) GT911:   Resolution: 1024x600
I (1718) GT911:   Driver auto-detected I2C address
I (1722) GT911:   Touch ready for reading
I (1726) GUITION_MAIN: ✓ Touch controller ready
I (1830) GUITION_MAIN: GT911 active at 0x14 (INT=HIGH during reset)

I (1835) GUITION_MAIN: ========================================
I (1841) GUITION_MAIN:    System Initialization Complete
I (1846) GUITION_MAIN: ========================================
```

**Summary:**
- ✅ I2C Bus: GPIO7/8 @ 400kHz
- ✅ ES8311 Audio: 0x18 (Chip ID: 0x83)
- ✅ RTC RX8025T: 0x32 (time valid)
- ✅ Display: 1024x600 MIPI DSI
- ✅ Touch GT911: 0x14 (TouchPad ID: 911)

**All I2C devices initialized successfully using direct init pattern!**

---

## Feature Flags Configuration

**Working Configuration for Full System:**
```c
#define ENABLE_I2C 1           // Required for I2C devices
#define ENABLE_I2C_SCAN 0      // ❌ MUST BE DISABLED

#define ENABLE_AUDIO 1         // ✅ ES8311 audio codec
#define DEBUG_AUDIO 1          // Show detailed logs

#define ENABLE_RTC 1           // ✅ RX8025T RTC
#define DEBUG_RTC 1            // Show detailed logs
#define ENABLE_RTC_TEST 1      // Show time on boot

#define ENABLE_DISPLAY 1       // ✅ MIPI DSI display
#define ENABLE_DISPLAY_TEST 0  // Disable RGB patterns

#define ENABLE_TOUCH 1         // ✅ GT911 touch
#define DEBUG_TOUCH 1          // Show detailed logs
#define ENABLE_TOUCH_TEST 0    // Disable continuous read
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
The I2C bus issues were caused by **I2C scan timing**, not MIPI DSI initialization. The display and I2C devices can be initialized in sequence without conflicts.

---

## References

- **Guition Official Demo:** `JC1060P470C_I_W_Y/Demo_IDF/ESP-IDF/lvgl_demo_v9`
- **ESP-IDF GT911 Driver:** `components/esp_lcd_touch_gt911`
- **ESP-IDF ES8311 Component:** https://components.espressif.com/components/espressif/es8311
- **ESP Codec Dev BSP:** https://components.espressif.com/components/espressif/esp_codec_dev
- **Board BSP:** `esp32_p4_function_ev_board.h`
- **RTC Datasheet:** Epson RX8025T Real-Time Clock Module
- **ES8311 Datasheet:** http://www.everest-semi.com/pdf/ES8311%20PB.pdf

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
   - Applied to GT911, RTC, and ES8311

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
                  ✅ All devices working
                  ✅ SD + WiFi coexisting
```
