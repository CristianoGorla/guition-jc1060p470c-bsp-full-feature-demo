# ESP-Hosted C6 Reset Strategies - Testing Results

**Board:** Guition JC1060P470C (ESP32-P4 + ESP32-C6)  
**Date:** 2026-03-02  
**Issue:** Boot loop behavior with different reset strategies

---

## Problem Statement

ESP-Hosted provides multiple strategies for resetting the ESP32-C6 slave. Different configurations produce **different boot behaviors**:

- Some cause **boot loops** (infinite restart cycle)
- Some work reliably after initial flash
- Some work on first boot but fail on subsequent resets

---

## Reset Strategies Tested

### Strategy 1: Conditional Reset (Reset Slave When Necessary)

**Configuration:**
```c
// ESP-Hosted transport configuration
#define RESET_HOST_IF_NEEDED    1  // Host auto-reset on errors
#define RESET_SLAVE_IF_NEEDED   1  // Conditional C6 reset
```

**Behavior:**
- ✅ **Initial flash**: Works perfectly
- ❌ **Subsequent reboots**: Boot loop
- ❌ **Hardware button reset**: Boot loop
- ❌ **IDF monitor restart**: Boot loop

**Boot Loop Pattern:**
```
I (4592) wifi_hosted: Initializing WiFi driver...
I (4592) transport: Attempt connection with slave: retry[0]
I (4692) transport: Started host communication init timer of 5000 millisec
W (4692) ldo: The voltage value 0 is out of the recommended range [500, 2700]
I (4692) sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit...
[... SDIO init ...]
E (9692) transport: slave not ready even after 5000 millisec
E (9692) H_API: ESP-Hosted transport init failed
[SYSTEM RESTART]
```

**Root Cause Hypothesis:**
- C6 **not reset** on P4 reboot (conditional logic decides "not necessary")
- C6 still in **previous state** from last boot
- SDIO handshake fails because C6 state machine is **out of sync**
- Timeout triggers restart
- Cycle repeats indefinitely

---

### Strategy 2: Forced Reset Every Boot (TESTING)

**Configuration:**
```c
// ESP-Hosted transport configuration
#define RESET_HOST_IF_NEEDED    ?  // TBD
#define RESET_SLAVE_ALWAYS      1  // Force C6 reset EVERY boot
```

**Expected Behavior:**
- C6 reset **unconditionally** on every P4 boot
- More deterministic (C6 always starts from known state)
- Should prevent boot loop issue

**Status:** 🧪 **Testing in progress...**

---

## Boot Logs Comparison

### Working Boot (After Initial Flash)

```
I (4592) wifi_hosted: Initializing WiFi driver...
I (4592) transport: Attempt connection with slave: retry[0]
W (4592) H_SDIO_DRV: Reset slave using GPIO[54]  ← C6 RESET
I (4591) os_wrapper_esp: GPIO [54] configured
W (9611) ldo: The voltage value 0 is out of the recommended range [500, 2700]
I (9611) sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit...
I (9647) sdio_wrapper: Function 0 Blocksize: 512
I (9648) sdio_wrapper: Function 1 Blocksize: 512
I (9748) H_SDIO_DRV: Card init success, TRANSPORT_RX_ACTIVE
I (9807) H_SDIO_DRV: SDIO Host operating in STREAMING MODE
I (9835) transport: Received INIT event from ESP32 peripheral
I (9835) transport: Identified slave [esp32c6]
[... continues successfully ...]
```

**Key:** `W (4592) H_SDIO_DRV: Reset slave using GPIO[54]` - **C6 IS RESET**

### Boot Loop (Conditional Reset, Second Boot)

```
I (4592) wifi_hosted: Initializing WiFi driver...
I (4592) transport: Attempt connection with slave: retry[0]
I (4692) transport: Started host communication init timer of 5000 millisec  ← NO RESET!
W (4692) ldo: The voltage value 0 is out of the recommended range [500, 2700]
I (4692) sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit...
I (4727) sdio_wrapper: Function 0 Blocksize: 512
I (4728) sdio_wrapper: Function 1 Blocksize: 512
I (4828) H_SDIO_DRV: Card init success, TRANSPORT_RX_ACTIVE
I (4908) H_SDIO_DRV: SDIO Host operating in STREAMING MODE
E (4913) H_SDIO_DRV: Dropping packet(s) from stream  ← C6 SENDS GARBAGE
E (4913) H_SDIO_DRV: Failed to push data to rx queue
[... timeout after 5s ...]
E (9692) transport: slave not ready even after 5000 millisec
E (9692) H_API: ESP-Hosted transport init failed
[SYSTEM RESTART]
```

**Key:** No `Reset slave using GPIO[54]` message - **C6 NOT RESET**

---

## Analysis: Why Conditional Reset Fails

### C6 State Persistence

```
P4 Boot 1 (Initial Flash):
├─ P4 resets (flash operation)
├─ C6 powered on from cold state
├─ ESP-Hosted: "C6 might be in unknown state, reset it"
├─ GPIO54 LOW → HIGH (C6 reset)
├─ C6 boots from clean state
└─ ✅ SDIO handshake successful

P4 Boot 2 (Software Reset):
├─ P4 resets (Ctrl+T, Ctrl+R)
├─ C6 remains powered (never lost power)
├─ ESP-Hosted: "C6 was working before, probably fine"
├─ GPIO54 unchanged (no reset)
├─ C6 still in PREVIOUS state (not rebooted)
├─ SDIO init expects "just booted" state
├─ C6 sends data from previous session
└─ ❌ SDIO handshake fails → boot loop
```

### The Core Problem

**Conditional reset logic assumes:**
- "If C6 was working in previous boot, it's still in good state"

**Reality:**
- P4 software reset ≠ C6 reset
- C6 **never lost power**, so it's still running old firmware state
- SDIO initialization expects C6 to be in "fresh boot" state
- C6 is actually in "mid-session" state from previous P4 boot
- **State mismatch** causes communication failure

---

## Recommended Solution

### Strategy: Force C6 Reset on Every P4 Boot

**Why this works:**

1. **Deterministic State:**
   - C6 **always** starts from known state (reset vector)
   - No dependency on previous boot state
   - SDIO handshake always sees "fresh" C6

2. **Matches Hardware Behavior:**
   - On cold boot (power cycle), C6 resets
   - On warm boot (software reset), C6 should also reset
   - **Uniform behavior** across all reset types

3. **Eliminates Boot Loop:**
   - C6 never in "stale state"
   - SDIO always synchronized
   - No timeout/retry cycles

**Implementation:**
```c
// In ESP-Hosted configuration
// Force C6 reset unconditionally
if (reset_slave_always) {
    ESP_LOGW(TAG, "Reset slave using GPIO[54]");
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
}
```

---

## BSP Responsibility

### What BSP Should NOT Do

❌ **Never touch GPIO54** (C6 reset pin):
```c
// ❌ BAD: BSP manages C6 reset
void bsp_power_sequence(void) {
    gpio_set_level(GPIO_NUM_54, 0);  // ← CONFLICT!
    vTaskDelay(500);
    gpio_set_level(GPIO_NUM_54, 1);
}
```

**Why this fails:**
- BSP resets C6 at T+1.5s (during Phase A)
- ESP-Hosted tries to reset C6 at T+4.5s (during WiFi init)
- **Double reset** causes timing issues
- C6 may not be ready when ESP-Hosted expects it

### What BSP SHOULD Do

✅ **Only manage SD card power**:
```c
// ✅ GOOD: BSP minimal responsibility
void bsp_power_sequence(void) {
    // SD card power control only
    gpio_set_level(GPIO_NUM_36, 1);  // SD_POWER_EN
    vTaskDelay(pdMS_TO_TICKS(50));
}
```

**Why this works:**
- BSP manages **only** GPIO36 (SD power)
- ESP-Hosted has **exclusive ownership** of GPIO54
- No conflicts, no double-reset
- Each driver manages its own pins

---

## Testing Checklist

### Test Scenarios

- [ ] **Cold boot** (power cycle)
- [ ] **IDF monitor restart** (Ctrl+T, Ctrl+R)
- [ ] **Hardware button reset**
- [ ] **Software reset** (`esp_restart()`)
- [ ] **Flash operation** (`idf.py flash`)
- [ ] **10 consecutive reboots** (stability test)

### Success Criteria

✅ **All scenarios must:**
1. C6 resets on every P4 boot (look for `Reset slave using GPIO[54]` in logs)
2. SDIO handshake completes successfully
3. WiFi initializes without errors
4. SD card mounts successfully
5. No boot loops
6. No timeouts

### Failure Indicators

❌ **Any of these means configuration is wrong:**
- `slave not ready even after 5000 millisec`
- `Dropping packet(s) from stream`
- `Failed to push data to rx queue`
- System restart loop
- C6 not reset on reboot (missing `Reset slave using GPIO[54]` log)

---

## Configuration Matrix

| Reset Strategy | Cold Boot | Soft Reset | Hardware Button | Stability | Recommended |
|----------------|-----------|------------|-----------------|-----------|-------------|
| **Conditional Reset** | ✅ Works | ❌ Boot loop | ❌ Boot loop | ❌ Unstable | ❌ No |
| **Force Reset Always** | 🧪 Testing | 🧪 Testing | 🧪 Testing | 🧪 Testing | ⏳ TBD |
| **No Auto Reset** | ❌ Fails | ❌ Fails | ❌ Fails | ❌ Broken | ❌ No |

---

## Next Steps

1. **Test "Force Reset Always" strategy**
   - Configure ESP-Hosted to reset C6 unconditionally
   - Run all test scenarios
   - Verify no boot loops

2. **Document working configuration**
   - Update `sdkconfig.defaults`
   - Update README with recommended settings
   - Add troubleshooting section

3. **Measure boot time impact**
   - Compare boot times: conditional vs forced reset
   - Document any performance differences

4. **Update BSP documentation**
   - Clarify BSP never touches GPIO54
   - Emphasize driver ownership model
   - Add warning about GPIO conflicts

---

## References

- ESP-Hosted documentation: https://github.com/espressif/esp-hosted
- ESP-IDF SDIO driver: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/sdmmc_host.html
- Guition JC1060P470C schematics (GPIO54 = C6 CHIP_PU)

---

**Status:** 🧪 Active testing  
**Last Updated:** 2026-03-02 17:46 CET
