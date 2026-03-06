# Vendor Documentation

This directory contains the original Guition hardware documentation, datasheets, and reference materials for the **JC1060P470C_I_W_Y** development board.

---

## 📁 Contents

The vendor documentation package includes:

### Hardware Design Files
- **Schematics**: Complete board schematic diagrams (PDF format)
- **PCB Layout**: PCB design files and layout reference
- **BOM**: Bill of Materials with component specifications

### Component Datasheets
- **ESP32-P4**: Main microcontroller datasheet and technical reference
- **JD9165**: MIPI-DSI display controller specifications
- **GT911**: Capacitive touch controller datasheet
- **ES8311**: I2S audio codec specifications
- **RX8025T**: Real-time clock module datasheet
- **ESP32-C6**: WiFi coprocessor module documentation
- **NS4150**: Audio power amplifier specifications

### Reference Code
- **ESP-IDF Examples**: Vendor-provided demo applications
- **LVGL Examples**: Display and touch integration examples
- **Hardware Test Tools**: Peripheral testing utilities
- **Configuration Examples**: Reference sdkconfig files

### Development Tools
- **Flash Download Tools**: Firmware programming utilities
- **Serial Debugging Tools**: UART communication tools
- **Configuration Utilities**: Hardware setup and testing tools

---

## 📊 Downloading the Official Package

### Official Guition Download Portal

```
https://pan.jczn1688.com/1/HMI%20display
```

### ⚠️ Critical: Download the CORRECT File

> [!IMPORTANT]
> **Multiple similar files exist with nearly identical names!**
>
> Downloading the wrong package will result in:
> - Incorrect GPIO mappings
> - Wrong I2C device addresses
> - Incompatible display initialization sequences
> - WiFi/SD card configuration conflicts
> - Failed peripheral detection
> - **Days of failed compilations and debugging**

### Manual Download Instructions

**Step 1: Navigate to Portal**
- Open: `https://pan.jczn1688.com/1/HMI%20display`
- Wait for the file list to load

**Step 2: Locate the CORRECT File**

✅ **CORRECT FILE**:
```
Filename: JC1060P470C_I_W_Y.zip
Date:     2026-01-23 10:41
Size:     230.82 MB
```

❌ **WRONG FILES** (Do NOT download):
```
JC1060P470C_I_W.zip      ← Missing "_Y" suffix
JC1060P470C_I.zip        ← Wrong variant
Other JC1060P470C_*      ← Different board variants
```

**Step 3: Visual Confirmation**

Refer to the screenshot below for exact file location:

![Correct File Selection](download_guide_screenshot.jpg)

**Key Visual Identifiers**:
- File icon: Yellow/orange ZIP archive icon
- Filename ends with: `_Y.zip` (← critical suffix)
- File size: Exactly **230.82 MB**
- Date: 2026-01-23 10:41

**Step 4: Download**
- Select **only** the `JC1060P470C_I_W_Y.zip` file
- Click download button
- Wait for download completion (~230 MB)

**Step 5: Verification**

After download, verify file integrity:

```bash
# Check file size (macOS/Linux)
ls -lh JC1060P470C_I_W_Y.zip
# Expected: ~231M (230.82 MB)

# Check file is valid ZIP
unzip -t JC1060P470C_I_W_Y.zip
# Expected: "No errors detected"
```

---

## 📚 Package Structure

After extraction, the package contains:

```
JC1060P470C_I_W_Y/
├── 01_Hardware/
│   ├── Schematic_JC1060P470C_I_W_Y.pdf
│   ├── PCB_Layout_Top.pdf
│   ├── PCB_Layout_Bottom.pdf
│   ├── BOM_Components.xlsx
│   └── Assembly_Drawing.pdf
│
├── 02_Datasheets/
│   ├── ESP32-P4_Datasheet.pdf
│   ├── JD9165_Display_Controller.pdf
│   ├── GT911_Touch_Controller.pdf
│   ├── ES8311_Audio_Codec.pdf
│   ├── RX8025T_RTC.pdf
│   ├── ESP32-C6_Module.pdf
│   └── NS4150_Amplifier.pdf
│
├── 03_Demo_Code/
│   ├── ESP-IDF_v5.3/
│   │   ├── 01_Basic_Init/
│   │   ├── 02_Display_Test/
│   │   ├── 03_Touch_Test/
│   │   ├── 04_WiFi_Test/
│   │   └── 05_Full_Demo/
│   │
│   ├── LVGL_Examples/
│   │   ├── lvgl_hello_world/
│   │   ├── lvgl_widgets_demo/
│   │   └── lvgl_benchmark/
│   │
│   └── Hardware_Tests/
│       ├── i2c_scanner/
│       ├── sdcard_test/
│       ├── audio_playback/
│       └── camera_test/
│
├── 04_Tools/
│   ├── Flash_Download_Tools_v3.9.5/
│   ├── Serial_Monitor_Tools/
│   └── Configuration_Wizard/
│
├── 05_User_Manual/
│   ├── User_Manual_EN.pdf
│   ├── User_Manual_CN.pdf
│   ├── Quick_Start_Guide.pdf
│   └── FAQ.pdf
│
└── README.txt
```

---

## 🔍 Key Documents

### For Hardware Debugging
1. **Schematic**: `01_Hardware/Schematic_JC1060P470C_I_W_Y.pdf`
   - Complete GPIO mapping
   - I2C device addresses
   - Power rail specifications
   - Component placement

2. **Component Datasheets**: `02_Datasheets/`
   - Register maps
   - Initialization sequences
   - Timing diagrams
   - Electrical specifications

### For Software Development
1. **Demo Code**: `03_Demo_Code/ESP-IDF_v5.3/`
   - Reference initialization code
   - Working configuration examples
   - Hardware test utilities

2. **User Manuals**: `05_User_Manual/`
   - Board specifications
   - Feature descriptions
   - Configuration guides
   - Troubleshooting tips

---

## ❗ Why the "_Y" Suffix Matters

### Board Variant Differences

| Variant | WiFi Module | SD Card | Camera | Touch |
|---------|-------------|---------|--------|-------|
| `JC1060P470C_I` | ❌ No | ✅ Yes | ❌ No | ✅ Yes |
| `JC1060P470C_I_W` | ✅ Yes (different) | ✅ Yes | ❌ No | ✅ Yes |
| `JC1060P470C_I_W_Y` | ✅ Yes (ESP32-C6) | ✅ Yes | ✅ Yes | ✅ Yes |

**Our Board**: `JC1060P470C_I_W_Y`
- **I**: Integrated display and touch
- **W**: WiFi module (ESP32-C6 via SDIO)
- **Y**: Camera interface (MIPI CSI)

### Configuration Differences

Using wrong documentation leads to:

**GPIO Conflicts**:
```c
// WRONG (_I_W variant)
#define CAMERA_RESET_GPIO 45    // Incorrect!

// CORRECT (_I_W_Y variant)
#define CAMERA_RESET_GPIO 46    // ESP32-C6 uses GPIO45
```

**I2C Address Mismatch**:
```c
// Some variants use different touch controller addresses
// _I_W_Y uses GT911 @ 0x14 (after reset sequence)
// Other variants may use 0x5D
```

**Display Initialization**:
```c
// Panel configuration differs between variants
// Wrong config = black screen or corrupted display
```

---

## 📝 Notes for Repository Maintainers

### Files Included in This Repository

Due to size constraints, this repository includes only:
- ✅ Critical datasheets (PDF)
- ✅ Key schematics (PDF)
- ✅ Hardware pinout references
- ✅ Configuration examples

### Files NOT Included (Download from vendor)

- ❌ Full demo code (use our BSP instead)
- ❌ Windows flash tools (use `idf.py` instead)
- ❌ GUI configuration utilities
- ❌ Large CAD files

### Updating Vendor Documentation

If Guition releases updated documentation:

1. Download new package from vendor portal
2. Extract and verify contents
3. Compare with existing docs in `docs/vendor_docs/`
4. Update only changed/new files
5. Update this README with version information

---

## 🔗 Related Resources

### Official Guition Links
- **Vendor Portal**: https://pan.jczn1688.com/1/HMI%20display
- **Manufacturer**: Jingcai Technology (JCZN)

### ESP-IDF Resources
- **ESP32-P4 Documentation**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/
- **LVGL Integration**: https://docs.lvgl.io/master/integration/framework/espressif.html
- **ESP-Hosted**: https://github.com/espressif/esp-hosted

### This Project
- **Main Repository**: https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo
- **Issue Tracker**: Report problems with vendor documentation
- **Discussions**: Ask questions about hardware integration

---

## ❓ FAQ

### Q: Can I use documentation from a different board variant?
**A**: ❌ No. Even small suffix differences indicate different hardware configurations. Always use documentation for `JC1060P470C_I_W_Y` specifically.

### Q: The vendor portal link is broken. Where else can I get the files?
**A**: Contact Guition support directly. Do not use third-party mirrors or outdated files from random sources.

### Q: Do I need the vendor package if I'm using this repository?
**A**: For basic development, no. This repository includes all necessary driver code. However, the vendor package is useful for:
- Hardware debugging (schematics)
- Component-level troubleshooting (datasheets)
- Understanding reference implementations
- Board bring-up from scratch

### Q: Can I redistribute the vendor documentation?
**A**: Check Guition's license terms. Generally, redistribution of manufacturer documentation requires permission. This repository includes only essential technical references under fair use.

### Q: The package I downloaded has different contents. What do I do?
**A**: Verify you downloaded `JC1060P470C_I_W_Y.zip` (230.82 MB, dated 2026-01-23). If contents differ significantly from the structure above, contact Guition support to confirm you have the correct package version.

---

**Last Updated**: 2026-03-06  
**Package Version**: JC1060P470C_I_W_Y (2026-01-23)  
**Maintained By**: Cristiano Gorla
