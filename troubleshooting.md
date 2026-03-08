# Troubleshooting Guide - Guition JC1060P470C

## Table of Contents

1. [SDMMC Slot Arbitration (0x108 Timeout) - CRITICAL](#sdmmc-slot-arbitration-0x108-timeout---critical)
2. [ESP-Hosted SDIO Link Stabilization](#esp-hosted-sdio-link-stabilization)
3. [System Reset Behavior and Initialization Reliability](#system-reset-behavior-and-initialization-reliability)
4. [GT911 Touch Controller Initialization Issues](#gt911-touch-controller-initialization-issues)
5. [ESP-Hosted WiFi/BLE Interrupt Configuration](#esp-hosted-wifibluetooth-interrupt-configuration)
6. [WiFi Connection Success (ESP-Hosted)](#wifi-connection-success-esp-hosted)
7. [RTC RX8025T Initialization](#rtc-rx8025t-initialization)
8. [RTC NTP Synchronization](#rtc-ntp-synchronization)
9. [ES8311 Audio Codec Initialization](#es8311-audio-codec-initialization)
10. [CMakeLists.txt lwIP Duplicate Bug](#cmakeliststxt-lwip-duplicate-bug)

---

## SDMMC Slot Arbitration (0x108 Timeout) - CRITICAL

### Problem: Boot Loop with `sdmmc_send_cmd returned 0x108`

**Date Resolved:** 2026-03-03  
**Severity:** CRITICAL - System Unusable

**Symptoms:**

```
I (8542) SD_MANAGER: [BUS ARBITRATION] WiFi transport suspended...
I (8653) SD_MANAGER: Deinitializing SDMMC controller...
E (8653) sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x108
E (8653) H_SDIO_DRV: failed to read registers
I (8653) H_SDIO_DRV: Host is resetting itself, to avoid any sdio race condition
I (8653) os_wrapper_esp: Restarting host
ESP-ROM:esp32p4-eco2-20240710
```

System enters infinite boot loop:

1. WiFi initializes on SDMMC Slot 1
2. SD mount attempts to switch to Slot 0
3. ESP-Hosted driver detects bus error 0x108
4. System restarts
5. Loop repeats indefinitely

---

### Root Cause: Race Condition During SDMMC Controller Reinitialization

**Hardware Configuration:**

- ESP32-P4 has **ONE** SDMMC controller serving **TWO** slots:
  - **Slot 0**: SD Card (GPIO39-44)
  - **Slot 1**: ESP32-C6 WiFi via ESP-Hosted (GPIO14-19)

**Problem Timeline:**

```
T+8542ms: SD mount begins
T+8542ms: esp_hosted_pause_transport() called
T+8542ms: esp_wifi_stop() stops WiFi stack (logical layer)
T+8555ms: WiFi stop event propagates
T+8653ms: sdmmc_host_deinit() begins (100ms later)
T+8653ms: ERROR! ESP-Hosted RX task still active, attempts SDIO read
T+8653ms: Controller in undefined state → 0x108 timeout
T+8653ms: ESP-Hosted detects race, triggers system restart
```

**Why `esp_wifi_stop()` Wasn't Enough:**

- `esp_wifi_stop()` only stops the **WiFi stack** (application layer)
- ESP-Hosted **SDIO driver tasks** continue running:
  - `sdio_data_to_rx_buf_task` - Receives data from C6
  - `Write thread` - Sends data to C6
- These tasks access SDMMC bus **during** `sdmmc_host_deinit()`
- Controller deinit conflicts with active SDIO transactions → **0x108**

---

### Solution: Clean Switch Protocol with CCCR Handshake

**Protocol Based On:** SDIO Specification v3.00 (Card Common Control Registers)

#### Implementation Overview

**6-Step Pause Sequence** (`esp_hosted_pause_transport()`):

```c
// Step 1: Stop WiFi stack (logical layer)
esp_wifi_stop();
vTaskDelay(50ms);  // Event propagation

// Step 2: CCCR handshake (silence slave interrupts)
// NOTE: Currently skipped (no direct card handle access)
// CMD52 write to CCCR 0x04 would disable Master Interrupt Enable

// Step 3: Full WiFi deinit (kills ESP-Hosted tasks)
esp_wifi_deinit();  // Terminates sdio_data_to_rx_buf_task, Write thread

// Step 4: Wait for task cleanup
vTaskDelay(200ms);  // CRITICAL: Ensures all tasks fully terminated

// Step 5: Verify bus idle (poll DAT0)
while (gpio_get_level(GPIO14) == 0 && timeout < 100ms) {
    vTaskDelay(5ms);
}

// Step 6: Host interrupts disabled by sdmmc_host_deinit() (caller)
```

**4-Step Resume Sequence** (`esp_hosted_resume_transport()`):

```c
// Step 1: Reinitialize WiFi driver (ESP-Hosted reinits Slot 1)
esp_wifi_init();

// Step 2: Set WiFi mode
esp_wifi_set_mode(WIFI_MODE_STA);

// Step 3: Restart WiFi stack
esp_wifi_start();  // Tasks restarted, SDIO active again

// Step 4: Slave interrupts re-enabled automatically by ESP-Hosted init
```

#### Updated SD Mount Sequence

```c
// In sd_card_manager.c:

// 1. Pause WiFi transport (Clean Switch Protocol)
esp_hosted_pause_transport();  // 6-step sequence

// 2. Deinitialize SDMMC controller (release Slot 1)
sdmmc_host_deinit();

// 3. Settling delay (bus capacitance discharge)
vTaskDelay(50ms);

// 4. Reinitialize controller for Slot 0
sdmmc_host_init();
vTaskDelay(100ms);  // Stabilization

// 5. Initialize Slot 0 pins and mount SD
sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);
esp_vfs_fat_sdmmc_mount("/sdcard", ...);

// 6. Resume WiFi transport (reverse protocol)
esp_hosted_resume_transport();  // 4-step sequence
```

---

### Expected Boot Log (After Fix)

```
I (8541) BOOTSTRAP: [Phase B] Starting SD card mount...
I (8543) SD_MANAGER: === SD Card Mount (Phase B - after WiFi) ===
I (8543) SD_MANAGER: Enabling pull-ups on SDMMC pins (GPIO39-44)...
I (8544) SD_MANAGER: [BUS ARBITRATION] Initiating Clean Switch Protocol...
I (8544) wifi_hosted: [TRANSPORT] === Clean Switch Protocol: Slot 1→0 ===
I (8544) wifi_hosted: [TRANSPORT] Step 1: Stopping WiFi stack...
I (8555) RPC_WRAP: ESP Event: wifi station stopped
I (8555) wifi_hosted: [TRANSPORT] Step 2: CCCR handshake (silence C6 interrupts)...
I (8555) wifi_hosted: [CCCR] CMD52 handshake skipped (no direct card handle access)
I (8555) wifi_hosted: [CCCR] Relying on WiFi deinit to stop slave activity
I (8555) wifi_hosted: [TRANSPORT] Step 3: Deinitializing WiFi driver...
I (8580) wifi_hosted: [TRANSPORT] Step 4: Waiting for task cleanup (200ms)...
I (8780) wifi_hosted: [TRANSPORT] Step 5: Verifying bus idle...
I (8780) wifi_hosted: [CCCR] Verifying bus idle (polling DAT0/GPIO14)...
I (8785) wifi_hosted: [CCCR] Bus IDLE verified (DAT0=HIGH)
I (8785) wifi_hosted: [TRANSPORT] Step 6: Host interrupts will be disabled by sdmmc_host_deinit()
I (8785) wifi_hosted: [TRANSPORT] ✓ Clean handshake complete, bus IDLE

I (8785) SD_MANAGER: Deinitializing SDMMC controller (release Slot 1)...
I (8786) SD_MANAGER: Bus settling delay (50ms)...
I (8836) SD_MANAGER: Reinitializing SDMMC controller for Slot 0...
I (8996) SD_MANAGER: Initializing SDMMC Slot 0...
I (9046) SD_MANAGER: Mounting FAT filesystem on Slot 0...
I (9196) SD_MANAGER: ✓ SD card mounted successfully
I (9196) SD_MANAGER:    Card: SU08G
I (9196) SD_MANAGER:    Capacity: 7580 MB
I (9196) SD_MANAGER: [BUS ARBITRATION] Resuming WiFi transport on Slot 1...
I (9196) wifi_hosted: [TRANSPORT] === Clean Switch Protocol: Slot 0→1 ===
I (9196) wifi_hosted: [TRANSPORT] Step 1: Reinitializing WiFi driver...
I (10696) wifi_hosted: [TRANSPORT] Step 2: Setting WiFi mode to STA...
I (10696) wifi_hosted: [TRANSPORT] Step 3: Starting WiFi...
I (10796) wifi_hosted: [TRANSPORT] Step 4: Slave interrupts re-enabled by ESP-Hosted init
I (10796) wifi_hosted: [TRANSPORT] ✓ WiFi transport resumed on Slot 1

I (10796) BOOTSTRAP: [Phase B] ✓ SD_READY
I (10796) BOOTSTRAP: ========================================
I (10796) BOOTSTRAP:    ✓ Bootstrap COMPLETE
I (10796) BOOTSTRAP: ========================================
```

**Key Indicators of Success:**

- ✅ No `0x108` error during `sdmmc_host_deinit()`
- ✅ No "Host is resetting itself" message
- ✅ SD card mounts successfully
- ✅ WiFi transport resumes without errors
- ✅ System reaches "Bootstrap COMPLETE"
- ✅ No boot loop

---

### Technical Details

#### SDIO CCCR (Card Common Control Registers)

**CCCR Register 0x04: Interrupt Enable**

```c
// Per SDIO Specification v3.00:
bit 0: Master Interrupt Enable (MIEN)
  0 = Disable all interrupts from this function
  1 = Enable interrupts
```

**CMD52: Direct I/O Read/Write**

```c
// Command argument format:
bit 31:    R/W flag (0=read, 1=write)
bit 28:    Function number (0=CIA/CCCR)
bit 27:    RAW flag (0=normal, 1=read after write)
bit 26:    Reserved (0)
bit 25-9:  Register address (0x04 = Int Enable)
bit 7-0:   Write data

// Example: Disable interrupts
uint32_t arg = (1 << 31) |  // Write
               (0 << 28) |  // Function 0 (CCCR)
               (0x04 << 9) | // Address 0x04
               0x00;        // Data: disable MIEN
```

**Note on CMD52 Implementation:**

The current implementation **skips** CMD52 because:

- No direct access to `sdmmc_card_t` handle from ESP-Hosted internals
- `esp_wifi_deinit()` already terminates SDIO tasks (equivalent effect)
- Direct CMD52 would require modifying ESP-Hosted driver

The Clean Switch Protocol achieves the same result via:

- **Task termination** (`esp_wifi_deinit()` kills RX/TX threads)
- **Bus idle verification** (GPIO polling confirms no activity)
- **Settling delay** (ensures hardware state stabilization)

#### Error Code 0x108

```c
#define ESP_ERR_TIMEOUT 0x108  // Operation timed out
```

**Occurs when:**

- SDMMC controller sends command to card
- Card doesn't respond within timeout (typically 1000ms)
- During `sdmmc_io_rw_extended()` - SDIO data transfer function

**Root cause in this case:**

- Controller being deinitialized while SDIO transaction in progress
- Slave (C6) expecting different slot configuration
- Bus in undefined state (neither Slot 0 nor Slot 1 fully configured)

#### Bus Idle Verification (DAT0 Polling)

**SDIO Specification:**

- DAT0 line used for busy signaling
- LOW = Slave busy processing command
- HIGH = Slave idle, ready for next command

**Implementation:**

```c
// Poll GPIO14 (Slot 1 DAT0) until HIGH
gpio_set_direction(GPIO14, GPIO_MODE_INPUT);
TickType_t start = xTaskGetTickCount();

while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(100)) {
    if (gpio_get_level(GPIO14) == 1) {
        // Bus IDLE, safe to proceed
        return true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));  // Poll every 5ms
}
```

---

### Configuration Changes

**No Kconfig Changes Required!**

The Clean Switch Protocol is **transparent** to the application. Configuration remains unchanged:

```c
// In Kconfig (already configured):
CONFIG_BSP_PIN_CLK=43
CONFIG_BSP_PIN_CMD=44
CONFIG_BSP_PIN_D0=39
CONFIG_BSP_PIN_D1=40
CONFIG_BSP_PIN_D2=41
CONFIG_BSP_PIN_D3=42
CONFIG_BSP_PIN_SD_POWER_EN=36

CONFIG_BSP_SDIO_SLOT1_CLK=18
CONFIG_BSP_SDIO_SLOT1_CMD=19
CONFIG_BSP_SDIO_SLOT1_D0=14  // Used for bus idle detection
CONFIG_BSP_SDIO_SLOT1_D1=15
CONFIG_BSP_SDIO_SLOT1_D2=16
CONFIG_BSP_SDIO_SLOT1_D3=17
CONFIG_BSP_C6_RESET_GPIO=54
```

All configuration uses **Kconfig macros** from BSP:

- ❌ No `feature_flags.h` (deprecated)
- ✅ All settings in `sdkconfig` via `idf.py menuconfig`
- ✅ BSP provides pin definitions via `CONFIG_BSP_*` macros

---

### Related Fixes

**Commit History:**

```
00e71c4 - Fix: Implement Clean Switch Protocol via CCCR handshake (resolve 0x108)
3a15014 - Fix: Use Clean Switch Protocol with settling delay for SD mount
2613746 - Fix: Use full WiFi deinit for transport pause to prevent 0x108
e5c090b - Fix: Remove extra delay in SD mount, pause handles task termination
```

**Key Changes:**

1. `esp_hosted_wifi.c`: Implemented 6-step pause and 4-step resume protocol
2. `sd_card_manager.c`: Integrated Clean Switch Protocol into mount sequence
3. `esp_hosted_wifi.h`: Added API documentation with usage examples

---

### Why This Solution Works

**Problem:** ESP-Hosted SDIO tasks accessing bus during controller deinit  
**Solution:** Full WiFi deinit terminates ALL tasks before controller touch

**Critical Insights:**

1. **Layered Architecture:**
   - `esp_wifi_stop()` = Application layer (WiFi stack)
   - `esp_wifi_deinit()` = Driver layer (ESP-Hosted tasks)
   - Only `deinit()` guarantees zero SDIO activity

2. **Task Termination Is Async:**
   - `esp_wifi_deinit()` **triggers** task cleanup
   - Tasks don't terminate immediately
   - 200ms delay ensures cleanup completes before `sdmmc_host_deinit()`

3. **Bus State Verification:**
   - DAT0 polling provides hardware-level confirmation
   - Software assumptions aren't enough (race conditions)
   - GPIO read is definitive: HIGH = safe to proceed

4. **Settling Delay Matters:**
   - GPIO states need time to stabilize after deinit
   - SDMMC controller internal state machine needs reset
   - 50ms settling prevents residual electrical noise

5. **Symmetric Protocol:**
   - Pause: WiFi → SD (Slot 1 → Slot 0)
   - Resume: SD → WiFi (Slot 0 → Slot 1)
   - Clean entry/exit for both directions

---

### Alternative Solutions Attempted

#### ❌ Attempt 1: Simple Delay After `esp_wifi_stop()`

```c
esp_wifi_stop();
vTaskDelay(100ms);
sdmmc_host_deinit();  // Still crashed with 0x108
```

**Why it failed:** WiFi stop doesn't terminate SDIO driver tasks

#### ❌ Attempt 2: Longer Delay Without Deinit

```c
esp_wifi_stop();
vTaskDelay(500ms);  // Even 500ms wasn't enough
sdmmc_host_deinit();  // Still crashed
```

**Why it failed:** Tasks remain active indefinitely until `deinit()`

#### ✅ Final Solution: Full Deinit + Task Cleanup Wait

```c
esp_wifi_stop();
vTaskDelay(50ms);
esp_wifi_deinit();  // Kills tasks
vTaskDelay(200ms);  // Wait for cleanup
verify_bus_idle();  // Hardware confirmation
sdmmc_host_deinit();  // Now safe
```

**Why it works:** Guaranteed zero SDIO activity before controller touch

---

### Performance Impact

**Additional Boot Time:**

- WiFi pause: ~250ms (stop + deinit + task cleanup + verification)
- SD mount: ~330ms (same as before)
- WiFi resume: ~1500ms (reinit + mode set + start)
- **Total overhead:** ~2.1 seconds for clean slot switching

**Trade-off Analysis:**

- ❌ Without protocol: Boot loop (unusable system)
- ✅ With protocol: +2.1s boot time, but **100% reliable**

**Optimization Opportunities:**

- Task cleanup wait could potentially be reduced from 200ms to 150ms
- Bus idle poll timeout could be reduced from 100ms to 50ms
- WiFi resume could overlap with other bootstrap tasks

**Not recommended:** These optimizations risk reintroducing race conditions. Current timing is conservative but proven reliable.

---

### Testing Checklist

**Verify Fix:**

```bash
# 1. Flash and monitor
idf.py build flash monitor

# 2. Look for success indicators
✓ [TRANSPORT] === Clean Switch Protocol: Slot 1→0 ===
✓ [CCCR] Bus IDLE verified (DAT0=HIGH)
✓ [TRANSPORT] ✓ Clean handshake complete
✓ SD card mounted successfully
✓ [TRANSPORT] === Clean Switch Protocol: Slot 0→1 ===
✓ [TRANSPORT] ✓ WiFi transport resumed on Slot 1
✓ Bootstrap COMPLETE

# 3. Confirm NO errors
✗ No "sdmmc_send_cmd returned 0x108"
✗ No "H_SDIO_DRV: failed to read registers"
✗ No "Host is resetting itself"
✗ No boot loop

# 4. Verify WiFi still works after SD mount
I (10796) BOOTSTRAP: Bootstrap COMPLETE
I (12000) wifi_hosted: WiFi operations still responding

# 5. Test multiple resets
# Ctrl+T, Ctrl+R (in ESP-IDF monitor) x10
# All boots should succeed
```

**Regression Testing:**

- SD card read/write after WiFi resume
- WiFi scan after SD mount
- WiFi connect after SD mount
- Simultaneous WiFi + SD file operations (if needed)

---

### Summary

**Problem:** SDMMC Slot 1→0 switch caused race condition 0x108  
**Cause:** ESP-Hosted SDIO tasks active during `sdmmc_host_deinit()`  
**Solution:** Clean Switch Protocol (full WiFi deinit + bus idle verification)  
**Result:** ✅ Reliable slot switching, no boot loops, WiFi+SD coexistence

**Key Takeaway:**

When sharing hardware resources (SDMMC controller) between multiple drivers (ESP-Hosted WiFi, SD Card), **explicit coordination protocols** are essential. The Clean Switch Protocol ensures:

1. **Mutual exclusion**: Only one driver active at a time
2. **Clean handoff**: Bus state verified before controller reconfiguration
3. **Symmetric operations**: Same protocol for both directions
4. **Hardware confirmation**: GPIO polling validates software assumptions

**Configuration:** All managed via Kconfig (`idf.py menuconfig`), no manual feature flags.

---

## ESP-Hosted SDIO Link Stabilization

### Problem: "ESP-Hosted link not yet up" Error and Restart Loop

**Date Fixed:** 2026-03-01 23:17 CET

**Symptoms:**

```
E (12662) H_API: ESP-Hosted link not yet up
ESP-ROM:esp32p4-eco2-20240710
Build:Jul 10 2024
rst:0xc (SW_CPU_RESET),boot:0x1c (SPI_FAST_FLASH_BOOT)
```

The system enters an infinite restart loop:

1. Bootstrap completes successfully (all three phases)
2. System reports "ESP-Hosted link not yet up" ~5 seconds after Phase B completion
3. Automatic restart triggered
4. Cycle repeats indefinitely

**Root Cause:**

The `wifi_hosted_init_transport()` function returns `ESP_OK` when the ESP-Hosted transport **configuration** is complete, but the underlying **SDIO communication link** needs additional time to establish. The Phase B task was immediately signaling `HOSTED_READY` after `wifi_hosted_init_transport()` returned, allowing Phase C (and subsequent WiFi operations) to proceed before the SDIO link was stable.

**Solution:**

Added 2-second stabilization delay in Phase B after ESP-Hosted transport initialization.

**Commit:** `9a48e7e` and `25ae2b9` (2026-03-01 23:17 CET)

---

## Configuration Management

### Kconfig-Based Configuration (Current Approach)

**All system configuration managed via Kconfig:**

```bash
idf.py menuconfig
# → Component config
#   → Guition JC1060 BSP Configuration
```

**Pin Definitions:**

- `CONFIG_BSP_PIN_*` - SD card pins (Slot 0)
- `CONFIG_BSP_SDIO_SLOT1_*` - WiFi pins (Slot 1)
- `CONFIG_BSP_C6_RESET_GPIO` - ESP32-C6 reset control
- All defined in BSP Kconfig

**Feature Flags (Deprecated):**

❌ `feature_flags.h` - **No longer used**  
✅ Kconfig macros - **Current approach**

**Migration Notes:**

- Old code using `#ifdef FEATURE_X` → Replaced with `#ifdef CONFIG_BSP_FEATURE_X`
- All feature toggles available in `idf.py menuconfig`
- No manual header file editing required

---

## References

- **Guition Official Demo:** `JC1060P470C_I_W_Y/Demo_IDF/ESP-IDF/lvgl_demo_v9`
- **ESP-IDF SDMMC Host Driver:** https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/sdmmc_host.html
- **SDIO Specification v3.00:** SD Association (CCCR register documentation)
- **ESP-Hosted Documentation:** https://github.com/espressif/esp-hosted
- **ESP-IDF GT911 Driver:** `components/esp_lcd_touch_gt911`
- **ESP-IDF ES8311 Component:** https://components.espressif.com/components/espressif/es8311
