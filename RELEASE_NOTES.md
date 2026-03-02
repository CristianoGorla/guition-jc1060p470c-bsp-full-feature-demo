# Release Notes

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
- LVGL UI integration
- I2S audio playback
- Touch input event handling
- BLE support via ESP-Hosted
- Web dashboard
- OTA updates

### 🙏 Acknowledgments

- **Espressif Systems** - ESP-IDF framework and ESP-Hosted
- **Guition** - JC1060P470C development board and demo code
- **Open Source Community** - Component drivers and documentation

### 📝 License

Unlicense - See LICENSE file for details

---

## Next Release: v1.1.0-dev

**Expected:** TBD  
**Focus:** Bootstrap Manager, external peripherals, testing suite

Development continues on `main` branch with version `1.1.0-dev`.

---

**Full Changelog:** [v1.0.0-beta](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.0.0-beta)
