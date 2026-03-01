# Project Status - ESP32-P4 Guition JC1060 with ESP-Hosted

**Last Updated**: 2026-03-02 00:42 CET  
**Version**: v1.0.0-beta-60  
**Status**: ✅ **WORKING** (with minor first-boot issue)

## 🎯 Current State

### What's Working ✅

1. **Hardware Initialization**
   - ✅ I2C Bus (GPIO7/GPIO8)
   - ✅ ES8311 Audio Codec (powered down, safe state)
   - ✅ RTC RX8025T (time valid)
   - ✅ Display JD9165 (1024x600 MIPI DSI)
   - ✅ Touch GT911 (1024x600)

2. **Bootstrap Sequence (v1.0.0-beta Restored)**
   - ✅ Phase A: Power Management (GPIO54, GPIO36)
   - ✅ Phase C: ESP-Hosted Transport (SDMMC Slot 1)
   - ✅ Phase B: SD Card Mount (SDMMC Slot 0, dummy init)
   - ✅ Total boot time: ~6 seconds

3. **Storage & Network**
   - ✅ SD Card: 7580 MB mounted on `/sdcard`
   - ✅ WiFi: Connects to AP (after first boot)
   - ✅ IP: 192.168.188.88 via DHCP

### Known Issues ⚠️

1. **WiFi Connection Fails on First Boot After Flash**
   - **Symptom**: First boot after `idf.py flash` times out WiFi connection
   - **Workaround**: Manual reset via terminal makes WiFi work
   - **Root Cause**: Missing C6 firmware ready handshake
   - **Status**: Documented in `docs/WIFI_FIRST_BOOT_ISSUE.md`
   - **Priority**: Medium (workaround exists, stable after first boot)

## 📚 Documentation

### Available Documents

1. **[BOOTSTRAP_SEQUENCE.md](./BOOTSTRAP_SEQUENCE.md)**
   - Complete bootstrap sequence (Power → ESP-Hosted → SD)
   - SDMMC controller sharing explanation
   - Dummy init/deinit functions
   - Full boot log with annotations

2. **[WIFI_FIRST_BOOT_ISSUE.md](./WIFI_FIRST_BOOT_ISSUE.md)**
   - First boot WiFi connection failure analysis
   - 3-phase action plan
   - SDIO timing and handshake protocol
   - Test matrix and configuration options

### Configuration Files

- **`sdkconfig.defaults`**: Organized with sections and comments
- **`partitions.csv`**: NVS + phy_init + factory + storage
- **Pin definitions**: Documented in `bootstrap_manager.h`

## 🔑 Key Technical Details

### SDMMC Resource Sharing

```
SDMMC Controller (Shared)
├─ Slot 0: SD Card (GPIO39-44)
│  ├─ CLK: GPIO43
│  ├─ CMD: GPIO44
│  ├─ D0-D3: GPIO39-42
│  └─ Power: GPIO36 (external)
│
└─ Slot 1: ESP-Hosted (GPIO14-19)
   ├─ CLK: GPIO18
   ├─ CMD: GPIO19
   ├─ D0-D3: GPIO14-17
   └─ Control:
      ├─ GPIO54: C6_CHIP_PU (reset)
      └─ GPIO6: C6_IO2 (data ready interrupt)
```

### Bootstrap Sequence

```
Phase A: Power ON
  ├─ GPIO isolation (C6 + SD powered down)
  ├─ 100ms stabilization
  ├─ SD power ON (GPIO36)
  └─ C6 release (GPIO54) → boots immediately
       ↓
Phase C: ESP-Hosted Transport Init
  ├─ init_wifi() → initializes SDMMC controller
  ├─ C6 firmware boots (~2s)
  └─ SDIO transport established (Slot 1)
       ↓
Phase B: SD Mount
  ├─ sdmmc_host_init_dummy() → skip re-init
  ├─ sdmmc_host_init_slot(Slot 0)
  └─ FAT filesystem mounted
       ↓
WiFi Operations
  └─ scan/connect using ESP-Hosted transport
```

### Critical Functions

**Dummy Init/Deinit** (prevents SDMMC re-initialization):
```c
static esp_err_t sdmmc_host_init_dummy(void) {
    // Controller already initialized by ESP-Hosted
    return ESP_OK;
}

static esp_err_t sdmmc_host_deinit_dummy(void) {
    // Keep controller active for ESP-Hosted
    return ESP_OK;
}
```

## 🚀 Next Steps

### Immediate Priority: Fix First Boot WiFi Issue

**Phase 1: Implement GPIO6 Handshake** (Primary Solution)

1. Add GPIO6 polling in `bootstrap_wifi_sequence()`
2. Wait for C6 firmware ready signal
3. Test first boot after flash

**Expected Files to Modify**:
- `main/bootstrap_manager.c`
- `main/bootstrap_manager.h` (if GPIO6 not defined)

**Success Criteria**:
- WiFi connects on first boot without manual reset
- Boot time remains < 7 seconds

### Future Enhancements

1. **BLE Support**: Enable BLE over ESP-Hosted transport
2. **I2S Audio**: Configure ES8311 for audio playback
3. **LVGL UI**: Display framework integration
4. **OTA Updates**: Firmware update over WiFi
5. **Power Management**: Sleep modes optimization

## 🛠️ Development Environment

- **IDF Version**: v5.5.3
- **Target**: ESP32-P4 (rev v1.3)
- **Board**: Guition JC1060P470C
- **Flash**: Boya 16MB (QIO mode)
- **PSRAM**: 32MB Octal @ 200MHz
- **Build System**: CMake
- **Monitor**: idf.py monitor @ 115200 baud

## 📊 Performance Metrics

| Metric | Value |
|--------|-------|
| Boot time (cold) | 5.9s |
| Boot time (warm) | 6.5s (includes hard reset) |
| WiFi connect | 1.5s (after first boot) |
| SD mount | 450ms |
| PSRAM | 32000 KB available |
| Heap (internal) | 428 KB available |

## 🔗 Repository Structure

```
host_sdcard_with_hosted/
├─ main/
│  ├─ bootstrap_manager.c/h      # Three-phase bootstrap
│  ├─ sd_card_manager.c/h        # SD mount with dummy init
│  ├─ sdmmc_arbiter.c/h          # Runtime mode switching API
│  ├─ esp_hosted_wifi.c/h        # WiFi wrapper for ESP-Hosted
│  ├─ guition_main.c             # Main application
│  └─ ... (peripheral drivers)
├─ docs/
│  ├─ BOOTSTRAP_SEQUENCE.md      # Complete sequence docs
│  ├─ WIFI_FIRST_BOOT_ISSUE.md   # First boot issue action plan
│  └─ PROJECT_STATUS.md          # This file
├─ sdkconfig.defaults            # Organized config
├─ partitions.csv                # Partition table
└─ CMakeLists.txt
```

## 🎓 Key Learnings

1. **ESP-Hosted Transport ≠ WiFi Operations**
   - Transport initializes SDMMC controller
   - WiFi operations use transport after initialization

2. **SDMMC Controller Sharing**
   - Two slots share one controller
   - First init owns the controller
   - Subsequent inits must use dummy functions

3. **Warm Boot Handling**
   - Hard reset cycle clears C6 state
   - GPIO power-down prevents conflicts
   - 500ms capacitor discharge is critical

4. **SDIO Timing**
   - GPIO6 handshake needed for C6 ready signal
   - 4-bit mode @ 40MHz works after handshake
   - First boot may need longer C6 boot time

## 📞 Handoff Information

### For Continuing Work

If resuming this project in a new chat session, provide this context:

```
Project: ESP32-P4 Guition JC1060 with ESP-Hosted WiFi via C6

Current Status:
- Bootstrap sequence working (Power → ESP-Hosted → SD)
- SD card + WiFi coexist on shared SDMMC controller
- Minor issue: WiFi connection fails on first boot after flash

Next Task:
- Implement GPIO6 handshake for C6 firmware ready signal
- Location: main/bootstrap_manager.c, bootstrap_wifi_sequence()
- Goal: Wait for C6 ready before WiFi operations

Documentation:
- docs/BOOTSTRAP_SEQUENCE.md (complete sequence)
- docs/WIFI_FIRST_BOOT_ISSUE.md (issue + action plan)
- docs/PROJECT_STATUS.md (this file)

Repository: https://github.com/CristianoGorla/host_sdcard_with_hosted
Branch: main
Commit: v1.0.0-beta-60-g274c71b
```

---

**Project Health**: 🟢 Stable and ready for GPIO6 handshake implementation
