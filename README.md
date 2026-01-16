| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Toilet Timer

An ESP32-S3 based timer application for the LilyGo Mini E-Paper S3 development board featuring a 1.02" 128x80 monochrome e-paper display.

## Features

- E-paper display support (128x80 resolution)
- Modular e-paper driver architecture
- Low power consumption with e-paper display retention

## Hardware Required

* [LilyGo Mini E-Paper S3](https://github.com/Xinyuan-LilyGO/LilyGO-Mini-Epaper) development board
  - ESP32-S3 microcontroller
  - 1.02" e-paper display (128x80 pixels)
  - Built-in USB-C for programming and power
* USB-C cable

## Project Structure

```
toilet-timer/
├── main/
│   ├── main.c           # Main application logic
│   ├── epd_driver.c     # E-paper display driver implementation
│   ├── epd_driver.h     # E-paper display driver header
│   └── CMakeLists.txt   # Component build configuration
├── README.md
└── CMakeLists.txt       # Project build configuration
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

## Development

### E-Paper Display Driver

The e-paper display driver is implemented in [main/epd_driver.c](main/epd_driver.c) and provides a clean API:

```c
// Initialize display
epd_config_t config = {
    .pin_mosi = 35,
    .pin_clk = 36,
    .pin_cs = 37,
    .pin_dc = 34,
    .pin_rst = 38,
    .pin_busy = 39,
    .width = 128,
    .height = 80,
};
epd_init(&config);

// Display a buffer
uint8_t framebuffer[128 * 80 / 8];
epd_display_buffer(framebuffer, sizeof(framebuffer));

// Clear display to white
epd_clear();
```

### Pin Configuration (LilyGo Mini E-Paper S3)

| Function | GPIO | Description |
|----------|------|-------------|
| MOSI     | 35   | SPI data out |
| CLK      | 36   | SPI clock |
| CS       | 37   | Chip select |
| DC       | 34   | Data/Command |
| RST      | 38   | Reset |
| BUSY     | 39   | Busy status |

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

## Resources

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [LilyGo GitHub](https://github.com/Xinyuan-LilyGO/LilyGO-Mini-Epaper)
- [Proper PIN map from Smoria](https://github.com/Xinyuan-LilyGO/LilyGO-Mini-Epaper/issues/7)