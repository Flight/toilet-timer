/**
 * @file graphics.c
 * @brief Graphics drawing functions for framebuffer
 */

#include "graphics.h"
#include "font.h"
#include "utf8.h"
#include "config.h"

static struct {
    int width;
    int height;
    int bytes_per_row;
} s_graphics = {0};

void graphics_init(int width, int height)
{
    s_graphics.width = width;
    s_graphics.height = height;
    s_graphics.bytes_per_row = width / 8;
}

static const uint8_t *get_glyph(uint32_t codepoint)
{
    if (codepoint >= 32 && codepoint <= 122) {
        return font_5x7[codepoint - 32];
    }

    for (size_t i = 0; i < font_ua_5x7_count; i++) {
        if (font_ua_5x7[i].codepoint == codepoint) {
            return font_ua_5x7[i].glyph;
        }
    }

    return NULL;
}

static void draw_glyph(uint8_t *framebuffer, int x, int y, const uint8_t *glyph, uint8_t color)
{
    for (int col = 0; col < FONT_CHAR_WIDTH; col++) {
        uint8_t column = glyph[col];
        for (int row = 0; row < FONT_CHAR_HEIGHT; row++) {
            if (column & (1 << row)) {
                draw_pixel(framebuffer, x + col, y + row, color);
            }
        }
    }
}

void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color)
{
    if (x < 0 || x >= s_graphics.width || y < 0 || y >= s_graphics.height) {
        return;
    }

    int byte_index = (y * s_graphics.bytes_per_row) + (x / 8);
    int bit_index = 7 - (x % 8);

    if (color) {
        framebuffer[byte_index] |= (1 << bit_index);
    } else {
        framebuffer[byte_index] &= ~(1 << bit_index);
    }
}

void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color)
{
    int cursor_x = x;
    const char *cursor = str;

    while (*cursor) {
        uint32_t codepoint = utf8_next_codepoint(&cursor);
        const uint8_t *glyph = get_glyph(codepoint);
        if (glyph != NULL) {
            draw_glyph(framebuffer, cursor_x, y, glyph, color);
        }
        cursor_x += FONT_CHAR_SPACING;
    }
}

int measure_string_width(const char *str)
{
    size_t char_count = utf8_strlen(str);
    return (int)(char_count * FONT_CHAR_SPACING);
}
