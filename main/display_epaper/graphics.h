/**
 * @file graphics.h
 * @brief Graphics drawing functions for framebuffer
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

/**
 * @brief Initialize graphics context with the physical display dimensions
 *
 * The logical (rotated) dimensions follow CONFIG_DISPLAY_ROTATION.
 *
 * @param width Physical display width in pixels
 * @param height Physical display height in pixels
 */
void graphics_init(int width, int height);

/**
 * @brief Logical display width (after rotation)
 */
int graphics_get_width(void);

/**
 * @brief Logical display height (after rotation)
 */
int graphics_get_height(void);

/**
 * @brief Draw a pixel on the framebuffer (logical coordinates)
 *
 * @param framebuffer Pointer to framebuffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param color 0=black, 1=white
 */
void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color);

/**
 * @brief Fill a rectangle (logical coordinates)
 *
 * @param framebuffer Pointer to framebuffer
 * @param x Left edge
 * @param y Top edge
 * @param width Width in pixels
 * @param height Height in pixels
 * @param color 0=black, 1=white
 */
void fill_rect(uint8_t *framebuffer, int x, int y, int width, int height, uint8_t color);

/**
 * @brief Draw a UTF-8 string on the framebuffer, wrapping at the right edge
 *
 * @param framebuffer Pointer to framebuffer
 * @param x X coordinate of the top-left corner
 * @param y Y coordinate of the top-left corner
 * @param str UTF-8 encoded string ('\n' starts a new line)
 * @param color 0=black, 1=white
 * @param scale Integer zoom factor (1 = native font size)
 */
void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color, int scale);

/**
 * @brief Draw a UTF-8 string with every line centered horizontally
 *
 * @param framebuffer Pointer to framebuffer
 * @param y Y coordinate of the top of the first line
 * @param str UTF-8 encoded string ('\n' starts a new line)
 * @param color 0=black, 1=white
 * @param scale Integer zoom factor (1 = native font size)
 */
void draw_string_centered(uint8_t *framebuffer, int y, const char *str, uint8_t color, int scale);

/**
 * @brief Measure the width of the widest line of a UTF-8 string
 *
 * @param str UTF-8 encoded string
 * @param scale Integer zoom factor
 * @return Width in pixels
 */
int measure_string_width(const char *str, int scale);

/**
 * @brief Measure the total height of a (multi-line) UTF-8 string
 *
 * @param str UTF-8 encoded string
 * @param scale Integer zoom factor
 * @return Height in pixels (0 for an empty string)
 */
int measure_string_height(const char *str, int scale);

#endif /* GRAPHICS_H */
