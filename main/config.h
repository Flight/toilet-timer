/**
 * @file config.h
 * @brief Hardware and display configuration constants
 */

#ifndef CONFIG_H
#define CONFIG_H

/* E-Paper Display Hardware Pins */
#define EPD_PIN_MOSI    15
#define EPD_PIN_CLK     14
#define EPD_PIN_CS      13
#define EPD_PIN_DC      12
#define EPD_PIN_RST     11
#define EPD_PIN_BUSY    10
#define EPD_PIN_POWER   42

/* Display Dimensions */
#define DISPLAY_WIDTH   80
#define DISPLAY_HEIGHT  128

/* Font Dimensions */
#define FONT_CHAR_WIDTH     5
#define FONT_CHAR_HEIGHT    7
#define FONT_CHAR_SPACING   6  /* Width + 1 pixel spacing */

#endif /* CONFIG_H */
