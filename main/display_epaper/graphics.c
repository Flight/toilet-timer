/**
 * @file graphics.c
 * @brief Graphics drawing functions for framebuffer
 *
 * Coordinates use logical (rotated) display orientation:
 * - (0,0) is top-left corner of the display as viewed by user
 * - x increases to the right, y increases downward
 * - Logical dimensions: 128 wide x 80 tall (after 90-degree rotation)
 * - Physical dimensions: 80 wide x 128 tall
 */

#include "graphics.h"
#include "fonts/font_9x15.h"
#include "utf8.h"
#include "global_constants.h"

static struct {
    int phys_width;      /* Physical display width */
    int phys_height;     /* Physical display height */
    int logical_width;   /* Logical width (after rotation) = phys_height */
    int logical_height;  /* Logical height (after rotation) = phys_width */
    int bytes_per_row;
} s_graphics = {0};

void graphics_init(int width, int height)
{
    s_graphics.phys_width = width;
    s_graphics.phys_height = height;
    s_graphics.logical_width = height;   /* Rotated 90 degrees */
    s_graphics.logical_height = width;
    s_graphics.bytes_per_row = width / 8;
}

static const uint16_t *get_glyph(uint32_t codepoint)
{
    if (codepoint >= 32 && codepoint <= 126) {
        return font_9x15[codepoint - 32];
    }

    for (size_t i = 0; i < font_ua_9x15_count; i++) {
        if (font_ua_9x15[i].codepoint == codepoint) {
            return font_ua_9x15[i].glyph;
        }
    }

    return NULL;
}

/* Write pixel to framebuffer using physical coordinates */
static void draw_pixel_physical(uint8_t *framebuffer, int phys_x, int phys_y, uint8_t color)
{
    if (phys_x < 0 || phys_x >= s_graphics.phys_width ||
        phys_y < 0 || phys_y >= s_graphics.phys_height) {
        return;
    }

    int byte_index = (phys_y * s_graphics.bytes_per_row) + (phys_x / 8);
    int bit_index = 7 - (phys_x % 8);

    if (color) {
        framebuffer[byte_index] |= (1 << bit_index);
    } else {
        framebuffer[byte_index] &= ~(1 << bit_index);
    }
}

/* Convert logical coordinates to physical and draw pixel */
void draw_pixel(uint8_t *framebuffer, int x, int y, uint8_t color)
{
    /* Transform logical (x,y) to physical coordinates
     * 90-degree clockwise rotation: (x, y) -> (phys_width - 1 - y, x)
     */
    int phys_x = s_graphics.phys_width - 1 - y;
    int phys_y = x;
    draw_pixel_physical(framebuffer, phys_x, phys_y, color);
}

static void draw_glyph(uint8_t *framebuffer, int x, int y, const uint16_t *glyph, uint8_t color)
{
    /* Font is 9 columns x 15 rows
     * Each column is a uint16_t where bits 0-14 represent vertical pixels
     * x,y is top-left corner of the glyph in logical coordinates
     */
    for (int col = 0; col < 9; col++) {
        uint16_t column = glyph[col];
        for (int row = 0; row < 15; row++) {
            if (column & (1 << row)) {
                /* col = horizontal position (0-8), row = vertical position (0-14) */
                int logical_x = x + col;
                int logical_y = y + row;
                draw_pixel(framebuffer, logical_x, logical_y, color);
            }
        }
    }
}

void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color)
{
    /* Text flows left-to-right, wraps to next line downward
     * x,y is top-left corner in logical coordinates
     */
    int cursor_x = x;
    int cursor_y = y;
    const char *cursor = str;
    const int start_x = x;

    while (*cursor) {
        uint32_t codepoint = utf8_next_codepoint(&cursor);

        if (codepoint == '\n') {
            cursor_y += FONT_CHAR_HEIGHT + 2;
            cursor_x = start_x;

            if (cursor_y + FONT_CHAR_HEIGHT > s_graphics.logical_height) {
                break;
            }
            continue;
        }

        const uint16_t *glyph = get_glyph(codepoint);

        if (glyph != NULL) {
            if (cursor_x + FONT_CHAR_WIDTH > s_graphics.logical_width) {
                cursor_y += FONT_CHAR_HEIGHT + 2;
                cursor_x = start_x;

                if (cursor_y + FONT_CHAR_HEIGHT > s_graphics.logical_height) {
                    break;
                }
            }

            draw_glyph(framebuffer, cursor_x, cursor_y, glyph, color);
            cursor_x += FONT_CHAR_SPACING;
        }
    }
}

int measure_string_width(const char *str)
{
    /* Calculate width of string in pixels (logical coordinates) */
    const char *cursor = str;
    int max_width = 0;
    int current_width = 0;

    while (*cursor) {
        uint32_t codepoint = utf8_next_codepoint(&cursor);

        if (codepoint == '\n') {
            if (current_width > max_width) {
                max_width = current_width;
            }
            current_width = 0;
        } else if (get_glyph(codepoint) != NULL) {
            current_width += FONT_CHAR_SPACING;
        }
    }

    if (current_width > max_width) {
        max_width = current_width;
    }

    return max_width;
}
