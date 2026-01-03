/**
 * @file graphics.h
 * @brief Graphics drawing functions for framebuffer
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color);
void draw_char(uint8_t *framebuffer, int x, int y, char c, uint8_t color);
void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
