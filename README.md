# Guition JC1060P470C BSP - Full Feature Demo

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-blue)](https://github.com/espressif/esp-idf)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32--P4-orange)](https://www.espressif.com/en/products/socs/esp32-p4)
[![LVGL](https://img.shields.io/badge/LVGL-v9.2.2-purple)](https://lvgl.io/)
[![Development](https://img.shields.io/badge/dev-v1.3.0--dev-yellow)](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo)

**Complete Board Support Package and System Monitor for the Guition JC1060P470C_I_W_Y development board featuring ESP32-P4 with LVGL v9 graphics integration.**

---

## 📋 What This Project Does

This firmware provides a **complete hardware demonstration** for the Guition JC1060P470C board with a professional **System Monitor UI**:

### 🖥️ System Monitor Dashboard (LVGL v9)

**Two interactive screens** accessible via horizontal swipe:

#### Screen 1: Hardware Peripherals Status
![Peripheral Status Screen](20260306_092912.jpg)

Real-time monitoring of **12 hardware peripherals** with color-coded status indicators:
- **Display** (MIPI-DSI 1024×600)
- **Touch Controller** (GT911 5-point capacitive)
- **I2C Bus** (400kHz)
- **Audio Codec** (ES8311 + PA)
- **Real-Time Clock** (RX8025T with battery backup)
- **SD Card** (SDMMC Slot 0)
- **WiFi** (ESP-Hosted via ESP32-C6)
- **Camera** (MIPI CSI interface)
- Plus 4 future expansion slots

Each card shows:
- Hardware name and technical specs
- Connection details (I2C address, GPIO pins, bus type)
- Live status: ✅ OK (green) | ⚠️ WARN (orange) | ❌ ERR (red) | ⭕ OFF (gray) | ⚫ N/A (dim)
- System info: Heap memory, PSRAM usage, uptime

#### Screen 2: Debug & Development Tools
![Debug Tools Screen](20260306_092922.jpg)

**9 interactive tool launchers** for hardware testing and diagnostics:
- **Serial Log Monitor** - ESP-IDF log viewer with filtering
- **Camera Test** - Live preview with touch controls (Gain/Exposure)
- **Sensor Monitor** - Real-time data graphs
- **WiFi Scanner** - Network discovery with RSSI
- **SD Card Browser** - File manager
- **I2C Bus Scanner** - Device detection (0x00-0x7F)
- **System Info** - CPU, memory, task stats
- **GPIO Monitor** - Pin state viewer
- **Performance** - FPS, latency, LVGL profiling

**Features:**
- Touch-responsive cards with visual feedback
- Auto-refresh every 2 seconds (configurable via Kconfig)
- Horizontal swipe navigation with page indicators
- Dark theme with cyan accent colors
- Validated LVGL v9.2.2 configuration for DSI + touch dashboard workflows

### 🔧 Hardware Support

Complete driver integration for all onboard peripherals:
- **Display**: JD9165 4.7" 1024×600 MIPI-DSI with DMA2D acceleration
- **Touch**: GT911 capacitive controller with hardware reset and LVGL input integration
- **Audio**: I2S codec with integrated power amplifier control
- **RTC**: Battery-backed real-time clock with automatic NTP synchronization
- **WiFi**: ESP-Hosted SDIO communication with ESP32-C6 coprocessor
- **Storage**: SD card support (SDMMC, 4-bit mode @ 40MHz) - see limitations below

### ⚡ Software Architecture

**Three-Phase Bootstrap Manager:**
Deterministic initialization sequence preventing SDMMC bus conflicts:
1. **Phase A**: Power management (SD card power control, rail stabilization)
2. **Phase C**: WiFi manager (ESP-Hosted SDIO transport initialization)
3. **Phase B**: SD manager (optional, clean slot switching)

**Kconfig-Based Configuration:**
All peripherals can be enabled/disabled via menuconfig without code changes.

**LVGL v9 Integration:**
Optimized memory configuration with DSI display support:
- Dual hardware frame buffers in current BSP display configuration (~2.4 MB)
- LVGL draw buffer in PSRAM (~750 KB)
- End-to-end DSI/LVGL pipeline validated on ESP32-P4
- DMA2D acceleration enabled
- Touch input integrated

---

## 🚀 Quick Start

### Prerequisites

1. **ESP-IDF v5.5.3+** installed and configured
2. **Guition JC1060P470C_I_W_Y** board
3. **USB-C cable** for programming
4. **(Optional)** Vendor documentation package - see [Vendor Resources](#-vendor-resources) below

### Build and Flash

```bash
# Clone repository
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo

# Checkout development branch
git checkout develop/v1.3.0

# Set target
idf.py set-target esp32p4

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

### Expected Boot Log

```
I (11) boot: ESP-IDF v5.5.3-dirty 2nd stage bootloader
I (11) boot: compile time Mar  5 2026 21:13:48
I (11) boot: Multicore bootloader
I (13) boot: chip revision: v1.3
I (195) hex_psram: vendor id    : 0x0d (AP)
I esp_psram: Found 32MB PSRAM device
I esp_psram: Speed: 200MHz
I (924) cpu_start: Pro cpu start user code
I (924) cpu_start: cpu freq: 360000000 Hz
I (924) app_init: Application information:
I (925) app_init: Project name:     guition-jc1060p470c-bsp-full-fe
I (925) app_init: App version:      freeze-lvgl-v9-integration-2026
I (925) app_init: Compile time:     Mar  6 2026 00:06:29
I (926) app_init: ESP-IDF:          v5.5.3-dirty
I (927) heap_init: At 4FF2AED0 len 000100F0 (64 KiB): RETENT_RAM
I (928) heap_init: At 4FF42BB0 len 0003D450 (245 KiB): RAM
I (929) esp_psram: Adding pool of 31680K of PSRAM memory to heap allocator

I (934) MAIN: ┌==============================================================================┐
I (935) MAIN: │                       ____    _            ___    ___                        │
I (935) MAIN: │                      | __ )  (_)   ___    ( _ )  / _ \                       │
I (935) MAIN: │                      |  _ \  | |  / _ \   / _ \ | | | |                      │
I (935) MAIN: │                      | |_) | | | | |_| | | (_) || |_| |                      │
I (936) MAIN: │                      |____/  |_|  \___/   \___/  \___/                       │
I (936) MAIN: │                                                                              │
I (936) MAIN: │                      https://github.com/CristianoGorla/                      │
I (936) MAIN: │                  guition-jc1060p470c-bsp-full-feature-demo                   │
I (937) MAIN: │                                                                              │
I (937) MAIN: │                         Guition JC1060P470C Firmware                         │
I (937) MAIN: │                                                                              │
I (937) MAIN: │Version: v1.3.0-dev                                             Build: 9d3f376│
I (937) MAIN: └==============================================================================┘
I (938) MAIN: 
I (938) MAIN:                              2026-03-06 08:20:57
I (938) MAIN: 
I (938) BSP: | CORE       | [LOG] External log noise filter enabled (level=NONE)
I (938) BSP: | CORE       | ==============================================================
I (938) BSP: | CORE       |   Guition BSP v1.3.0
I (939) BSP: | CORE       |   Hardware Layer Only
I (939) BSP: | CORE       | ==============================================================
I (939) BSP: | CORE       | [PHASE A] Power Manager...
I (939) BSP: | CORE       | [RESET] Clean boot - no hard reset
I (1089) BSP: | CORE       | [PHASE A] [OK] POWER_READY
I (1089) BSP: | CORE       | [PHASE D] Peripheral Drivers...
I (1089) BSP: | JD9165     | Initializing JD9165 display (1024x600, 2-lane DSI)
I (1089) BSP: | JD9165     | Backlight PWM initialized (GPIO 23, 20kHz)
I (1089) BSP: | JD9165     | MIPI DSI PHY powered on (LDO3 @ 2.5V)
I (1379) BSP: | JD9165     | Display initialized successfully (dual FB mode)
I (1379) BSP: | CORE       | [PHASE D] [OK] Display HW
I (1379) BSP: | CORE       | [I2C] [OK] Ready
I (1379) BSP: | GT911      | Initializing GT911 touch controller
I (1379) BSP: | GT911      | Hardware reset enabled (GPIO 21)
I (1445) BSP: | GT911      | Reset sequence complete, GT911 address set to 0x14
I (1465) BSP: | GT911      | GT911 initialized (addr 0x14, 1024x600, 5 points, polling mode)
I (1465) BSP: | GT911      | 
I (1465) BSP: | GT911      | ========== GT911 CONFIGURATION ==========
I (1466) BSP: | GT911      | Config Version: 0x63
I (1466) BSP: | GT911      | X Resolution: 1024 (expected 1024) [PASS]
I (1466) BSP: | GT911      | Y Resolution: 600 (expected 600) [PASS]
I (1466) BSP: | GT911      | Max Touch Points: 5 (expected 5) [PASS]
I (1466) BSP: | GT911      |   Config bytes (0x8047-0x804C): 63 00 04 58 02 05 
I (1467) BSP: | GT911      | =========================================
I (1467) BSP: | GT911      | 
I (1467) BSP: | GT911      | Touch monitor task disabled (LVGL handles polling)
I (1467) BSP: | CORE       | [PHASE D] [OK] Touch HW
I (1467) BSP: | AUDIO      | Initializing ES8311 + NS4150 audio system
I (1468) BSP: | AUDIO      | NS4150 amplifier control initialized (GPIO 11)
I (1468) BSP: | AUDIO      | I2S initialized (48000 Hz, 16-bit, MCLK=9, BCLK=13, WS=12, DOUT=10)
I (1468) BSP: | AUDIO      | ES8311 codec initialized (I2C address 0x18)
I (1469) BSP: | AUDIO      | NS4150 power amplifier enabled
I (1469) BSP: | AUDIO      | Audio system initialized (48000 Hz, 16-bit, PA enabled)
I (1469) BSP: | CORE       | [PHASE D] [OK] Audio
I (1469) BSP: | RTC        | Initializing RX8025T RTC
I (1469) BSP: | RTC        | RX8025T initialized (address 0x32, INT on GPIO 0)
I (1470) BSP: | CORE       | [PHASE D] [OK] RTC
I (1470) BSP: | CORE       | [PHASE D] [OK] Complete
I (1470) BSP: | CORE       | ==============================================================
I (1470) BSP: | CORE       |   [OK] BSP Ready (Hardware only)
I (1470) BSP: | CORE       |   App must init LVGL separately
I (1470) BSP: | CORE       | ==============================================================

I (3591) lvgl_dashboard: Dashboard initialized: 12 peripherals, 9 tools
I (3641) MAIN: UI ready: dashboard loaded
I (3641) MAIN: === System Ready ===

I (3641) MAIN: Entering idle loop...
```

**Boot time**: ~3.6 seconds from reset to UI ready

---

## ⚙️ Configuration

All features can be configured via **menuconfig** without code changes:

```bash
idf.py menuconfig
```

Navigate to: **Guition JC1060P470C Board Configuration**

### Hardware Peripherals

| Peripheral | Config Option | Default | Status |
|------------|--------------|---------|--------|
| **Display** | `BSP_ENABLE_DISPLAY` | ✅ ON | ✅ Stable |
| **LVGL** | `BSP_ENABLE_LVGL` | ✅ ON | ✅ Stable |
| **I2C Bus** | `BSP_ENABLE_I2C` | ✅ ON | ✅ Stable |
| **Touch** | `BSP_ENABLE_TOUCH` | ✅ ON | ✅ Stable |
| **Audio** | `BSP_ENABLE_AUDIO` | ✅ ON | ✅ Stable |
| **RTC** | `BSP_ENABLE_RTC` | ✅ ON | ✅ Stable |
| **WiFi** | `BSP_ENABLE_WIFI` | ❌ OFF | ✅ Stable (disabled in sdkconfig.defaults) |
| **SD Card** | `BSP_ENABLE_SDCARD` | ❌ OFF | ⚠️ Experimental |

### System Monitor Configuration

In menuconfig: **Guition Demo Configuration → LVGL Demo Selection**

```kconfig
config LVGL_DEMO_SYSTEM_MONITOR
    bool "System Monitor (Recommended)"
    default y

config SYSTEM_MONITOR_AUTO_REFRESH
    bool "Enable auto-refresh"
    depends on LVGL_DEMO_SYSTEM_MONITOR
    default y

config SYSTEM_MONITOR_REFRESH_INTERVAL_MS
    int "Refresh interval (ms)"
    depends on SYSTEM_MONITOR_AUTO_REFRESH
    range 500 10000
    default 2000
```

---

## 📊 System Status

### ✅ Working Components

| Component | Version | Status | Notes |
|-----------|---------|--------|-------|
| **ESP-IDF** | v5.5.3 | ✅ Stable | Framework |
| **LVGL** | v9.2.2 | ✅ Stable | Graphics library |
| **Display** | JD9165 | ✅ Stable | 1024×600 MIPI-DSI |
| **Touch** | GT911 | ✅ Stable | 5-point capacitive |
| **I2C Bus** | 400kHz | ✅ Stable | GPIO7/8 |
| **Audio** | ES8311 | ✅ Stable | I2S codec + PA |
| **RTC** | RX8025T | ✅ Stable | Battery-backed |
| **WiFi** | ESP-Hosted | ✅ Stable | SDMMC Slot 1 (disabled by default) |
| **PSRAM** | 32MB | ✅ Stable | 200MHz |
| **Flash** | 16MB | ✅ Stable | 40MHz QIO |

### ⚠️ Experimental/Limited Components

| Component | Status | Notes |
|-----------|--------|-------|
| **SD Card** | ⚠️ Disabled by default | See Known Limitations below |
| **Camera** | ✅ Preview tool available | OV02C10 live preview, long-press exit, Gain/Exposure sliders |

---

## 📌 Hardware Pinout

### Display (MIPI-DSI)
- **Controller**: JD9165
- **Resolution**: 1024×600 pixels
- **Interface**: 2-lane MIPI-DSI
- **Backlight**: GPIO23 (PWM)

### Touch Controller (I2C)
- **Model**: GT911
- **Address**: 0x14
- **SDA/SCL**: GPIO7/8
- **Reset**: GPIO21
- **Interrupt**: GPIO20

### Audio (I2S)
- **Codec**: ES8311 (I2C 0x18)
- **MCLK**: GPIO9
- **BCLK**: GPIO13
- **WS**: GPIO12
- **DOUT**: GPIO10
- **PA Control**: GPIO11 (NS4150)

### RTC (I2C)
- **Model**: RX8025T
- **Address**: 0x32
- **Interrupt**: GPIO0

### WiFi (SDMMC Slot 1)
- **Coprocessor**: ESP32-C6
- **Interface**: SDIO 4-bit @ 40MHz
- **CLK**: GPIO14
- **CMD**: GPIO15
- **D0-D3**: GPIO16-19
- **Reset**: GPIO54
- **Handshake**: GPIO52/53

### SD Card (SDMMC Slot 0) - Optional
- **Interface**: SDIO 4-bit @ 40MHz
- **CLK**: GPIO43
- **CMD**: GPIO44
- **D0-D3**: GPIO39-42
- **Power Enable**: GPIO36

---

## 📚 Documentation

### Technical Documentation

- **[docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md)** - LVGL memory optimization details
- **[docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md)** - Current development status
- **[RELEASE_NOTES.md](RELEASE_NOTES.md)** - Version history and changelogs
- **[troubleshooting.md](troubleshooting.md)** - Common issues and solutions
- **[SDMMC_ARBITER_README.md](SDMMC_ARBITER_README.md)** - SDMMC bus arbitration details
- **[I2C_MIPI_DSI_CONFLICT.md](I2C_MIPI_DSI_CONFLICT.md)** - I2C/MIPI DSI conflict resolution

### Vendor Documentation

- **[docs/vendor_docs/](docs/vendor_docs/)** - Original Guition hardware documentation, datasheets, and reference code
  - Schematics and PCB layout files
  - Component datasheets (Display, Touch, Audio, RTC, WiFi module)
  - Original vendor demo code and configuration examples
  - Hardware test tools and utilities

---

## 🏭 Vendor Resources

### Hardware Documentation Package

The original vendor documentation package contains essential hardware information:
- Board schematics and PCB layout
- Component datasheets (JD9165, GT911, ES8311, RX8025T, ESP32-C6)
- Reference demo code
- Hardware test utilities
- Configuration examples

**Vendor files included in this repository**: See [docs/vendor_docs/](docs/vendor_docs/)

### Downloading the Original Vendor Package

> [!IMPORTANT]
> **Critical: Download the CORRECT file!**
> 
> Multiple similar files exist with nearly identical names. Downloading the wrong package can result in days of failed compilations and incompatible configurations.

**Official Guition Download Portal:**
```
https://pan.jczn1688.com/1/HMI%20display
```

**Manual Download Instructions:**

1. **Navigate to the portal** using the URL above

2. **Locate the CORRECT file**: `JC1060P470C_I_W_Y.zip`
   - ⚠️ **DO NOT download**: `JC1060P470C_I_W.zip` (missing "_Y" suffix)
   - ⚠️ **DO NOT download**: Other JC1060P470C variants with different suffixes
   - ✅ **CORRECT file**: `JC1060P470C_I_W_Y.zip` (230.82 MB)

3. **Verify file details before downloading**:
   - Filename: `JC1060P470C_I_W_Y.zip`
   - Date: 2026-01-23 10:41
   - Size: 230.82 MB

4. **Reference screenshot**: See [docs/vendor_docs/download_guide.jpg](docs/vendor_docs/) for visual confirmation of the correct file selection

**Package Contents:**
```
JC1060P470C_I_W_Y.zip
├── Hardware/
│   ├── Schematics (PDF)
│   ├── PCB Layout Files
│   └── BOM (Bill of Materials)
├── Datasheets/
│   ├── ESP32-P4 Datasheet
│   ├── JD9165 Display Controller
│   ├── GT911 Touch Controller
│   ├── ES8311 Audio Codec
│   ├── RX8025T RTC
│   └── ESP32-C6 Module
├── Demo_Code/
│   ├── ESP-IDF Examples
│   ├── LVGL Examples
│   └── Hardware Test Tools
└── Tools/
    ├── Flash Download Tools
    ├── Serial Debugging Tools
    └── Configuration Utilities
```

> [!TIP]
> **Why the correct file matters:**
> 
> Different board variants use different component configurations, pinouts, and initialization sequences. Using documentation for a different variant will result in:
> - Incorrect GPIO mappings
> - Wrong I2C addresses
> - Incompatible display initialization
> - WiFi/SD card conflicts
> - Failed peripheral detection

---

## 🔌 Advanced Features

### WiFi Connection

To enable full WiFi connection with automatic connect:

1. **Enable WiFi in menuconfig:**
   ```bash
   idf.py menuconfig
   # Guition Board Config → Hardware Peripherals
   # → [x] Enable WiFi
   ```

2. **Create credentials file:**
   ```bash
   cd main
   cp wifi_config.h.example wifi_config.h
   ```

3. **Edit credentials:**
   ```c
   #define WIFI_SSID "YourWiFiSSID"
   #define WIFI_PASSWORD "YourWiFiPassword"
   ```

4. **Enable auto-connect:**
   ```
   Guition Board Config → Application Features
   → [x] Enable WiFi auto-connect
   ```

### RTC NTP Synchronization

Automatic time sync from internet (requires WiFi connection):

```
Guition Board Config → Application Features
→ [x] Enable RTC NTP time sync
```

**Features:**
- NTP Server: pool.ntp.org
- Timezone: CET (UTC+1) with DST
- Typical sync time: 12-13 seconds

### Debug Mode

Enable comprehensive debug logging:

```
Guition Board Config → Debug Logging
→ [x] Enable debug mode
```

Includes:
- Per-peripheral debug output
- Hardware test utilities
- System heartbeat monitoring
- Verbose I2C bus diagnostics

---

## ⚠️ Known Limitations

### SD Card + WiFi Simultaneous Use

> [!CAUTION]
> **Critical Hardware Limitation**
>
> The ESP32-P4 has **only one SDMMC controller** shared between:
> - **Slot 0**: SD Card
> - **Slot 1**: ESP-Hosted WiFi (ESP32-C6)
>
> **Current Status**: SD Card support is **disabled by default** due to unresolved slot arbitration issues.

**Problem:**
When both SD Card and WiFi are enabled:
1. Bootstrap manager attempts to switch SDMMC controller from Slot 1 (WiFi) to Slot 0 (SD Card)
2. During `sdmmc_host_deinit()`, ESP-Hosted detects error **0x108 (SDIO timeout)**
3. ESP-Hosted automatically restarts the host system (`esp_restart()`)
4. Results in **infinite boot loop**

**Workaround:**
Use **WiFi-only configuration** (default):
```
CONFIG_BSP_ENABLE_SDCARD=n  # SD Card disabled
CONFIG_BSP_ENABLE_WIFI=y     # WiFi enabled
```

**Experimental SD Card Support:**
If you need SD Card access and accept risks:

1. **Disable WiFi** or **Disable ESP-Hosted auto-restart**:
   ```bash
   idf.py menuconfig
   # Component config → ESP-Hosted config
   # → [ ] Restart transport on failure
   ```

2. **Enable SD Card**:
   ```bash
   # Guition Board Config → Hardware Peripherals
   # → [x] Enable SD Card
   ```

> [!WARNING]
> **This configuration may leave ESP-Hosted in an undefined state. WiFi functionality will be degraded after the error occurs.**

**Root Cause:**
The SDMMC controller cannot be safely switched between slots while ESP-Hosted transport is active. A driver-level fix is required in ESP-Hosted to support clean slot arbitration.

**Status**: Issue documented, fix pending in ESP-Hosted repository.

---

## 🐛 Troubleshooting

### Boot Loop After Enabling SD Card

**Symptoms:**
```
E (8799) sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x108
E (8799) H_SDIO_DRV: failed to read registers
I (8799) H_SDIO_DRV: Host is resetting itself
I (8799) os_wrapper_esp: Restarting host
```

**Solution**: Disable SD Card (see Known Limitations above)

### LVGL Display Not Working

**Symptoms:**
```
E (1464) lcd.dsi: esp_lcd_dpi_panel_get_frame_buffer(409): invalid frame buffer number
```

**Solution**: Already fixed in this branch! Configuration uses `avoid_tearing=false` with `num_fbs=1`.

See [docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md) for details.

### Touch Not Responding

**Checklist:**
1. Verify `CONFIG_BSP_ENABLE_TOUCH=y`
2. Check I2C bus is enabled (`CONFIG_BSP_ENABLE_I2C=y`)
3. Ensure GT911 hardware reset is configured (GPIO21)
4. Verify LVGL touch integration is enabled

### WiFi Connection Timeout

**Checklist:**
1. Credentials correct in `main/wifi_config.h`
2. Router broadcasts 2.4GHz (5GHz not supported)
3. `CONFIG_APP_ENABLE_WIFI_CONNECT=y` in menuconfig
4. ESP-Hosted initialized successfully (check boot log)

**See [troubleshooting.md](troubleshooting.md) for complete troubleshooting guide.**

---

## 📦 Build Information

| Parameter | Value | Notes |
|-----------|-------|-------|
| **ESP-IDF** | v5.5.3 | Framework version |
| **Target** | ESP32-P4 | Main chip |
| **CPU Frequency** | 360 MHz | Clock speed |
| **PSRAM** | 32 MB @ 200MHz | External RAM |
| **Flash** | 16 MB @ 40MHz | SPI flash (QIO mode) |
| **LVGL** | v9.2.2 | Graphics library |
| **Memory Usage** | ~3.2 MB | Current BSP dual-FB display + LVGL draw buffer |
| **Boot Time** | ~3.6 seconds | Reset to UI ready |

---

## 🗺️ Roadmap

### Current Focus

- ✅ System Monitor UI complete
- ✅ LVGL v9 integration stable
- ✅ WiFi fully functional (disabled by default)
- ⚠️ SD Card support suspended (driver issue)

### Planned Features

- [ ] Debug tool implementations (Log Monitor, I2C Scanner, etc.)
- [ ] Audio playback integration with LVGL
- [ ] Camera still capture/save workflow (from Camera Test)
- [ ] Additional camera controls (AWB/AE presets, profile switching)
- [ ] Custom application templates
- [ ] Power management integration
- [ ] ESP-Hosted slot arbitration fix

---

## 📋 Versions

### Development Version - Current Line

**v1.3.0-dev** (`develop/v1.3.0`, current branch)

- ✅ LVGL v9.2.2 fully integrated with System Monitor UI
- ✅ Optimized memory configuration (avoid_tearing=false)
- ✅ Display 1024×600 with touch input
- ✅ WiFi (ESP-Hosted) supported (disabled by default in sdkconfig.defaults)
- ⚠️ **SD Card disabled by default** (slot arbitration issue)
- 📄 See [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) for latest updates

**Installation:**
```bash
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout develop/v1.3.0
idf.py build flash monitor
```

### Latest Stable Release

**[v1.0.0-beta](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.0.0-beta)** (2026-03-01)

- ✅ All 8 onboard peripherals fully functional (including SD Card)
- ✅ Three-phase bootstrap manager with deterministic initialization
- ✅ WiFi connection and RTC NTP synchronization
- ⚠️ Beta status: Production testing ongoing
- ❌ No LVGL integration

**See [RELEASE_NOTES.md](RELEASE_NOTES.md) for complete version history.**

---

## 🤝 Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

## 📝 License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

---

**Project**: Guition JC1060P470C BSP - Full Feature Demo  
**Branch**: develop/v1.3.0  
**Status**: ✅ System Monitor Stable | LVGL v9 Stable | WiFi Supported  
**Last Updated**: 2026-03-08
