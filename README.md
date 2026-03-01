| Supported Hosts | ESP32-P4 | ESP32-S3 |
| --------------- | -------- | -------- |

# SD Card (SDMMC) with ESP-Hosted example

> [!IMPORTANT]
> The ESP-IDF `master` branch has an issue that prevents the SD card and ESP-Hosted from working together when both are using SDMMC. See [ESP-IDF Issue 16233](https://github.com/espressif/esp-idf/issues/16233) for more information. This code contains a workaround for this issue.

This example demonstrates how to use an SD card on an ESP32-P4 dev board when ESP-Hosted is using SDIO to communicate with the on-board ESP32-C6. It has been modified from the standard [ESP-IDF SD Card example](https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card/sdmmc).

This example does the following steps:

1. Initialise ESP-Hosted and Wi-Fi
1. Use an "all-in-one" `esp_vfs_fat_sdmmc_mount` function to:
   - initialize SDMMC peripheral,
   - probe and initialize an SD card,
   - mount FAT filesystem using FATFS library
   - register FAT filesystem in VFS, enabling C standard library and POSIX functions to be used.
1. Print information about the card, such as name, type, capacity, and maximum supported frequency.
1. Perform a Wi-Fi Scan (or Connect if enabled)
1. Create a file using `fopen` and write to it using `fprintf`.
1. Rename the file. Before renaming, check if destination file already exists using `stat` function, and remove it using `unlink` function.
1. Open renamed file for reading, read back the line, and print it to the terminal.

This example supports SD (SDSC, SDHC, SDXC) cards and eMMC chips.

## Hardware

This example requires an ESP32-P4 development board with an SD card slot and an SD card. On this board, the SD card slot is assigned to SDMMC Slot 0, while the on-board ESP32-C6 is connected to the ESP32-P4 on SDMMC Slot 1.

Although it is possible to connect an SD card breakout adapter, keep in mind that connections using breakout cables are often unreliable and have poor signal integrity. You may need to use lower clock frequency when working with SD card breakout adapters.

This example doesn't utilize card detect (CD) and write protect (WP) signals from SD card slot.

### Pin assignments for SDMMC Slot 0 on ESP32-P4

On ESP32-P4, SDMMC Slot 0 GPIO pins cannot be customized. The GPIO assigned in the example should not be modified.

The table below lists the default pin assignments.

| ESP32-P4 pin | SD card pin | Notes                                                                |
| ------------ | ----------- | -------------------------------------------------------------------- |
| GPIO43       | CLK         | 10k pullup                                                           |
| GPIO44       | CMD         | 10k pullup                                                           |
| GPIO39       | D0          | 10k pullup                                                           |
| GPIO40       | D1          | not used in 1-line SD mode; 10k pullup in 4-line mode                |
| GPIO41       | D2          | not used in 1-line SD mode; 10k pullup in 4-line mode                |
| GPIO42       | D3          | not used in 1-line SD mode, but card's D3 pin must have a 10k pullup |

### ESP-Hosted Control Pins (ESP32-C6 ↔ ESP32-P4)

| Function          | C6 Pin      | P4 Pin  | Description               |
| ----------------- | ----------- | ------- | ------------------------- |
| Interrupt/OOB     | GPIO 2      | GPIO 6  | Data ready signal         |
| Reset             | CHIP_PU     | GPIO 54 | Hardware reset            |
| Debug (unused)    | GPIO 9      | JP1-18  | External header only      |

**Note:** GPIO6 interrupt is configured in `sdkconfig.defaults` for efficient ESP-Hosted communication.

### 4-line and 1-line SD modes

By default, this example uses 4 line SD mode, utilizing 6 pins: CLK, CMD, D0 - D3. It is possible to use 1-line mode (CLK, CMD, D0) by changing "SD/MMC bus width" in the example configuration menu (see `CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_1`).

Note that even if card's D3 line is not connected to the ESP chip, it still has to be pulled up, otherwise the card will go into SPI protocol mode.

## WiFi Features

### WiFi Scan (Default)

By default, the example performs a WiFi scan to verify ESP-Hosted connectivity:

```c
// In main/feature_flags.h
#define ENABLE_WIFI 1         // ✅ Enable WiFi/ESP-Hosted
#define ENABLE_WIFI_CONNECT 0 // ❌ Scan only (default)
```

### WiFi Connection Test (Advanced)

To test WiFi connection instead of just scanning:

1. **Create WiFi credentials file:**
   ```bash
   cd main
   cp wifi_config.h.example wifi_config.h
   ```

2. **Edit credentials:**
   ```c
   // In main/wifi_config.h
   #define WIFI_SSID "YourWiFiSSID"
   #define WIFI_PASSWORD "YourWiFiPassword"
   ```

3. **Enable connection test:**
   ```c
   // In main/feature_flags.h
   #define ENABLE_WIFI_CONNECT 1 // ✅ Enable connection test
   ```

4. **Build and flash:**
   ```bash
   idf.py build flash monitor
   ```

**Expected Output (WiFi Connected):**
```
I (4429) GUITION_MAIN: ✓ WiFi initialized (ESP-Hosted via C6)
I (6429) GUITION_MAIN: === WiFi Connection Test ===
I (6429) GUITION_MAIN: Connecting to: YourWiFiSSID
I (8500) GUITION_MAIN: ✓ WiFi connected!
I (8500) GUITION_MAIN:    IP Address: 192.168.1.123
I (8501) GUITION_MAIN:    Netmask:    255.255.255.0
I (8502) GUITION_MAIN:    Gateway:    192.168.1.1
I (8503) GUITION_MAIN:    RSSI: -45 dBm
```

> [!NOTE]
> The `wifi_config.h` file is gitignored for security. Never commit your WiFi credentials to the repository.

## RTC NTP Synchronization (Advanced)

### RTC NTP Sync Test

Test RTC synchronization with NTP server (requires WiFi connection):

1. **Enable WiFi connection** (see WiFi Connection Test above)

2. **Enable RTC NTP sync:**
   ```c
   // In main/feature_flags.h
   #define ENABLE_RTC 1            // ✅ Enable RTC
   #define ENABLE_RTC_NTP_SYNC 1   // ✅ Enable NTP sync test
   #define ENABLE_WIFI_CONNECT 1   // ✅ Required for NTP
   ```

3. **Build and flash:**
   ```bash
   idf.py build flash monitor
   ```

**Test Workflow:**

1. **Step 1/4**: Read current RTC time
2. **Step 2/4**: Reset RTC to default (2000-01-01 00:00:00)
3. **Step 3/4**: Synchronize with NTP server (pool.ntp.org)
4. **Step 4/4**: Update RTC with NTP time

**Expected Output:**
```
I (7868) GUITION_MAIN: ✓ WiFi connected!
I (7868) GUITION_MAIN:    IP Address: 192.168.188.88

I (7869) RTC_NTP: ========================================
I (7870) RTC_NTP:    RTC NTP Sync Test
I (7871) RTC_NTP: ========================================

I (7872) RTC_NTP: Step 1/4: Read current RTC time
I (7873) RTC_NTP: Current RTC: 2026-03-01 19:52:55

I (7874) RTC_NTP: Step 2/4: Reset RTC to default time
I (7875) RTC_NTP: Resetting RTC to default time (2000-01-01 00:00:00)...
I (7876) RTC_NTP: ✓ RTC reset to: 2000-01-01 00:00:00
I (7877) RTC_NTP: RTC after reset: 2000-01-01 00:00:00

I (7878) RTC_NTP: Step 3/4: Synchronize with NTP server
I (7879) RTC_NTP: Starting NTP time synchronization...
I (7880) RTC_NTP: NTP Server: pool.ntp.org
I (7881) RTC_NTP: Timezone: CET (UTC+1, DST auto)
I (7882) RTC_NTP: Waiting for NTP sync (timeout: 10 seconds)...
I (8456) RTC_NTP: NTP time synchronized!
I (8456) RTC_NTP: ✓ NTP sync successful!
I (8457) RTC_NTP: Current time: 2026-03-01 19:52:56 CET

I (8458) RTC_NTP: Step 4/4: Update RTC with NTP time
I (8459) RTC_NTP: Updating RTC with system time...
I (8460) RTC_NTP: System time: 2026-03-01 19:52:56 (wday=6)
I (8461) RTC_NTP: ✓ RTC updated successfully
I (8462) RTC_NTP: RTC readback: 2026-03-01 19:52:56

I (8463) RTC_NTP: ========================================
I (8464) RTC_NTP:    RTC NTP Sync Test Complete
I (8465) RTC_NTP: ========================================
```

**Features:**
- **NTP Server**: pool.ntp.org (public NTP server pool)
- **Timezone**: CET (UTC+1) with automatic DST adjustment
- **Timeout**: 10 seconds for NTP synchronization
- **Verification**: Reads back RTC time after update to confirm success

**Use Cases:**
- Initial RTC setup on first boot
- Periodic time synchronization when WiFi is available
- Recovery from RTC power loss (PON/VLF flags set)
- Development/testing time synchronization

## How to use example

### Build and flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(Replace PORT with serial port name.)

(To exit the serial monitor, type `Ctrl-]`.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Example output

A Wi-Fi Scan is performed before and after accessing the SD Card. This shows that both SDMMC Slots work as expected.

```
I (2103) transport: Received INIT event from ESP32 peripheral
I (2103) transport: EVENT: 12
I (2113) transport: Identified slave [esp32c6]
I (2113) transport: EVENT: 11
I (2113) transport: capabilities: 0xd
I (2123) transport: Features supported are:
I (2123) transport:      * WLAN
I (2123) transport:        - HCI over SDIO
I (2133) transport:        - BLE only
I (2133) transport: EVENT: 13
I (2133) transport: ESP board type is : 13

I (2143) transport: Base transport is set-up, TRANSPORT_TX_ACTIVE
I (2143) H_API: Transport active
I (2143) transport: Slave chip Id[12]
I (2153) transport: raw_tp_dir[-], flow_ctrl: low[60] high[80]
I (2153) transport: transport_delayed_init
I (2163) esp_cli: Remove any existing deep_sleep cmd in cli
I (2163) esp_cli: Registering command: crash
I (2173) esp_cli: Registering command: reboot
I (2173) esp_cli: Registering command: mem-dump
I (2173) esp_cli: Registering command: task-dump
I (2183) esp_cli: Registering command: cpu-dump
I (2183) esp_cli: Registering command: heap-trace
I (2193) esp_cli: Registering command: sock-dump
I (2193) esp_cli: Registering command: host-power-save
I (2203) hci_stub_drv: Host BT Support: Disabled
I (2203) H_SDIO_DRV: Received INIT event
I (2203) H_SDIO_DRV: Event type: 0x22
I (2213) H_SDIO_DRV: Write thread started
I (2643) example: Initializing SD card
I (2643) example: Using SDMMC peripheral
W (2643) ldo: The voltage value 0 is out of the recommended range [500, 2700]
I (2643) RPC_WRAP: ESP Event: wifi station started
I (2653) RPC_WRAP: ESP Event: wifi station started
I (2743) example: Mounting filesystem
I (2743) sdmmc_periph: sdmmc_host_init: SDMMC host already initialized, skipping init flow
I (2953) example: Filesystem mounted
I (2953) example: Doing Wi-Fi Scan
I (3653) rpc_req: Scan start Req

W (3653) rpc_rsp: Hosted RPC_Resp [0x21a], uid [4], resp code [12298]
I (6183) RPC_WRAP: ESP Event: StaScanDone
I (6203) example: Total APs scanned = 11, actual AP number ap_info holds = 10
Name: SD16G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 14868MB
CSD: ver=2, sector_size=512, capacity=30449664 read_bl_len=9
SSR: bus_width=4
I (6213) example: Opening file /sdcard/hello.txt
I (6303) example: File written
I (6323) example: Renaming file /sdcard/hello.txt to /sdcard/foo.txt
I (6333) example: Reading file /sdcard/foo.txt
I (6333) example: Read from file: 'Hello SD16G!'
I (6333) example: Opening file /sdcard/nihao.txt
I (6353) example: File written
I (6353) example: Reading file /sdcard/nihao.txt
I (6363) example: Read from file: 'Nihao SD16G!'
I (6363) example: Card unmounted
I (6363) example: Doing another Wi-Fi Scan
I (6363) rpc_req: Scan start Req

I (8863) RPC_WRAP: ESP Event: StaScanDone
I (8883) example: Total APs scanned = 11, actual AP number ap_info holds = 10
I (8883) main_task: Returned from app_main()

Done
```

## Troubleshooting

### Card fails to initialize with `sdmmc_init_sd_scr: send_scr (1) returned 0x107` error

Check connections between the card and the ESP32. For example, if you have disconnected GPIO2 to work around the flashing issue, connect it back and reset the ESP32 (using a button on the development board, or by pressing Ctrl-T Ctrl-R in IDF Monitor).

### Card fails to initialize with `sdmmc_check_scr: send_scr returned 0xffffffff` error

Connections between the card and the ESP32 are too long for the frequency used. Try using shorter connections, or try reducing the clock speed of SD interface.

### Failure to mount filesystem

```
example: Failed to mount filesystem.
```

The example will be able to mount only cards formatted using FAT32 filesystem.

### WiFi connection timeout

If WiFi connection times out:
1. Verify credentials in `main/wifi_config.h`
2. Check WiFi signal strength (RSSI)
3. Ensure router is 2.4GHz compatible (ESP32-C6 doesn't support 5GHz)
4. Check `troubleshooting.md` for GPIO6 interrupt configuration

### NTP sync timeout

If NTP synchronization fails:
1. Verify WiFi is connected (check IP address)
2. Check internet connectivity
3. Firewall may block NTP (UDP port 123)
4. Try alternative NTP server (edit `rtc_ntp_sync.c`)
5. Increase timeout in `sync_time_from_ntp()` call
