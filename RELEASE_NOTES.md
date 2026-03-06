# Release Notes

## v1.3.0-dev (2026-03-06)

**Status:** Development Branch (`develop/v1.3.0`)  
**Focus:** System Monitor UI with LVGL v9 integration, BSP architecture cleanup

### ✨ Major Features

#### System Monitor Dashboard (NEW)
- ✅ **Professional LVGL v9 UI** with two interactive screens
  - **Screen 1**: 12 hardware peripheral status cards with live monitoring
  - **Screen 2**: 9 debug/development tool launchers
- ✅ **Touch navigation**: Horizontal swipe between screens with page indicators
- ✅ **Auto-refresh**: 2-second intervals (configurable via Kconfig)
- ✅ **Color-coded status**: ✅ OK (green) | ⚠️ WARN (orange) | ❌ ERR (red) | ⚪ OFF (gray) | ⚫ N/A (dim)
- ✅ **System info header**: Real-time heap, PSRAM, uptime display
- ✅ **Dark theme**: Professional appearance with cyan accent colors
- 📸 **Screenshots**: Added to repository (20260306_092912.jpg, 20260306_092922.jpg)

#### LVGL v9.2.2 Integration
- ✅ **Memory-optimized configuration**: ~2.0 MB total (display + LVGL buffers)
- ✅ **DSI configuration**: `avoid_tearing=false` + `num_fbs=1` (saved 800 KB)
- ✅ **DMA2D acceleration**: Hardware graphics acceleration enabled
- ✅ **Touch input integration**: GT911 fully integrated with LVGL input driver
- ✅ **Display rotation**: Software rotation support (0°/90°/180°/270°)
- 📄 Complete configuration guide: [docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md)

### 🔧 Changed

#### Documentation Improvements
- **README.md restructured**: "What This Project Does" section prominent at top
- **Display screenshots integrated**: System Monitor UI visible in README
- **Boot log updated**: Complete v1.3.0-dev boot sequence with panel-style logs
- **Known issues moved**: SD Card limitations clearly documented at bottom
- **Badge added**: LVGL v9.2.2 badge in README header

#### BSP Architecture
- **`main` log tag normalized** from legacy naming to `MAIN`
- **Boot banner redesigned** with framed ASCII art output
- **Panel logging utility** (`bsp_log_panel.h`): Consistent `BSP: | UNIT | message` format
- **BSP hardware test service** (`bsp_tests`): Hardware checks delegated to BSP APIs
- **External log noise filtering**: Moved to BSP ownership, menuconfig-driven

### 📋 System Status

#### ✅ Stable Components
- Display (JD9165 1024×600 MIPI-DSI) + LVGL v9.2.2
- Touch (GT911 5-point capacitive) + LVGL input
- I2C Bus (400kHz, GPIO7/8)
- Audio (ES8311 codec + NS4150 PA)
- RTC (RX8025T battery-backed)
- WiFi (ESP-Hosted via ESP32-C6) - disabled by default in sdkconfig.defaults
- PSRAM (32 MB @ 200MHz)
- Bootstrap Manager (three-phase initialization)

#### ⚠️ Experimental/Disabled
- **SD Card**: Disabled by default due to SDMMC slot arbitration issue (Error 0x108)
  - Simultaneous WiFi + SD Card causes boot loop
  - See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for technical details

### 🎨 System Monitor Features

#### Peripheral Status Cards (Screen 1)
Each card displays:
- Hardware name and model
- Technical specifications (resolution, frequency, bus type)
- Connection details (I2C address, GPIO pins, slot number)
- Live status indicator with color coding
- Last update timestamp

**Monitored Peripherals**:
1. Display (MIPI-DSI 1024×600)
2. Touch Controller (GT911 @ 0x14)
3. I2C Bus (400kHz, GPIO7/8)
4. Audio Codec (ES8311 @ 0x18 + PA)
5. Real-Time Clock (RX8025T @ 0x32)
6. SD Card (SDMMC Slot 0) - ⚠️ Experimental
7. WiFi (ESP-Hosted, SDMMC Slot 1)
8. Camera (MIPI CSI) - 🚧 Not implemented
9-12. Future expansion slots

#### Debug Tools (Screen 2)
1. **Serial Log Monitor** - ESP-IDF log viewer
2. **Camera Test** - Live preview (pending implementation)
3. **Sensor Monitor** - Real-time data graphs
4. **WiFi Scanner** - Network discovery with RSSI
5. **SD Card Browser** - File manager
6. **I2C Bus Scanner** - Device detection (0x00-0x7F)
7. **System Info** - CPU, memory, task statistics
8. **GPIO Monitor** - Pin state viewer
9. **Performance** - FPS, latency, LVGL profiling

### 🔧 Configuration

#### New Kconfig Options

**System Monitor** (`Component config → Guition Demo Configuration`):
```kconfig
LVGL_DEMO_SYSTEM_MONITOR       # Enable System Monitor UI (default: y)
SYSTEM_MONITOR_AUTO_REFRESH    # Enable auto-refresh (default: y)
SYSTEM_MONITOR_REFRESH_INTERVAL_MS  # Refresh interval (default: 2000ms)
```

**Default Peripheral Configuration**:
```kconfig
BSP_ENABLE_DISPLAY=y           # Display + LVGL
BSP_ENABLE_I2C=y               # I2C bus
BSP_ENABLE_TOUCH=y             # Touch + LVGL input
BSP_ENABLE_AUDIO=y             # Audio codec
BSP_ENABLE_RTC=y               # Real-time clock
BSP_ENABLE_WIFI=n              # WiFi (disabled in sdkconfig.defaults)
BSP_ENABLE_SDCARD=n            # SD Card (disabled - see notice)
```

### 📦 Installation

**Quick Start (LVGL + System Monitor)**:
```bash
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout develop/v1.3.0
idf.py set-target esp32p4
idf.py build flash monitor
```

**Boot time**: ~3.6 seconds from reset to UI ready

### ⚠️ Known Issues

#### SD Card + WiFi Conflict (Critical)

**Problem**: SDMMC controller slot arbitration fails when switching from WiFi (Slot 1) to SD Card (Slot 0)

**Symptoms**:
```
E (8799) sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x108
E (8799) H_SDIO_DRV: failed to read registers
I (8799) H_SDIO_DRV: Host is resetting itself
I (8799) os_wrapper_esp: Restarting host
```

**Root Cause**: ESP-Hosted SDIO driver detects timeout error (0x108) during controller deinitialization and triggers automatic host restart, causing infinite boot loop.

**Workaround**: Use WiFi-only configuration (default)
```kconfig
CONFIG_BSP_ENABLE_SDCARD=n  # SD Card disabled
CONFIG_BSP_ENABLE_WIFI=y     # WiFi enabled
```

**Status**: Issue documented, driver-level fix required in ESP-Hosted. See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for complete technical analysis.

### 📚 Documentation Updates

**New Documents**:
- [prompts/DASHBOARD_UI.md](prompts/DASHBOARD_UI.md) - System Monitor implementation guide
- [docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md) - LVGL memory optimization guide

**Updated Documents**:
- [README.md](README.md) - Restructured with "What This Project Does" section, display photos
- [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) - Current development status
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) - SD Card slot arbitration analysis

### 🎯 Roadmap

**Immediate (v1.3.x)**:
- [ ] Implement debug tool functions (Log Monitor, I2C Scanner, etc.)
- [ ] Audio playback integration with LVGL controls
- [ ] Camera live preview (MIPI CSI)
- [ ] WiFi configuration UI screen
- [ ] Settings persistence (NVS integration)

**Near-term (v1.4.0)**:
- [ ] ESP-Hosted slot arbitration fix (pending driver patch)
- [ ] SD Card + WiFi simultaneous support
- [ ] OTA update support
- [ ] Power management optimization

**Long-term (v2.0.0)**:
- [ ] Custom application templates
- [ ] Web dashboard (REST API)
- [ ] BLE support via ESP-Hosted
- [ ] Performance profiling suite

### ✅ Validation Summary

- ✅ Project builds successfully on ESP-IDF v5.5.3
- ✅ System Monitor UI fully functional with touch input
- ✅ All I2C peripherals stable (Touch, Audio, RTC)
- ✅ WiFi stable when enabled (disabled by default)
- ✅ Display 1024×600 with LVGL v9 stable
- ✅ Memory usage optimized (~2.0 MB)
- ✅ Boot sequence deterministic (~3.6s)
- ⚠️ SD Card disabled due to slot arbitration issue

---

## v1.0.0-beta (2026-03-01)

**Status:** Beta Release - All Features Validated  
**Target:** Guition JC1060P470C Development Board (ESP32-P4 + ESP32-C6)

### ✨ New Features

#### Hardware Support
- ✅ **I2C Bus** (GPIO7/8 @ 400kHz) - Fully operational
- ✅ **ES8311 Audio Codec** (0x18) - I2C initialization and detection
- ✅ **RX8025T RTC** (0x32) - Time read/write with NTP sync support
- ✅ **JD9165 MIPI DSI Display** (1024x600) - Full initialization
- ✅ **GT911 Capacitive Touch** (0x14) - Auto-detection and initialization
- ✅ **SD Card SDMMC** (Slot 0, 4-bit) - FAT filesystem with VFS
- ✅ **ESP-Hosted WiFi** (Slot 1, 4-bit) - ESP32-C6 via SDIO
- ✅ **NVS Flash** - Non-volatile storage

#### Advanced Features
- ✅ **WiFi Connection Test** - Connect to AP and display IP/RSSI
- ✅ **RTC NTP Synchronization** - Sync hardware RTC with internet time
- ✅ **Timezone Support** - CET (UTC+1) with automatic DST
- ✅ **Feature Flags System** - Modular enable/disable of peripherals
- ✅ **Debug Logging** - Per-component detailed logs

#### Developer Experience
- ✅ **Comprehensive Documentation** - README.md with status tables
- ✅ **Troubleshooting Guide** - Complete reset behavior analysis
- ✅ **Boot Log Documentation** - Expected timing for each stage
- ✅ **Sanitization Rules** - Automated sensitive data protection
- ✅ **CI/CD Workflow** - GitHub Actions for sanitization checks

### 🔧 Improvements

#### Stability
- **Fixed:** lwIP duplicate in CMakeLists.txt causing WiFi instability
- **Fixed:** I2C scan interference with GT911 touch controller
- **Fixed:** SD card `0x107` errors after hardware reset (documented as expected)
- **Improved:** Direct device initialization (no pre-probe)
- **Improved:** Consistent initialization patterns across all I2C devices

#### Documentation
- Complete hardware pinout tables
- Reset behavior comparison (5 types)
- Expected boot log output with timing
- Configuration files reference
- Feature flags comprehensive list

### 📋 Configuration

#### Default Settings (feature_flags.h)
```c
ENABLE_I2C 1           // I2C bus
ENABLE_AUDIO 1         // ES8311 codec
ENABLE_RTC 1           // RX8025T RTC
ENABLE_DISPLAY 1       // JD9165 display
ENABLE_TOUCH 1         // GT911 touch
ENABLE_SD_CARD 1       // SD card
ENABLE_WIFI 1          // ESP-Hosted WiFi
ENABLE_WIFI_CONNECT 1  // WiFi connection test
ENABLE_I2C_SCAN 0      // DISABLED (required)
```

#### Build Configuration
- ESP-IDF: v5.5.3-dirty
- Target: ESP32-P4
- CPU: 360 MHz
- PSRAM: 32 MB @ 200MHz
- Flash: 16 MB @ 40MHz QIO

### ⚠️ Known Issues

#### Reset Behavior
- **Hardware button reset:** SD card may fail with `0x107` error
  - **Workaround:** Use IDF monitor restart (Ctrl+T, Ctrl+R)
  - **Root cause:** Hardware state inconsistency (documented in troubleshooting.md)
  - **Status:** Expected behavior for complex embedded systems

- **USB disconnect/reconnect:** Both SD and WiFi may fail
  - **Workaround:** Full power cycle (disconnect 5+ seconds)
  - **Recommended:** Use IDF monitor for development

#### Limitations
- **Audio:** I2C initialization only (no I2S playback)
  - Full audio support requires `esp_codec_dev` BSP
- **Touch:** Initialization only (no continuous input reading in default config)
- **Display:** Initialization only (no LVGL UI in default config)

### 📦 Installation

```bash
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout v1.0.0-beta

# Configure WiFi credentials (optional)
cp main/wifi_config.h.example main/wifi_config.h
# Edit main/wifi_config.h with your SSID/password

# Build and flash
idf.py build flash monitor
```

### 🔄 Upgrade from Previous Versions

This is the first tagged release. If you have cloned the repository before:

```bash
git fetch --tags
git checkout v1.0.0-beta

# Check for breaking changes
git diff main..v1.0.0-beta main/feature_flags.h

# Rebuild
rm -rf build
idf.py build flash monitor
```

### 📚 Documentation

- **README.md** - System overview, feature tables, usage guide
- **troubleshooting.md** - Complete troubleshooting guide with reset behavior analysis
- **SANITIZATION_RULES.md** - Permanent rules for sensitive data protection
- **RELEASE_NOTES.md** - This file

### 🔒 Security

#### Sanitization
- All documentation sanitized (IPs, MACs, SSIDs, paths)
- Automated CI checks enforce sanitization rules
- Confidential PDFs gitignored
- WiFi credentials never committed

#### Sanitized Values
- Gateway IP: `10.0.0.1` (was: `192.168.x.1`)
- Host IP: `10.0.0.100` (was: `192.168.x.100`)
- Gateway MAC: `AA:BB:CC:00:00:01`
- Host MAC: `AA:BB:CC:00:00:02`
- SSID: `GUITION_BETA_AP` or placeholders

### 🧪 Testing

#### Test Coverage
- ✅ All hardware initialization (8 components)
- ✅ WiFi connection and IP acquisition
- ✅ SD card mount and filesystem operations
- ✅ RTC time read/write
- ✅ RTC NTP synchronization
- ✅ Reset behavior (5 types documented)
- ✅ I2C device detection and initialization

#### Continuous Integration
- ✅ Sanitization checks (GitHub Actions)
- ✅ Build validation (manual)
- ✅ Documentation consistency (manual)

### 🎯 Roadmap to v1.0.0 (Production)

**Before removing "-beta" suffix:**
- [ ] Extended field testing (1+ week)
- [ ] Community feedback integration
- [ ] Additional hardware validation
- [ ] Performance benchmarks
- [ ] Power consumption analysis

**Future enhancements (v1.1.0+):**
- LVGL UI integration ✅ **DONE in v1.3.0-dev**
- I2S audio playback
- Touch input event handling ✅ **DONE in v1.3.0-dev**
- BLE support via ESP-Hosted
- Web dashboard
- OTA updates

### 🙏 Acknowledgments

- **Espressif Systems** - ESP-IDF framework and ESP-Hosted
- **Guition** - JC1060P470C development board and demo code
- **Open Source Community** - Component drivers and documentation

### 📝 License

Apache License 2.0 - See LICENSE file for details

---

**Full Changelog:** 
- [v1.3.0-dev](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/tree/develop/v1.3.0) (current)
- [v1.0.0-beta](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.0.0-beta)
