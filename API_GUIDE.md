# API Quick Reference Guide

## Display Module (`main/display_epaper/display.h`)

High-level display interface with automatic framebuffer management.
Coordinates are logical pixels after `CONFIG_DISPLAY_ROTATION`; (0,0) is the
top-left corner as seen by the user. Color `0` is black, `1` is white.

### Initialization
```c
void display_enable_power_early(void);  // call first thing in app_main()
esp_err_t display_init(void);
void display_deinit(void);
```

### Drawing Functions
```c
// Clear framebuffer to white
void display_clear(void);

// Draw text at position, with an integer zoom factor (1 = native 10x20 font)
void display_draw_text(int x, int y, const char *text, uint8_t color, int scale);

// Draw text with every line centered horizontally
void display_draw_text_centered(int y, const char *text, uint8_t color, int scale);

// Fill a rectangle
void display_fill_rect(int x, int y, int width, int height, uint8_t color);

// Measure text
int display_measure_text(const char *text, int scale);   // widest line, pixels
int display_text_height(const char *text, int scale);    // all lines, pixels
```

### Display Control
```c
esp_err_t display_update(void);  // full refresh (~2 s)
esp_err_t display_sleep(void);   // controller deep sleep + panel supply off
esp_err_t display_wake(void);    // supply on + re-init

int display_get_width(void);     // logical width (200)
int display_get_height(void);    // logical height (200)
```

### Example Usage
```c
#include "display_epaper/display.h"

void app_main(void) {
    display_enable_power_early();
    display_init();

    display_clear();
    display_draw_text_centered(40, "12", 0, 4);          // big number
    display_draw_text_centered(130, "днів тому", 0, 1);  // caption

    display_update();
    display_sleep();
}
```

## Graphics Module (`main/display_epaper/graphics.h`)

Low-level drawing primitives on a raw framebuffer (used by the display module).

```c
void graphics_init(int width, int height);
int graphics_get_width(void);
int graphics_get_height(void);
void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color);
void fill_rect(uint8_t *framebuffer, int x, int y, int width, int height, uint8_t color);
void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color, int scale);
void draw_string_centered(uint8_t *framebuffer, int y, const char *str, uint8_t color, int scale);
int measure_string_width(const char *str, int scale);
int measure_string_height(const char *str, int scale);
```

## UTF-8 Module (`main/display_epaper/utf8.h`)

```c
uint32_t utf8_next_codepoint(const char **str);  // decode next codepoint, advance pointer
size_t utf8_strlen(const char *str);             // count characters (not bytes)
```

## Font (`main/display_epaper/fonts/font_10x20.h`)

10x20 pixel glyphs (X11 Misc-Fixed, public domain): ASCII 32-126, the degree
sign and the Ukrainian alphabet. Each glyph is 20 rows of `uint16_t`, bit 15 is
the left-most pixel. Regenerate with `tools/bdf2font.py`.

## Font metrics (`main/global_constants.h`)

```c
#define FONT_CHAR_WIDTH    10
#define FONT_CHAR_HEIGHT   20
#define FONT_CHAR_SPACING  11   // character advance
#define FONT_LINE_SPACING  2    // extra pixels between lines
```

## Board Modules

```c
// main/board_power/board_power.h
void board_power_init_early(void);             // battery latch driven HIGH, audio on, holds released
void board_power_latch_apply_policy(void);     // after NVS init: switch to the weak latch if this board can
bool board_power_latch_is_weak(void);
void board_power_prepare_for_deep_sleep(void); // e-paper and audio off, latch held through deep sleep

// main/deep_sleep/deep_sleep.h
esp_err_t deep_sleep_configure_wakeup(void);   // keys + RTC alarm, timer fallback 2 h after the alarm
void deep_sleep_enter(void);
void deep_sleep_fallback(const char *reason);  // sleep instead of staying awake after a failure
void deep_sleep_start_awake_watchdog(void);    // force deep sleep after CONFIG_MAX_AWAKE_SECONDS

// main/rtc_pcf85063/rtc_pcf85063.h
esp_err_t rtc_pcf85063_init(void);
esp_err_t rtc_pcf85063_restore_system_time(void);
esp_err_t rtc_pcf85063_save_system_time(void);
esp_err_t rtc_pcf85063_arm_daily_alarm(int local_hour, int local_minute, time_t *armed_at);

// main/shtc3/shtc3.h
esp_err_t shtc3_read(shtc3_reading_t *reading);   // temperature_x10, humidity

// main/battery_level/battery_level.h
int battery_level_read_percent(int *millivolts);  // 0-100, or -1
int battery_level_last_mv(void);                  // last reading of this wake-up, or -1

// main/battery_log/battery_log.h
void battery_log_record(int millivolts, uint8_t flags);  // append this wake-up to the NVS history and print it

// main/sntp/sntp.h
bool sntp_is_sync_due(void);                      // Wi-Fi sync policy
```
