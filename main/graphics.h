/**
 * @file graphics.h
 * @brief Graphics drawing functions for framebuffer
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

/**
 * @brief Initialize graphics context with display dimensions
 *
 * @param width Display width in pixels
 * @param height Display height in pixels
 */
void graphics_init(int width, int height);

/**
 * @brief Draw a pixel on the framebuffer
 *
 * @param framebuffer Pointer to framebuffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param color 0=white, 1=black
 */
void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color);

/**
 * @brief Draw a UTF-8 string on the framebuffer
 *
 * @param framebuffer Pointer to framebuffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param str UTF-8 encoded string
 * @param color 0=white, 1=black
 */
void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color);

/**
 * @brief Measure the width of a UTF-8 string
 *
 * @param str UTF-8 encoded string
 * @return Width in pixels
 */
int measure_string_width(const char *str);

#endif /* GRAPHICS_H */
