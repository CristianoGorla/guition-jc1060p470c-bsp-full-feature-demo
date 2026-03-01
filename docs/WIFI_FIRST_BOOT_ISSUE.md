# WiFi First Boot Connection Issue

## Problem Description

**Symptom**: After flashing firmware with `idf.py flash`, the first boot fails to connect to WiFi. A manual reset via terminal makes WiFi work correctly on subsequent boots.

### First Boot (FAIL)
```
I (7897) H_API: esp_wifi_remote_connect
I (8090) RPC_WRAP: ESP Event: Station mode: Disconnected
W (22918) GUITION_MAIN: WiFi timeout
```

### Second Boot (SUCCESS)
```
I (7893) H_API: esp_wifi_remote_connect
I (8268) RPC_WRAP: ESP Event: Station mode: Connected
I (9284) GUITION_MAIN: ✓ WiFi connected!
```

## Root Cause Analysis

### Observed Differences

| Aspect | First Boot | Second Boot |
|--------|------------|-------------|
| Reset reason | `rst:0x17 (CHIP_USB_UART_RESET)` | `rst:0x3 (SW_CPU_RESET)` |
| Bootstrap | Hard reset executed | Hard reset executed |
| ESP-Hosted transport | Initialized ✓ | Initialized ✓ |
| WiFi connect | Timeout after 15s ❌ | Success in 1.5s ✓ |

### Potential Causes

1. **C6 State After Flash**
   - The C6 may retain inconsistent state after host flash
   - First boot sequence doesn't fully reset C6 firmware
   - GPIO54 reset during Phase A may not be sufficient

2. **SDIO Timing Issues**
   - 4-bit SDIO mode may have setup/hold timing violations
   - 40MHz clock may be too fast for initial sync
   - Missing handshake with C6 firmware ready signal

3. **Missing Handshake Protocol**
   - GPIO6 (C6_IO2 data ready interrupt) not checked
   - Phase C waits fixed 2s without confirming C6 ready
   - WiFi connection attempted before C6 firmware fully initialized

## Action Plan

### Phase 1: Implement GPIO6 Handshake ⭐ PRIMARY SOLUTION

**Goal**: Wait for C6 to signal "ready" via GPIO6 before attempting WiFi operations.

**Implementation**:

```c
// In bootstrap_manager.c - bootstrap_wifi_sequence()

// 1. Configure GPIO6 as input with pull-up
gpio_config_t handshake_conf = {
    .pin_bit_mask = (1ULL << GPIO_C6_IO2_HANDSHAKE),  // GPIO6
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
gpio_config(&handshake_conf);

// 2. Initialize WiFi transport (resets C6 via GPIO54)
init_wifi();

// 3. Poll GPIO6 for C6 ready signal
int timeout_ms = 0;
const int max_timeout_ms = 10000;  // 10 seconds
const int poll_interval_ms = 100;

while (timeout_ms < max_timeout_ms) {
    int level = gpio_get_level(GPIO_C6_IO2_HANDSHAKE);
    
    // Check for C6 ready signal
    // (polarity depends on C6 firmware implementation)
    if (level == 1) {  // Or check for stable state
        ESP_LOGI(TAG, "C6 handshake detected after %dms", timeout_ms);
        break;
    }
    
    vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    timeout_ms += poll_interval_ms;
}

if (timeout_ms >= max_timeout_ms) {
    ESP_LOGW(TAG, "C6 handshake timeout, proceeding anyway");
}

// 4. Additional stabilization delay
vTaskDelay(pdMS_TO_TICKS(1000));
```

**Expected Outcome**:
- WiFi connection succeeds on first boot
- Boot time may increase by 0-3s depending on C6 ready time

**Files to Modify**:
- `main/bootstrap_manager.c` (add handshake logic in `bootstrap_wifi_sequence()`)
- `main/bootstrap_manager.h` (define `GPIO_C6_IO2_HANDSHAKE` if not already defined)

### Phase 2: SDIO Mode Fallback Testing (If Phase 1 Fails)

**Goal**: Test if 1-bit SDIO mode is more stable than 4-bit for initial connection.

#### Test 2.1: Switch to 1-bit Mode

Add to `sdkconfig.defaults`:

```bash
# ESP-Hosted SDIO Bus Width
# Test 1-bit mode for stability
CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH_1=y
# CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH_4=y  # Default, disabled for test
```

**Test Procedure**:
1. Clean build: `idf.py fullclean`
2. Build & flash: `idf.py build flash`
3. Monitor first boot: does WiFi connect?
4. If YES: 1-bit mode is more stable (trade-off: ~50% throughput)
5. If NO: proceed to Phase 2.2

#### Test 2.2: Reduce SDIO Clock Frequency

Add to `sdkconfig.defaults`:

```bash
# ESP-Hosted SDIO Clock Frequency
# Reduce from 40MHz to 20MHz for stability
CONFIG_ESP_HOSTED_SDIO_FREQ=20000  # 20 MHz (default: 40000)
```

**Test Procedure**:
1. Try 4-bit @ 20MHz first
2. If stable, increment: 25MHz → 30MHz → 40MHz
3. Find maximum stable frequency

### Phase 3: Enhanced Reset Strategy (If Phase 1 & 2 Fail)

**Goal**: Treat USB UART reset like warm boot to force complete power cycle.

**Implementation**:

```c
// In bootstrap_manager.c - bootstrap_is_warm_boot()

bool bootstrap_is_warm_boot(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "Cold boot detected (power-on reset)");
            return false;
            
        case ESP_RST_USB:  // ← ADD THIS
            ESP_LOGW(TAG, "USB UART reset detected, treating as warm boot");
            return true;
            
        case ESP_RST_SW:
        case ESP_RST_PANIC:
        // ... rest of cases
            ESP_LOGW(TAG, "Warm boot detected (reset reason: %d)", reason);
            return true;
            
        default:
            ESP_LOGW(TAG, "Unknown reset reason: %d (treating as warm boot)", reason);
            return true;
    }
}
```

**Expected Outcome**:
- After flash, system performs hard reset cycle (GPIO54 + GPIO36 power-down)
- C6 starts from clean state
- May add ~500ms to first boot

## Testing Matrix

| Test | SDIO Mode | Clock | Handshake | Expected Result |
|------|-----------|-------|-----------|----------------|
| 1 | 4-bit | 40MHz | YES | ✓ Stable (preferred) |
| 2 | 1-bit | 40MHz | YES | ✓ Stable (fallback) |
| 3 | 4-bit | 20MHz | YES | ✓ Stable (debug) |
| 4 | 1-bit | 20MHz | YES | ✓ Stable (conservative) |

## Configuration Options Summary

### Current Working Config (Second Boot)
```bash
# sdkconfig.defaults
CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH_4=y    # Implicit default
CONFIG_ESP_HOSTED_SDIO_FREQ=40000       # Implicit default (40 MHz)
# No handshake check
```

### Proposed Config (Phase 1)
```bash
# Keep 4-bit @ 40MHz, add handshake
CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH_4=y
CONFIG_ESP_HOSTED_SDIO_FREQ=40000
# Handshake implemented in code
```

### Fallback Config (Phase 2.1)
```bash
# Conservative: 1-bit @ 40MHz with handshake
CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH_1=y
CONFIG_ESP_HOSTED_SDIO_FREQ=40000
```

### Debug Config (Phase 2.2)
```bash
# Ultra-conservative: 1-bit @ 20MHz with handshake
CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH_1=y
CONFIG_ESP_HOSTED_SDIO_FREQ=20000
```

## Success Criteria

### Primary Goal
- ✓ WiFi connects successfully on first boot after `idf.py flash`
- ✓ No manual reset required
- ✓ Boot time remains < 7 seconds

### Secondary Goals
- ✓ Maintain 4-bit SDIO mode (optimal throughput)
- ✓ Maintain 40MHz clock (optimal performance)
- ✓ Robust handshake for all boot scenarios

## References

- ESP-Hosted SDIO Protocol: https://github.com/espressif/esp-hosted/blob/master/esp_hosted_ng/docs/sdio_protocol.md
- ESP32 SDIO Slave: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/sdio_slave.html
- SDIO Interrupt Handling: GPIO6 (C6_IO2) mapped to P4 for data-ready signaling

## Next Steps

1. **Implement Phase 1** (GPIO6 handshake)
2. **Test first boot** after flash
3. **If success**: Document final solution and close issue
4. **If failure**: Proceed to Phase 2 (SDIO mode testing)
5. **Measure boot time** and WiFi throughput impact

## Notes

- Current bootstrap sequence is proven stable after first boot
- Issue is specific to first boot after flash via USB UART
- Manual reset via terminal always succeeds
- Transport initialization succeeds in both cases
- Only WiFi connection fails on first boot
