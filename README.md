| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Toilet Timer

A battery-powered cat litter box change tracker built on the LilyGo Mini E-Paper S3.

![Photo of usage](example.jpeg)

## Why?

![Paper and pen](before.jpeg)

We completely change the cat's litter box about once a week (with regular cleaning in between). The problem was we could never remember when the last change was. We used to write the date on toilet paper with a pen, but then you'd have to check your phone to figure out how many days ago that was.

This device shows the last change date and updates at midnight to display how many days have passed ("Today", "Yesterday", "2 days ago", etc.). Press the button to reset the date to today and the counter back to "Today".

There's no power outlet in the bathroom, so it needed to be battery-powered. Hence the e-ink display + deep sleep combination.

## Features

- E-paper display (80x128 resolution) - retains image with zero power
- Deep sleep mode for long battery life
- Single button (GPIO4) to mark litter change and reset the counter
- Wi-Fi connects only on GPIO4 or GPIO3 button press — never on timer or power-on wake-up
- SNTP time sync and OTA firmware updates triggered by Wi-Fi connection
- Daily 1:00 AM wake-up to refresh the display (no Wi-Fi, no sync)
- Shows days elapsed since last change in Ukrainian

## Hardware Required

* [LilyGo Mini E-Paper S3](https://github.com/Xinyuan-LilyGO/LilyGO-Mini-Epaper) development board
  - ESP32-S3 microcontroller
  - 1.02" e-paper display (80x128 pixels)
  - Built-in USB-C for programming and power
* USB-C cable

## Project Structure

```
toilet-timer/
├── main/
│   ├── main.c                  # Main application logic
│   ├── global_constants.h      # Global configuration constants
│   ├── global_event_group.h    # FreeRTOS event group definitions
│   ├── battery_level/          # Battery voltage monitoring
│   ├── deep_sleep/             # Deep sleep management
│   ├── display_epaper/         # E-paper display driver and graphics
│   │   ├── driver/             # Low-level GDEW0102T4 driver
│   │   └── fonts/              # Bitmap fonts
│   ├── nvs_utils/              # Non-volatile storage utilities
│   ├── ota_update/             # Over-the-air firmware updates
│   ├── show_messages/          # Display message formatting
│   ├── system_state/           # System state management
│   ├── time_utils/             # Time and date utilities
│   ├── trigger/                # Button trigger handling
│   └── wifi/                   # Wi-Fi connection management
├── local_ota_server/           # Local OTA update server files
├── README.md
└── CMakeLists.txt              # Project build configuration
```

## Setup and Build

### 1. Install ESP-IDF

Follow the [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) to install the ESP-IDF development framework.

### 2. Set the Target Chip

```bash
idf.py set-target esp32s3
```

### 3. Build the Project

```bash
idf.py build
```

### 4. Flash and Monitor

Connect your LilyGo Mini E-Paper S3 board via USB-C and run:

```bash
idf.py flash monitor
```

To exit the serial monitor, press `Ctrl+]`.

## Pin Configuration (LilyGo Mini E-Paper S3)

| Function        | GPIO | Description |
|-----------------|------|-------------|
| MOSI            | 15   | SPI data out |
| CLK             | 14   | SPI clock |
| CS              | 13   | Chip select |
| DC              | 12   | Data/Command |
| RST             | 11   | Reset |
| BUSY            | 10   | Busy status |
| POWER           | 42   | Power enable (needed while powering from the battery) |
| TIMER RESET BTN | 4    | Wake-up + reset timer to today (active LOW) |
| SYNC/UPDATE BTN | 3    | Wake-up + trigger Wi-Fi sync and OTA check without resetting timer (active LOW) |

## Local OTA Update

1. Generate a certificate for the OTA Updates. Please **don't forget to fill the server name** on this step:

   **Common Name (e.g. server FQDN or YOUR name) []: 192.168.50.111** (your local IP address).

   `openssl req -newkey rsa:2048 -new -nodes -x509 -days 365 -keyout local_ota_server/key.pem -out main/ota_update/cert.pem -subj "/CN=192.168.50.111"`

2. Create new GIT Tag

   `git tag -a v0.0.1 -m "new release"`

   `git push origin v0.0.1` (optional)

3. Run OTA web-server from `local_ota_server` folder. I'm using this [http-server](https://github.com/http-party/http-server) as **OpenSSL one didn't work properly** and was hanging during download until you don't stop it manually.

   `cd local_ota_server`

   `sudo npx http-server -S -C ../main/ota_update/cert.pem -p 8070 -c-1`

4. Build the project. If the build is successful, the `dongle.bin` file will appear in the `local_ota_server` folder.

## Manually Correcting the Last-Change Date

If you accidentally press the reset button, use the script in [set_manual_timestamp/](set_manual_timestamp/) to write a specific timestamp directly into the device's NVS (non-volatile storage) without flashing new firmware.

```bash
cd set_manual_timestamp

# Preview — generates nvs_timestamp.bin without touching the device
python3 set_timestamp.py "2026-03-13 11:00"

# Generate + flash in one step
python3 set_timestamp.py "2026-03-13 11:00" --flash /dev/cu.usbmodem*

# Set to right now
python3 set_timestamp.py now --flash /dev/cu.usbmodem*
```

The datetime is interpreted in your **local timezone**, which should match `CONFIG_SNTP_TIMEZONE` on the device.

Accepted formats: `YYYY-MM-DD HH:MM`, `YYYY-MM-DD HH:MM:SS`, or `now`.

> The script preserves the "time already synced" flag so the device won't show the *Connecting Wi-Fi…* screen on next boot. The OTA firmware hash is not preserved, but the firmware re-validates it harmlessly on the next button press.

## Resources

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [LilyGo GitHub](https://github.com/Xinyuan-LilyGO/LilyGO-Mini-Epaper)
- [Proper PIN map from Smoria](https://github.com/Xinyuan-LilyGO/LilyGO-Mini-Epaper/issues/7)