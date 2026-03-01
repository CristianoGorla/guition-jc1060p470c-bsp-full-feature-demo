# Bootstrap Sequence Documentation

## Overview

This document describes the proven initialization sequence from **v1.0.0-beta** that enables both ESP-Hosted WiFi and SD card to coexist on the same SDMMC peripheral.

## Critical Insight

**ESP-Hosted Transport ≠ WiFi Operations**

- **ESP-Hosted Transport**: Initializes SDIO/SDMMC controller for host-slave communication
- **WiFi Operations**: Use the already-initialized transport for scan/connect/data

The sequence is: **Power → ESP-Hosted Transport → SD Card → WiFi Operations**

## Three-Phase Bootstrap

### Phase A: Power Management

**Purpose**: Initialize power rails and release C6 from reset

```c
// Step 1: GPIO isolation
GPIO54 (C6_CHIP_PU) → LOW    // C6 in reset
GPIO36 (SD_POWER_EN) → LOW   // SD unpowered

// Step 2: Stabilization (100ms)
vTaskDelay(100);

// Step 3: Power-on sequence
GPIO36 (SD_POWER_EN) → HIGH  // SD powered
vTaskDelay(50);
GPIO54 (C6_CHIP_PU) → HIGH   // C6 released, boots immediately
vTaskDelay(50);
```

**Key Points**:
- C6 is released and boots **immediately** (no delay)
- C6 firmware initializes SDMMC controller for ESP-Hosted transport
- SD card is powered but not yet mounted

**Output**:
```
I (2658) BOOTSTRAP: [Phase A] ✓ POWER_READY
```

### Phase C: ESP-Hosted Transport Init

**Purpose**: Initialize ESP-Hosted SDIO transport layer

```c
init_wifi();  // Initializes SDMMC controller + ESP-Hosted transport
vTaskDelay(2000);  // Wait for C6 firmware boot + SDMMC init
```

**What Happens**:
1. **Host side** (`init_wifi()`):
   - Creates WiFi netif
   - Registers event handlers
   - Initializes SDMMC Slot 1 for ESP-Hosted
   - **SDMMC controller is now initialized**

2. **Slave side** (C6 firmware):
   - Boots from SPI flash
   - Initializes SDIO peripheral
   - Sends `INIT` event to host
   - Establishes transport layer

**Output**:
```
I (2662) wifi_hosted: === WiFi Hosted Transport Init (Phase B) ===
I (2677) wifi_hosted: Initializing netif...
I (4234) sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit Freq(KHz)[40000 KHz]
I (4437) transport: Received INIT event from ESP32 peripheral
I (4443) transport: Identified slave [esp32c6]
I (5364) wifi_hosted: ✓ WiFi stack initialized
I (7364) BOOTSTRAP: [Phase C] ✓ WIFI_READY (SDMMC controller initialized)
```

**Critical**: SDMMC controller is **now initialized** by ESP-Hosted transport.

### Phase B: SD Card Mount

**Purpose**: Mount SD card using already-initialized SDMMC controller

```c
// Step 1: Initialize Slot 0 ONLY (host already initialized)
sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);

// Step 2: Create host config with dummy init/deinit
sdmmc_host_t host = SDMMC_HOST_DEFAULT();
host.slot = SDMMC_HOST_SLOT_0;
host.init = &sdmmc_host_init_dummy;    // Skip host init!
host.deinit = &sdmmc_host_deinit_dummy; // Skip host deinit!

// Step 3: Mount filesystem
esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
```

**Dummy Functions**:
```c
static esp_err_t sdmmc_host_init_dummy(void)
{
    LOG_SD(TAG, "Skipping sdmmc_host_init (controller already initialized by WiFi)");
    return ESP_OK;
}

static esp_err_t sdmmc_host_deinit_dummy(void)
{
    LOG_SD(TAG, "Skipping sdmmc_host_deinit (keep controller active for WiFi)");
    return ESP_OK;
}
```

**Why Dummy Functions?**
- SDMMC controller is **already initialized** by ESP-Hosted in Phase C
- Calling `sdmmc_host_init()` again would **fail** or cause conflicts
- Dummy functions skip re-initialization while keeping the mount API happy

**Output**:
```
I (7371) SD_MANAGER: === SD Card Mount (Phase B - after WiFi) ===
I (7627) SD_MANAGER: Initializing SDMMC Slot 0 (host already active)...
I (7639) SD_MANAGER: Skipping sdmmc_host_init (controller already initialized by WiFi)
I (7813) SD_MANAGER: ✓ SD card mounted successfully
I (7814) BOOTSTRAP: [Phase B] ✓ SD_READY
```

### WiFi Operations (Post-Bootstrap)

**Purpose**: Use ESP-Hosted transport for WiFi scan/connect

After bootstrap completes, WiFi operations can be performed:

```c
// Scan
do_wifi_scan_and_check(NULL);

// Connect
wifi_connect(WIFI_SSID, WIFI_PASSWORD);
wait_for_ip();
```

**Output**:
```
I (7874) GUITION_MAIN: Connecting to: FRITZ!Box 7530 WL
I (8277) RPC_WRAP: ESP Event: Station mode: Connected
I (9294) GUITION_MAIN: ✓ WiFi connected!
I (9294) GUITION_MAIN:    IP: 192.168.188.88
```

## Complete Boot Log (Success)

```
I (1854) BOOTSTRAP: ========================================
I (1860) BOOTSTRAP:   Bootstrap Manager v1.1.0-restored
I (1865) BOOTSTRAP:   v1.0.0-beta Sequence: Power → WiFi → SD
I (1871) BOOTSTRAP: ========================================

[Phase A: Power]
I (2431) BOOTSTRAP: [Phase A] Power Manager starting...
I (2558) BOOTSTRAP: [Phase A]   GPIO36 (SD_POWER_EN) → HIGH (SD powered)
I (2608) BOOTSTRAP: [Phase A]   GPIO54 (C6_CHIP_PU) → HIGH (C6 released)
I (2658) BOOTSTRAP: [Phase A] ✓ POWER_READY

[Phase C: ESP-Hosted Transport]
I (2658) BOOTSTRAP: [Phase C] Starting WiFi transport...
I (2662) wifi_hosted: === WiFi Hosted Transport Init (Phase B) ===
I (4234) sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit Freq(KHz)[40000 KHz]
I (4437) transport: Received INIT event from ESP32 peripheral
I (7364) BOOTSTRAP: [Phase C] ✓ WIFI_READY (SDMMC controller initialized)

[Phase B: SD Mount]
I (7364) BOOTSTRAP: [Phase B] Starting SD card mount...
I (7639) SD_MANAGER: Skipping sdmmc_host_init (controller already initialized by WiFi)
I (7813) SD_MANAGER: ✓ SD card mounted successfully
I (7814) BOOTSTRAP: [Phase B] ✓ SD_READY

I (7818) BOOTSTRAP: ========================================
I (7823) BOOTSTRAP:   Bootstrap COMPLETE (5941 ms)
I (7828) BOOTSTRAP:   Phase A: Power ✓
I (7831) BOOTSTRAP:   Phase C: WiFi ✓ (SDMMC initialized)
I (7837) BOOTSTRAP:   Phase B: SD card ✓ (dummy init)
I (7842) BOOTSTRAP: ========================================

[WiFi Connect]
I (7874) GUITION_MAIN: Connecting to: FRITZ!Box 7530 WL
I (8277) RPC_WRAP: ESP Event: Station mode: Connected
I (9294) GUITION_MAIN: ✓ WiFi connected!
I (9294) GUITION_MAIN:    IP: 192.168.188.88
```

## SDMMC Resource Sharing

### Hardware Configuration

**Slot 0** (SD Card):
- CLK: GPIO43
- CMD: GPIO44
- D0: GPIO39
- D1: GPIO40
- D2: GPIO41
- D3: GPIO42

**Slot 1** (ESP-Hosted):
- CLK: GPIO18
- CMD: GPIO19
- D0: GPIO14
- D1: GPIO15
- D2: GPIO16
- D3: GPIO17

**Control Signals**:
- GPIO54: C6_CHIP_PU (reset)
- GPIO36: SD_POWER_EN (power)
- GPIO6: C6_IO2 (handshake/interrupt)

### Controller State Machine

```
┌─────────────────────────────────────────────┐
│         SDMMC Controller (Shared)           │
│                                             │
│  State: UNINITIALIZED                      │
└─────────────────────────────────────────────┘
                    ↓
            [Phase C: init_wifi()]
                    ↓
┌─────────────────────────────────────────────┐
│         SDMMC Controller (Active)           │
│                                             │
│  Initialized by: ESP-Hosted Transport       │
│  Slot 1: ESP-Hosted (active)               │
│  Slot 0: Available                         │
└─────────────────────────────────────────────┘
                    ↓
        [Phase B: sd_card_mount_safe()]
                    ↓
┌─────────────────────────────────────────────┐
│         SDMMC Controller (Active)           │
│                                             │
│  Slot 1: ESP-Hosted (active)               │
│  Slot 0: SD Card (active)                  │
│                                             │
│  Both slots share same controller!         │
└─────────────────────────────────────────────┘
```

## Key Takeaways

1. **Sequence matters**: ESP-Hosted **must** initialize SDMMC controller before SD mount

2. **Dummy functions are essential**: They prevent re-initialization of the shared controller

3. **ESP-Hosted ≠ WiFi**: ESP-Hosted is the transport layer; WiFi operations come after

4. **C6 boots immediately**: No delay after reset release; C6 firmware starts right away

5. **Total boot time**: ~6 seconds from power-on to WiFi connected

## Troubleshooting

### Error: `sdmmc_init_ocr: send_op_cond (1) returned 0x107`

**Cause**: SD mount attempted before SDMMC controller initialization

**Solution**: Ensure Phase C (WiFi transport) runs **before** Phase B (SD mount)

### Error: `sdmmc_host_init: SDMMC host already initialized`

**Cause**: Real `sdmmc_host_init()` called instead of dummy

**Solution**: Use dummy init/deinit functions in `sd_card_manager.c`

### WiFi Not Working After SD Mount

**Cause**: SD mount called `sdmmc_host_deinit()`, shutting down ESP-Hosted transport

**Solution**: Use `sdmmc_host_deinit_dummy()` to prevent controller shutdown

## References

- Working implementation: [v1.0.0-beta](https://github.com/CristianoGorla/host_sdcard_with_hosted/tree/v1.0.0-beta)
- Bootstrap manager: `main/bootstrap_manager.c`
- SD manager: `main/sd_card_manager.c`
- ESP-Hosted transport: `main/esp_hosted_wifi.c`
