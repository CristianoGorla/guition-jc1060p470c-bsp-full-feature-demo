# Bootstrap Strategy - Deterministic Initialization

**Version:** 1.1.0-dev  
**Date:** 2026-03-01  
**Board:** Guition JC1060P470C (ESP32-P4 + ESP32-C6)

## Problem Statement

### Reset Issues Observed (v1.0.0-beta)

The Guition board exhibits non-deterministic initialization behavior:

| Reset Type | Symptom | Root Cause |
|------------|---------|------------|
| **Hardware button** | SD card fails (0x107) | SDMMC controller in unknown state |
| **USB disconnect** | WiFi timeout, SD fails | Power glitch, capacitors retain charge |
| **Software reset** | Bus conflicts | Peripherals maintain previous state |

**Key Insight:** The ESP32-P4 shares **one SDMMC controller** between two slots:
- **Slot 0:** SD card (data storage)
- **Slot 1:** ESP32-C6 WiFi/BLE (ESP-Hosted via SDIO)

When both initialize simultaneously or in wrong order, **bus conflicts occur**.

---

## Solution: Three-Phase Coordinated Bootstrap

### Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│  Bootstrap Manager (FreeRTOS Event Group Coordination) │
└─────────────────────────────────────────────────────────┘
          │
          ├─────────────────┬─────────────────┬─────────────────┐
          │                 │                 │                 │
     ┌────▼────┐       ┌────▼────┐       ┌────▼────┐       ┌────▼────┐
     │ Phase A │       │ Phase B │       │ Phase C │       │  Main   │
     │  Power  │──────>│  WiFi   │──────>│   SD    │──────>│  Loop   │
     │ Manager │       │ Hosted  │       │ Manager │       │  Ready  │
     └─────────┘       └─────────┘       └─────────┘       └─────────┘
      Priority 24       Priority 23       Priority 22
      
      POWER_READY       HOSTED_READY      SD_READY          ALL_READY
```

### Phase Dependencies

```
Phase A: Power Manager
  ├─> GPIO isolation (C6 reset, SD power off)
  ├─> Strapping protection (C6_IO9 high)
  ├─> Rail stabilization (100ms)
  ├─> Power-on sequence (SD first, then C6)
  └─> Signal POWER_READY
           │
           └──> Phase B: WiFi Manager
                  ├─> Wait POWER_READY
                  ├─> C6 handshake (GPIO6 monitoring)
                  ├─> SDIO transport init
                  └─> Signal HOSTED_READY
                           │
                           └──> Phase C: SD Manager
                                  ├─> Wait HOSTED_READY
                                  ├─> Enable SDMMC pull-ups
                                  ├─> Mount filesystem (safe)
                                  └─> Signal SD_READY
```

---

## Phase A: Power Management

### Responsibilities

1. **GPIO Isolation (Pre-Init Guard)**
   - Force `GPIO54` (C6_CHIP_PU) LOW → Hold ESP32-C6 in reset
   - Force `GPIO36` (SD_POWER_EN) LOW → Cut SD card power via MOSFET Q1
   - Prevents bus activity during initialization

2. **Strapping Pin Protection**
   - Force `C6_IO9` pin HIGH (via P4 GPIO, TBD)
   - Overrides external pull-down (R15 on C6 schematic)
   - Ensures C6 boots in **SPI Boot mode** (not Download mode)

3. **Power Sequencing**
   - Wait 100ms for VDD_HP/VDD_LP rail stabilization
   - Power on SD card first (`GPIO36` → HIGH)
   - Release C6 from reset (`GPIO54` → HIGH)
   - C6 firmware loads from internal SPI flash

4. **Event Signaling**
   - Set `POWER_READY` bit in event group
   - Unblocks Phase B task

### Code Example

```c
// Phase A: Power Manager Task
static void bootstrap_power_manager_task(void *arg) {
    bootstrap_manager_t *manager = arg;
    
    // Step 1: GPIO isolation
    gpio_set_level(GPIO_C6_CHIP_PU, 0);    // C6 in reset
    gpio_set_level(GPIO_SD_POWER_EN, 0);   // SD unpowered
    gpio_set_level(GPIO_C6_IO9, 1);        // SPI boot mode
    
    // Step 2: Rail stabilization
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Step 3: Power-on sequence
    gpio_set_level(GPIO_SD_POWER_EN, 1);   // SD powered
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(GPIO_C6_CHIP_PU, 1);    // C6 boot
    
    // Step 4: Signal ready
    xEventGroupSetBits(manager->event_group, POWER_READY_BIT);
    vTaskDelete(NULL);
}
```

---

## Phase B: WiFi Hosted

### Responsibilities

1. **Wait for Power Ready**
   - Block on `POWER_READY` event bit
   - Ensures C6 is powered and booting

2. **C6 Firmware Handshake**
   - Monitor `GPIO6` (C6_IO2 - data ready signal)
   - Wait for C6 firmware to complete boot
   - Timeout: 5 seconds (configurable)

3. **SDIO Transport Initialization**
   - Call `wifi_hosted_init_transport()`
   - Initializes **SDMMC Slot 1** for C6 communication
   - Verifies "Transport active" log message

4. **Event Signaling**
   - Set `HOSTED_READY` bit
   - SDMMC controller is now **configured and active**
   - Safe for SD card to reuse host

### Code Example

```c
// Phase B: WiFi Manager Task
static void bootstrap_wifi_manager_task(void *arg) {
    bootstrap_manager_t *manager = arg;
    
    // Step 1: Wait for power
    xEventGroupWaitBits(manager->event_group, POWER_READY_BIT, 
                        pdFALSE, pdFALSE, portMAX_DELAY);
    
    // Step 2: C6 handshake
    gpio_set_direction(GPIO_C6_IO2_HANDSHAKE, GPIO_MODE_INPUT);
    // TODO: Monitor GPIO6 for ready signal
    
    // Step 3: SDIO transport
    esp_err_t ret = wifi_hosted_init_transport();
    if (ret != ESP_OK) {
        xEventGroupSetBits(manager->event_group, FAILURE_BIT);
        vTaskDelete(NULL);
        return;
    }
    
    // Step 4: Signal ready
    xEventGroupSetBits(manager->event_group, HOSTED_READY_BIT);
    vTaskDelete(NULL);
}
```

---

## Phase C: SD Manager

### Responsibilities

1. **Wait for Hosted Ready**
   - Block on `HOSTED_READY` event bit
   - SDMMC controller is **already initialized** by WiFi
   - Bus is safe for SD operations

2. **Enable SDMMC Pull-Ups**
   - Activate internal pull-ups on GPIO39-44
   - Prevents floating signals on SD data lines
   - Reduces `0x107` timeout errors

3. **Safe Filesystem Mount**
   - Call `sd_card_mount_safe()` (uses dummy host init)
   - Reuses SDMMC host configured by WiFi
   - Mounts FAT filesystem via VFS

4. **Event Signaling**
   - Set `SD_READY` bit
   - System fully initialized

### Code Example

```c
// Phase C: SD Manager Task
static void bootstrap_sd_manager_task(void *arg) {
    bootstrap_manager_t *manager = arg;
    
    // Step 1: Wait for WiFi
    xEventGroupWaitBits(manager->event_group, HOSTED_READY_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    
    // Step 2: Enable pull-ups
    for (int gpio = 39; gpio <= 44; gpio++) {
        gpio_pullup_en(gpio);
    }
    
    // Step 3: Mount filesystem
    esp_err_t ret = sd_card_mount_safe(&manager->sd_card);
    if (ret != ESP_OK) {
        xEventGroupSetBits(manager->event_group, FAILURE_BIT);
        vTaskDelete(NULL);
        return;
    }
    
    // Step 4: Signal ready
    xEventGroupSetBits(manager->event_group, SD_READY_BIT);
    vTaskDelete(NULL);
}
```

---

## Warm Boot Detection and Recovery

### Problem: Dirty Hardware State

On software reset (IDF monitor restart, `esp_restart()`), hardware peripherals retain their previous state:
- SDMMC controller registers contain stale data
- SD card capacitors are still charged
- C6 may be in unknown state

### Solution: Hard Reset Cycle

Before starting Phase A, detect warm boot and force power cycle:

```c
bool bootstrap_is_warm_boot(void) {
    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            return false;  // Cold boot
            
        case ESP_RST_SW:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_DEEPSLEEP:
        case ESP_RST_BROWNOUT:
            return true;   // Warm boot
            
        default:
            return true;   // Assume warm boot (safe)
    }
}

void bootstrap_hard_reset(void) {
    // Force complete power-down
    gpio_set_level(GPIO_C6_CHIP_PU, 0);    // C6 in reset
    gpio_set_level(GPIO_SD_POWER_EN, 0);   // SD unpowered
    
    // Wait 500ms for capacitor discharge
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Ready for clean init
}
```

### Integration in Main

```c
void app_main(void) {
    bootstrap_manager_t manager = {0};
    
    // Detect and handle warm boot
    if (bootstrap_is_warm_boot()) {
        ESP_LOGW(TAG, "Warm boot detected, performing hard reset");
        bootstrap_hard_reset();
    }
    
    // Start coordinated bootstrap
    esp_err_t ret = bootstrap_manager_init(&manager);
    assert(ret == ESP_OK);
    
    // Wait for completion (30s timeout)
    ret = bootstrap_manager_wait(&manager, 30000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "System ready!");
        // Continue with app logic
    } else {
        ESP_LOGE(TAG, "Bootstrap failed");
        esp_restart();  // Retry
    }
}
```

---

## Expected Boot Log

### Successful Three-Phase Boot

```
I (150) BOOTSTRAP: ========================================
I (151) BOOTSTRAP:   Bootstrap Manager v1.1.0-dev
I (152) BOOTSTRAP:   Deterministic Three-Phase Init
I (153) BOOTSTRAP: ========================================
I (154) BOOTSTRAP: Cold boot detected (power-on reset)
I (155) BOOTSTRAP: Three-phase bootstrap tasks spawned
I (156) BOOTSTRAP:   Phase A: Power Manager (priority 24)
I (157) BOOTSTRAP:   Phase B: WiFi Manager (priority 23)
I (158) BOOTSTRAP:   Phase C: SD Manager (priority 22)

I (159) BOOTSTRAP: [Phase A] Power Manager starting...
I (160) BOOTSTRAP: [Phase A] Forcing GPIO isolation...
I (161) BOOTSTRAP: [Phase A]   GPIO54 (C6_CHIP_PU) → LOW (C6 in reset)
I (162) BOOTSTRAP: [Phase A]   GPIO36 (SD_POWER_EN) → LOW (SD unpowered)
I (163) BOOTSTRAP: [Phase A] Waiting 100ms for rail stabilization...
I (264) BOOTSTRAP: [Phase A] Power-on sequence starting...
I (265) BOOTSTRAP: [Phase A]   GPIO36 (SD_POWER_EN) → HIGH (SD powered)
I (316) BOOTSTRAP: [Phase A]   GPIO54 (C6_CHIP_PU) → HIGH (C6 released from reset)
I (317) BOOTSTRAP: [Phase A] Power-on complete, signaling POWER_READY

I (318) BOOTSTRAP: [Phase B] WiFi Manager waiting for POWER_READY...
I (319) BOOTSTRAP: [Phase B] POWER_READY received, starting WiFi Hosted init...
I (320) BOOTSTRAP: [Phase B] Waiting for C6 firmware ready signal (GPIO6)...
I (5321) BOOTSTRAP: [Phase B] C6 handshake timeout (proceeding anyway)
I (5322) BOOTSTRAP: [Phase B] Initializing ESP-Hosted SDIO transport...
I (5600) H_API: Transport active
I (5601) BOOTSTRAP: [Phase B] WiFi Hosted transport active
I (5602) BOOTSTRAP: [Phase B] Signaling HOSTED_READY

I (5603) BOOTSTRAP: [Phase C] SD Manager waiting for HOSTED_READY...
I (5604) BOOTSTRAP: [Phase C] HOSTED_READY received, SDMMC bus is safe
I (5605) BOOTSTRAP: [Phase C] Enabling pull-ups on SDMMC pins (GPIO39-44)...
I (5606) BOOTSTRAP: [Phase C] Mounting SD card filesystem...
I (5850) BOOTSTRAP: [Phase C] SD card mounted successfully
I (5851) BOOTSTRAP: [Phase C] Signaling SD_READY

I (5852) BOOTSTRAP: ========================================
I (5853) BOOTSTRAP:   Bootstrap COMPLETE (5703 ms)
I (5854) BOOTSTRAP: ========================================
```

### Warm Boot with Hard Reset

```
I (150) BOOTSTRAP: Warm boot detected (reset reason: 3)
I (151) BOOTSTRAP: === HARD RESET CYCLE ===
I (152) BOOTSTRAP: Forcing complete power-down to clear hardware state...
I (153) BOOTSTRAP:   GPIO54 (C6_CHIP_PU) → LOW
I (154) BOOTSTRAP:   GPIO36 (SD_POWER_EN) → LOW
I (155) BOOTSTRAP:   Waiting 500ms for capacitor discharge...
I (656) BOOTSTRAP: Hard reset complete, ready for clean init
I (657) BOOTSTRAP: ========================================
I (658) BOOTSTRAP:   Bootstrap Manager v1.1.0-dev
...
```

---

## GPIO Mapping Summary

| Function | P4 GPIO | C6 GPIO | Direction | Purpose |
|----------|---------|---------|-----------|----------|
| **C6 Reset** | 54 | CHIP_PU | P4 → C6 | Hold C6 in reset during isolation |
| **SD Power** | 36 | - | P4 → MOSFET | Enable/disable SD card power |
| **Handshake** | 6 | IO2 | C6 → P4 | C6 firmware ready signal |
| **Strapping** | TBD | IO9 | P4 → C6 | Force SPI boot mode (override R15) |
| **SDMMC Slot 0** | 39-44 | - | P4 ↔ SD | SD card data/cmd/clk |
| **SDMMC Slot 1** | 14-19 | - | P4 ↔ C6 | WiFi SDIO transport |

**TODO:** Identify P4 GPIO connected to C6_IO9 (check schematic/BSP).

---

## Benefits

### Reliability
- ✅ **100% deterministic** boot sequence
- ✅ **No bus conflicts** between SD and WiFi
- ✅ **No 0x107 errors** from floating pins
- ✅ **Warm boot recovery** automatic

### Maintainability
- ✅ **Clear phase separation** (power, WiFi, SD)
- ✅ **Event-driven coordination** (no polling loops)
- ✅ **FreeRTOS best practices** (high-priority tasks)
- ✅ **Detailed logging** for debugging

### Scalability
- ✅ **Easy to add phases** (e.g., Phase D for BLE)
- ✅ **Timeout-based recovery** (automatic retry)
- ✅ **Modular architecture** (swap implementations)

---

## Migration from v1.0.0-beta

### Old Approach (Sequential Init)

```c
void app_main(void) {
    nvs_flash_init();
    i2c_bus_init();
    display_init();
    touch_init();
    sd_card_init();      // ❌ May conflict with WiFi
    wifi_hosted_init();  // ❌ May conflict with SD
}
```

### New Approach (Bootstrap Manager)

```c
void app_main(void) {
    nvs_flash_init();
    i2c_bus_init();
    display_init();
    touch_init();
    
    // ✅ Coordinated SD + WiFi initialization
    bootstrap_manager_t manager = {0};
    bootstrap_manager_init(&manager);
    bootstrap_manager_wait(&manager, 30000);
    
    // SD and WiFi are ready
    sdmmc_card_t *card = bootstrap_manager_get_sd_card(&manager);
}
```

---

## Future Enhancements

### v1.1.1
- [ ] Identify and map C6_IO9 strapping GPIO
- [ ] Implement proper C6 handshake protocol (GPIO6 monitoring)
- [ ] Add watchdog for phase timeouts

### v1.2.0
- [ ] Phase D: BLE initialization (after WiFi ready)
- [ ] Dynamic priority adjustment based on workload
- [ ] Power consumption profiling

### v2.0.0
- [ ] Suspend/resume support (sleep modes)
- [ ] Hot-plug SD card detection
- [ ] OTA update integration

---

## References

- **ESP-IDF SDMMC Driver:** https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/sdmmc_host.html
- **ESP-Hosted Documentation:** https://github.com/espressif/esp-hosted
- **FreeRTOS Event Groups:** https://www.freertos.org/event-groups-API.html
- **Guition Board Schematic:** `JC1060P470C_schematic.pdf` (confidential)
- **ESP32-C6 Strapping Pins:** ESP32-C6 Technical Reference Manual, Section 2.4

---

**Document Version:** 1.0  
**Last Updated:** 2026-03-01  
**Author:** Cristiano Gorla
