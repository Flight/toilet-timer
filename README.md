| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Toilet Timer

An ESP32-S3 based timer application for the LilyGo Mini E-Paper S3 development board featuring a 1.02" 128x80 monochrome e-paper display.

## Features

- E-paper display support (128x80 resolution)
- LVGL graphics library integration for UI rendering
- Modular e-paper driver architecture
- Low power consumption with e-paper display retention
- Hello World demo application

## Hardware Required

* [LilyGo Mini E-Paper S3](https://www.lilygo.cc/) development board
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

### Adding Custom UI

The application uses [LVGL](https://lvgl.io/) for UI rendering. You can create custom screens by modifying [main/main.c](main/main.c):

```c
// Create UI elements
lv_obj_t *label = lv_label_create(lv_scr_act());
lv_label_set_text(label, "Your Text");
lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

// Render and display
lv_task_handler();
epd_display_buffer(framebuffer, buffer_size);
```

## Dependencies

This project uses the following ESP-IDF components:

- [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) - LVGL port for ESP32
- [lvgl/lvgl](https://components.espressif.com/components/lvgl/lvgl) - Graphics library
- [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) - LED control library

## Resources

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [LVGL Documentation](https://docs.lvgl.io/)
- [LilyGo GitHub](https://github.com/Xinyuan-LilyGO)
- [E-Paper Display Specifications](https://www.good-display.com/)
