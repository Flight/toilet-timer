/**
 * @file graphics.c
 * @brief Graphics drawing functions for framebuffer
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "graphics.h"
#include "font.h"

void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color)
{
    if (x < 0 || x >= 80 || y < 0 || y >= 128) {
        return;
    }

    int byte_index = (y * 10) + (x / 8);
    int bit_index = 7 - (x % 8);

    if (color) {
        framebuffer[byte_index] |= (1 << bit_index);
    } else {
        framebuffer[byte_index] &= ~(1 << bit_index);
    }
}

void draw_char(uint8_t *framebuffer, int x, int y, char c, uint8_t color)
{
    if (c < 32 || c > 122) {
        return;
    }

    const uint8_t *glyph = font_5x7[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t column = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (column & (1 << row)) {
                draw_pixel(framebuffer, x + col, y + row, color);
            }
        }
    }
}

void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color)
{
    int cursor_x = x;
    while (*str) {
        draw_char(framebuffer, cursor_x, y, *str, color);
        cursor_x += 6;
        str++;
    }
}
