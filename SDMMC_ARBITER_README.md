# SDMMC Controller Management: WiFi/SD Card Slot Switching

## Problem Statement

The ESP32-P4 has a **hardware constraint**: the SDMMC controller can only operate on one slot at a time.

**Hardware Configuration:**
- **SDMMC Slot 0** (GPIO39-44): SD Card
- **SDMMC Slot 1** (GPIO14-19): ESP-Hosted WiFi (ESP32-C6 via SDIO)
- **SDMMC Controller**: Single hardware controller shared between both slots

**The Challenge:**

The SDMMC controller is initialized during WiFi setup (Phase C) for Slot 1 (ESP-Hosted SDIO). When mounting the SD card on Slot 0, the controller must be:
1. Deinitialized from Slot 1
2. Reinitialized for Slot 0
3. Configured for SD card host mode

**Without proper reinitialization:**
```
Phase C: WiFi init → SDMMC controller active on Slot 1
Phase B: SD mount  → TIMEOUT (0x107) because controller still bound to Slot 1
```

## Solution: SDMMC Controller Management

The bootstrap manager and arbiter implement **sequential initialization** with proper controller reinitialization:

```
┌─────────────────────────────────────────────────┐
│         SDMMC Controller (Single HW Unit)       │
├─────────────────────┬───────────────────────────┤
│  Slot 1 (WiFi)      │   Slot 0 (SD Card)        │
│  GPIO14-19          │   GPIO39-44               │
├─────────────────────┼───────────────────────────┤
│ ESP-Hosted SDIO     │ SD card host mode         │
│ WiFi operations     │ File I/O operations       │
│ Network traffic     │ Read/write files          │
└─────────────────────┴───────────────────────────┘
         ↑                       ↑
    Controller must be reinitialized to switch slots
```

### Key Features

1. **Sequential Initialization**: Slot 1 (WiFi) first, then Slot 0 (SD)
2. **Controller Reinitialization**: Proper deinit/init sequence when switching slots
3. **Mutual Exclusion**: FreeRTOS mutex for runtime slot switching (via arbiter)
4. **NVS Persistence**: Saves preferred mode across reboots (arbiter feature)
5. **Thread-Safe**: Multiple tasks can request mode changes (arbiter feature)

## Architecture

### Bootstrap Sequence (Boot Time)

**Current 3-Phase Boot:**
```
Phase A: Power Management (BSP)
  ├── SD card power control (GPIO36)
  └── Signal: POWER_READY
  ↓
Phase C: WiFi Initialization
  ├── sdmmc_host_init() for Slot 1
  ├── ESP-Hosted SDIO transport init
  ├── Wait 2s for SDIO link stabilization
  └── Signal: WIFI_READY
  ↓
Phase B: SD Card Mount
  ├── sdmmc_host_deinit() (release Slot 1)
  ├── Wait 200ms for bus settling
  ├── sdmmc_host_init() for Slot 0
  ├── sdmmc_host_init_slot(SLOT_0, ...)
  ├── Mount FAT filesystem
  └── Signal: SD_READY
  ↓
Bootstrap Complete → Both WiFi and SD Ready
```

**CRITICAL: Phase B reinitializes the SDMMC controller from Slot 1 to Slot 0!**

### Component Files

- **`bootstrap_manager.c`**: Orchestrates 3-phase sequential init
- **`sd_card_manager.c`**: **FIXED** - Now properly reinitializes controller for Slot 0
- **`esp_hosted_wifi.c`**: WiFi transport init on Slot 1
- **`sdmmc_arbiter.h/c`**: Runtime slot switching API (for advanced use cases)

## Boot-Time Initialization (Automatic)

The bootstrap manager handles the complete initialization sequence automatically:

```c
void app_main(void)
{
    // Bootstrap initializes WiFi (Slot 1) then SD (Slot 0) sequentially
    bootstrap_manager_t bootstrap;
    esp_err_t ret = bootstrap_manager_init(&bootstrap);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap failed");
        return;
    }
    
    // Both WiFi and SD card are now ready
    // WiFi can be used for network operations
    // SD card is mounted at /sdcard
    
    // Example: Use WiFi
    wifi_connect("MyNetwork", "password123");
    wait_for_ip();
    
    // Example: Use SD card (no switching needed, already mounted)
    FILE *f = fopen("/sdcard/test.txt", "w");
    fprintf(f, "Hello World\n");
    fclose(f);
}
```

## Runtime Slot Switching API (Advanced)

For applications that need to dynamically switch between WiFi and SD card after boot, use the arbiter API:

### 1. Request WiFi Mode (Switch to Slot 1)

```c
// Request WiFi mode (deinitializes SD if active, reinits controller for Slot 1)
esp_err_t ret = sdmmc_arbiter_request_wifi(5000);  // 5s timeout
if (ret == ESP_OK) {
    // WiFi mode granted
    // SDMMC controller reinitialized for Slot 1 (SDIO)
    // SD card unmounted
    init_wifi();
    wifi_connect("SSID", "password");
}
```

### 2. Request SD Card Mode (Switch to Slot 0)

```c
// Request SD card mode (deinitializes WiFi if active, reinits controller for Slot 0)
sdmmc_card_t *card = NULL;
esp_err_t ret = sdmmc_arbiter_request_sd_card(5000, &card);
if (ret == ESP_OK) {
    // SD mode granted
    // SDMMC controller reinitialized for Slot 0 (SD host)
    // WiFi deinitialized
    FILE *f = fopen("/sdcard/test.txt", "r");
    // ... file operations ...
    fclose(f);
}
```

### 3. Release Mode (Optional)

```c
// Release WiFi mode (allows SD card requests)
sdmmc_arbiter_release_wifi();

// Release SD card mode (allows WiFi requests)
sdmmc_arbiter_release_sd_card();
```

### 4. Check Current Mode

```c
sdmmc_bus_mode_t mode = sdmmc_arbiter_get_mode();
if (mode == SDMMC_MODE_WIFI) {
    ESP_LOGI(TAG, "WiFi mode active (Slot 1)");
} else if (mode == SDMMC_MODE_SD_CARD) {
    ESP_LOGI(TAG, "SD card mode active (Slot 0)");
}
```

## Example Workflow: Dynamic Switching

### Scenario: Start with SD card, switch to WiFi, then back to SD

```c
void app_main(void)
{
    // Bootstrap initializes both WiFi and SD
    bootstrap_manager_t bootstrap;
    bootstrap_manager_init(&bootstrap);
    
    // Initially, SD card is mounted and WiFi is ready
    // Read config from SD card
    FILE *f = fopen("/sdcard/config.txt", "r");
    char ssid[32], pass[64];
    fscanf(f, "%s %s", ssid, pass);
    fclose(f);
    
    // Connect to WiFi (WiFi already initialized, just connect)
    wifi_connect(ssid, pass);
    wait_for_ip();
    
    // Download file from server
    uint8_t *data = http_download("http://example.com/data.bin");
    
    // Save to SD card (SD already mounted, no switching needed)
    f = fopen("/sdcard/data.bin", "wb");
    fwrite(data, 1, data_len, f);
    fclose(f);
    
    free(data);
    
    // If you need to switch modes dynamically (advanced use case):
    // 1. Release current mode
    // 2. Request new mode via arbiter API
}
```

## NVS Persistence (Arbiter Feature)

The arbiter can save the current mode to NVS for next boot:

```c
// Save current mode for next boot
sdmmc_arbiter_save_mode();

// Load saved mode (called automatically by arbiter_init)
sdmmc_bus_mode_t saved_mode;
sdmmc_arbiter_load_mode(&saved_mode);
```

**NVS Storage:**
- Namespace: `"sdmmc_arbiter"`
- Key: `"mode"`
- Value: `1` (WiFi), `2` (SD card)

## Hardware Pin Mapping

### SDMMC Slot 0 (SD Card)

| ESP32-P4 Pin | SD Card Pin | Notes |
|--------------|-------------|-------|
| GPIO43 | CLK | 10k pullup |
| GPIO44 | CMD | 10k pullup |
| GPIO39 | D0 | 10k pullup |
| GPIO40 | D1 | 10k pullup |
| GPIO41 | D2 | 10k pullup |
| GPIO42 | D3 | 10k pullup required |
| GPIO36 | PWR_EN | SD card power control (BSP) |

### SDMMC Slot 1 (ESP-Hosted WiFi)

| ESP32-P4 Pin | Function | Notes |
|--------------|----------|-------|
| GPIO14-19 | SDIO Data/CMD/CLK | ESP-Hosted SDIO transport |
| GPIO54 | C6_RESET | ESP-Hosted driver manages reset |
| GPIO6 | INT/READY | ESP-Hosted interrupt/ready signal |

## Performance Considerations

**Controller Reinitialization Time (Boot):**
- Phase C (WiFi): ~5.4s (SDMMC init + SDIO link stabilization)
- Phase B (SD): ~360ms (Controller deinit/reinit + SD mount)
- **Total Bootstrap**: ~6.4s

**Runtime Mode Switching Time (Arbiter API):**
- WiFi → SD: ~5.1s (WiFi deinit + controller reinit + SD mount)
- SD → WiFi: ~5.1s (SD unmount + controller reinit + WiFi init + SDIO stabilization)

**Recommendation:**
- At boot, both peripherals are available (no switching needed)
- Avoid runtime switching if possible (use both concurrently after boot)
- If switching is required, batch operations to minimize overhead
- Use NVS persistence to boot into preferred mode

## Debugging

Enable relevant logs:

```c
esp_log_level_set("BOOTSTRAP", ESP_LOG_DEBUG);
esp_log_level_set("SD_MANAGER", ESP_LOG_DEBUG);
esp_log_level_set("wifi_hosted", ESP_LOG_DEBUG);
esp_log_level_set("SDMMC_ARBITER", ESP_LOG_DEBUG);
```

**Common Issues:**

1. **SD mount fails with 0x107**: Controller not reinitialized for Slot 0
   - Solution: Verify `sd_card_manager.c` calls `sdmmc_host_deinit()` + `sdmmc_host_init()`

2. **WiFi timeout after SD mount**: Controller not reinitialized for Slot 1
   - Solution: Use arbiter API to properly switch back to WiFi mode

3. **"Already in X mode"**: Redundant request (harmless)
   - Solution: Check current mode before requesting

4. **Timeout on arbiter request**: Another task holds the mutex
   - Solution: Increase timeout or release mode in other task

## Recent Fixes

### v1.3.0-dev (2026-03-03)

**Fix: SDMMC Controller Reinitialization for SD Slot 0**

Commit: [b9b77b6](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/commit/b9b77b6db56747074c3489b1d494307371475171)

**Problem**: SD card mount was failing with timeout `0x107` because the SDMMC controller was initialized for WiFi (Slot 1) but SD mount attempted to use Slot 0 without reinitializing the controller.

**Solution**: Modified `sd_card_manager.c` to:
1. Call `sdmmc_host_deinit()` to release Slot 1
2. Wait 200ms for bus settling
3. Call `sdmmc_host_init()` to reinitialize for Slot 0
4. Then proceed with `sdmmc_host_init_slot(SLOT_0, ...)`

**Result**: SD card now mounts successfully after WiFi initialization.

## License

Unlicense (Public Domain)

Copyright (c) 2026 Cristiano Gorla
