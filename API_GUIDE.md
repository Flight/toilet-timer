# API Quick Reference Guide

## Display Module (`display.h`)

High-level display interface with automatic framebuffer management.

### Initialization
```c
esp_err_t display_init(void);
void display_deinit(void);
```

### Drawing Functions
```c
// Clear framebuffer to white
void display_clear(void);

// Draw text at position
void display_draw_text(int x, int y, const char *text, uint8_t color);

// Draw centered text
void display_draw_text_centered(int y, const char *text, uint8_t color);

// Measure text width
int display_measure_text(const char *text);
```

### Display Control
```c
// Update physical display
esp_err_t display_update(void);

// Enter deep sleep
esp_err_t display_sleep(void);

// Get dimensions
int display_get_width(void);   // Returns 80
int display_get_height(void);  // Returns 128
```

### Example Usage
```c
#include "display.h"
#include "config.h"

void app_main(void) {
    // Initialize
    display_init();

    // Draw text
    display_clear();
    display_draw_text_centered(60, "Hello World!", 0);

    // Update and sleep
    display_update();
    display_sleep();
}
```

## Graphics Module (`graphics.h`)

Low-level drawing primitives (typically not used directly by applications).

### Initialization
```c
void graphics_init(int width, int height);
```

### Drawing Functions
```c
void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color);
void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color);
int measure_string_width(const char *str);
```

## UTF-8 Module (`utf8.h`)

UTF-8 string utilities (used internally by graphics module).

```c
// Decode next codepoint
uint32_t utf8_next_codepoint(const char **str);

// Count characters (not bytes)
size_t utf8_strlen(const char *str);
```

### Example
```c
const char *text = "Привіт";
size_t chars = utf8_strlen(text);  // Returns 6 (not 12)

const char *p = text;
while (*p) {
    uint32_t codepoint = utf8_next_codepoint(&p);
    // Process codepoint...
}
```

## Configuration (`config.h`)

Hardware and display constants.

```c
// Display dimensions
#define DISPLAY_WIDTH   80
#define DISPLAY_HEIGHT  128

// Font metrics
#define FONT_CHAR_WIDTH     5
#define FONT_CHAR_HEIGHT    7
#define FONT_CHAR_SPACING   6

// Hardware pins
#define EPD_PIN_MOSI    15
#define EPD_PIN_CLK     14
#define EPD_PIN_CS      13
// ... etc
```

## Color Values

- `0` = White (background)
- `1` = Black (foreground)

## Coordinate System

```
    (0,0) ──────────► X (0-79)
      │
      │
      │
      ▼
      Y
    (0-127)
```

## Common Patterns

### Centered Text
```c
int y = (DISPLAY_HEIGHT - FONT_CHAR_HEIGHT) / 2;
display_draw_text_centered(y, "Centered Text", 0);
```

### Multi-line Text
```c
display_draw_text_centered(40, "Line 1", 0);
display_draw_text_centered(50, "Line 2", 0);
display_draw_text_centered(60, "Line 3", 0);
```

### Right-aligned Text
```c
const char *text = "Right";
int width = display_measure_text(text);
int x = DISPLAY_WIDTH - width;
display_draw_text(x, 60, text, 0);
```

### Full Screen Message
```c
display_init();
display_clear();

const char *title = "Title";
const char *message = "Message text here";

int title_y = 40;
int message_y = 60;

display_draw_text_centered(title_y, title, 0);
display_draw_text_centered(message_y, message, 0);

display_update();
display_sleep();
```
