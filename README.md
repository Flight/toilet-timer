| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Toilet Timer

An ESP32-S3 based timer project with LED control using the [led_strip](https://components.espressif.com/component/espressif/led_strip) component for addressable LEDs like [WS2812](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf).

## Hardware Required

* ESP32-S3 development board (e.g., ESP32-S3-DevKitC)
* Addressable LED strip (WS2812 or compatible)
* USB cable for power and programming

## Setup and Build

### 1. Set the Target Chip

```bash
idf.py set-target esp32s3
```

### 2. Configure the Project

Open the project configuration menu:

```bash
idf.py menuconfig
```

In the `Example Configuration` menu:
* Set LED type to `LED strip`
* Select `RMT` as the backend peripheral
* Configure the GPIO pin for your LED strip
* Set the blinking period in milliseconds

### 3. Build and Flash

Build, flash, and monitor the project:

```bash
idf.py flash monitor
```

To exit the serial monitor, press `Ctrl+]`.

## Development

The main application code is in [main/main.c](main/main.c). You can modify the LED behavior by:
- Changing colors: `led_strip_set_pixel(led_strip, 0, R, G, B)` (values 0-255)
- Adjusting timing: Modify the delay values
- Adding timer logic: Implement your custom timer functionality

## Resources

- [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html)
- [LED Strip Component Documentation](https://components.espressif.com/component/espressif/led_strip)
