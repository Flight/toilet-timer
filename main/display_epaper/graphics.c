/**
 * @file graphics.c
 * @brief Graphics drawing functions for framebuffer
 */

#include "graphics.h"
#include "fonts/font_9x15.h"
#include "utf8.h"
#include "global_constants.h"

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

static void draw_glyph(uint8_t *framebuffer, int x, int y, const uint16_t *glyph, uint8_t color)
{
    /* Font is 9 columns x 15 rows
     * 90-degree clockwise rotation without mirroring
     * After rotation: width=15, height=9
     */
    for (int col = 0; col < 9; col++) {  /* Font width (9 columns) */
        uint16_t column = glyph[col];
        for (int row = 0; row < 15; row++) {  /* Font height (15 rows) */
            if (column & (1 << row)) {
                /* Rotate 90 degrees clockwise
                 * (col, row) -> (14-row, col)
                 */
                int rotated_x = x + (14 - row);     /* 14-row becomes x */
                int rotated_y = y + col;             /* col becomes y */

                draw_pixel(framebuffer, rotated_x, rotated_y, color);
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
    /* With 90-degree rotation, text flows downward (vertically)
     * Wrap to next column (leftward) if text exceeds screen height or on \n
     */
    int cursor_x = x;
    int cursor_y = y;
    const char *cursor = str;
    const int start_y = y;  /* Remember starting y position for wrapping */

    while (*cursor) {
        uint32_t codepoint = utf8_next_codepoint(&cursor);

        /* Handle manual line break (newline character) */
        if (codepoint == '\n') {
            /* Move to next column (left) and reset to start y */
            cursor_x -= (FONT_CHAR_HEIGHT + 2);  /* Line spacing = char height + small gap */
            cursor_y = start_y;

            /* Stop if we've gone off the left edge */
            if (cursor_x < 0) {
                break;
            }
            continue;
        }

        const uint16_t *glyph = get_glyph(codepoint);

        if (glyph != NULL) {
            /* Check if current character would exceed screen height BEFORE drawing */
            if (cursor_y + FONT_CHAR_HEIGHT > s_graphics.height) {
                /* Wrap to next column (move left and reset to start y) */
                cursor_x -= (FONT_CHAR_HEIGHT + 2);  /* Line spacing = char height + small gap */
                cursor_y = start_y;

                /* Stop if we've gone off the left edge */
                if (cursor_x < 0) {
                    break;
                }
            }

            draw_glyph(framebuffer, cursor_x, cursor_y, glyph, color);
            cursor_y += FONT_CHAR_SPACING;
        }
    }
}

int measure_string_width(const char *str)
{
    /* With 90-degree rotation and wrapping, calculate total width needed
     * This includes multiple columns if text wraps or has manual line breaks
     */
    const char *cursor = str;
    int num_columns = 1;  /* Start with at least one column */
    int chars_in_column = 0;
    int chars_per_column = s_graphics.height / FONT_CHAR_SPACING;
    if (chars_per_column == 0) chars_per_column = 1;

    /* Count characters and manual line breaks */
    while (*cursor) {
        uint32_t codepoint = utf8_next_codepoint(&cursor);

        if (codepoint == '\n') {
            /* Manual line break - start new column */
            num_columns++;
            chars_in_column = 0;
        } else if (get_glyph(codepoint) != NULL) {
            chars_in_column++;

            /* Auto-wrap to new column if needed */
            if (chars_in_column >= chars_per_column) {
                num_columns++;
                chars_in_column = 0;
            }
        }
    }

    /* Return total width: columns * char_width + gaps between columns */
    if (num_columns <= 1) {
        size_t char_count = utf8_strlen(str);
        return (int)(char_count * FONT_CHAR_SPACING);  /* Single column: return height */
    } else {
        return num_columns * (FONT_CHAR_HEIGHT + 2);  /* Multiple columns with small gap */
    }
}
