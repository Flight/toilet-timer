/**
 * @file graphics.c
 * @brief Graphics drawing functions for framebuffer
 *
 * Coordinates use the logical (rotated) display orientation:
 * - (0,0) is the top-left corner of the display as viewed by the user
 * - x increases to the right, y increases downward
 * - CONFIG_DISPLAY_ROTATION (0/90/180/270 degrees clockwise) maps logical
 *   coordinates to physical panel pixels
 *
 * Framebuffer layout: physical row-major, 1 bit per pixel, MSB first,
 * bit set = white, bit clear = black.
 */

#include "sdkconfig.h"
#include "graphics.h"
#include "fonts/font_10x20.h"
#include "utf8.h"
#include "global_constants.h"

static struct {
    int phys_width;      /* Physical display width */
    int phys_height;     /* Physical display height */
    int logical_width;   /* Logical width (after rotation) */
    int logical_height;  /* Logical height (after rotation) */
    int bytes_per_row;
} s_graphics = {0};

void graphics_init(int width, int height)
{
    s_graphics.phys_width = width;
    s_graphics.phys_height = height;
    s_graphics.bytes_per_row = width / 8;
#if CONFIG_DISPLAY_ROTATION == 90 || CONFIG_DISPLAY_ROTATION == 270
    s_graphics.logical_width = height;
    s_graphics.logical_height = width;
#else
    s_graphics.logical_width = width;
    s_graphics.logical_height = height;
#endif
}

int graphics_get_width(void)
{
    return s_graphics.logical_width;
}

int graphics_get_height(void)
{
    return s_graphics.logical_height;
}

static int line_height(int scale)
{
    return (FONT_CHAR_HEIGHT + FONT_LINE_SPACING) * scale;
}

static int char_advance(int scale)
{
    return FONT_CHAR_SPACING * scale;
}

static const uint16_t *get_glyph(uint32_t codepoint)
{
    if (codepoint >= 32 && codepoint <= 126) {
        return font_10x20_ascii[codepoint - 32];
    }

    for (size_t i = 0; i < font_10x20_extra_count; i++) {
        if (font_10x20_extra[i].codepoint == codepoint) {
            return font_10x20_extra[i].rows;
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
    int phys_x, phys_y;

#if CONFIG_DISPLAY_ROTATION == 90
    phys_x = s_graphics.phys_width - 1 - y;
    phys_y = x;
#elif CONFIG_DISPLAY_ROTATION == 180
    phys_x = s_graphics.phys_width - 1 - x;
    phys_y = s_graphics.phys_height - 1 - y;
#elif CONFIG_DISPLAY_ROTATION == 270
    phys_x = y;
    phys_y = s_graphics.phys_height - 1 - x;
#else
    phys_x = x;
    phys_y = y;
#endif

    draw_pixel_physical(framebuffer, phys_x, phys_y, color);
}

void fill_rect(uint8_t *framebuffer, int x, int y, int width, int height, uint8_t color)
{
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            draw_pixel(framebuffer, x + dx, y + dy, color);
        }
    }
}

static void draw_glyph(uint8_t *framebuffer, int x, int y, const uint16_t *glyph, uint8_t color, int scale)
{
    /* Font is FONT_CHAR_WIDTH columns x FONT_CHAR_HEIGHT rows.
     * Each row is a uint16_t with bit 15 = left-most pixel.
     * x,y is the top-left corner of the glyph in logical coordinates. */
    for (int row = 0; row < FONT_CHAR_HEIGHT; row++) {
        uint16_t bits = glyph[row];
        if (bits == 0) {
            continue;
        }
        for (int col = 0; col < FONT_CHAR_WIDTH; col++) {
            if (!(bits & (0x8000 >> col))) {
                continue;
            }
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    draw_pixel(framebuffer, x + col * scale + dx, y + row * scale + dy, color);
                }
            }
        }
    }
}

/* Number of drawable glyphs until the next '\n' or the end of the string */
static int line_glyph_count(const char *line)
{
    const char *cursor = line;
    int count = 0;

    while (*cursor) {
        uint32_t codepoint = utf8_next_codepoint(&cursor);
        if (codepoint == '\n') {
            break;
        }
        if (get_glyph(codepoint) != NULL) {
            count++;
        }
    }

    return count;
}

static int line_width(int glyph_count, int scale)
{
    if (glyph_count <= 0) {
        return 0;
    }
    /* Advance per glyph minus the trailing inter-character gap */
    return glyph_count * char_advance(scale) - scale;
}

void draw_string(uint8_t *framebuffer, int x, int y, const char *str, uint8_t color, int scale)
{
    /* Text flows left-to-right, wraps to next line downward
     * x,y is top-left corner in logical coordinates */
    if (scale < 1) {
        scale = 1;
    }

    int cursor_x = x;
    int cursor_y = y;
    const char *cursor = str;
    const int start_x = x;
    const int glyph_w = FONT_CHAR_WIDTH * scale;
    const int glyph_h = FONT_CHAR_HEIGHT * scale;

    while (*cursor) {
        uint32_t codepoint = utf8_next_codepoint(&cursor);

        if (codepoint == '\n') {
            cursor_y += line_height(scale);
            cursor_x = start_x;

            if (cursor_y + glyph_h > s_graphics.logical_height) {
                break;
            }
            continue;
        }

        const uint16_t *glyph = get_glyph(codepoint);

        if (glyph != NULL) {
            if (cursor_x + glyph_w > s_graphics.logical_width) {
                cursor_y += line_height(scale);
                cursor_x = start_x;

                if (cursor_y + glyph_h > s_graphics.logical_height) {
                    break;
                }
            }

            draw_glyph(framebuffer, cursor_x, cursor_y, glyph, color, scale);
            cursor_x += char_advance(scale);
        }
    }
}

void draw_string_centered(uint8_t *framebuffer, int y, const char *str, uint8_t color, int scale)
{
    if (scale < 1) {
        scale = 1;
    }

    const char *cursor = str;
    int cursor_y = y;

    while (*cursor) {
        int width = line_width(line_glyph_count(cursor), scale);
        int cursor_x = (s_graphics.logical_width - width) / 2;
        if (cursor_x < 0) {
            cursor_x = 0;
        }

        /* Draw this line up to the next '\n' */
        while (*cursor) {
            uint32_t codepoint = utf8_next_codepoint(&cursor);
            if (codepoint == '\n') {
                break;
            }
            const uint16_t *glyph = get_glyph(codepoint);
            if (glyph != NULL) {
                draw_glyph(framebuffer, cursor_x, cursor_y, glyph, color, scale);
                cursor_x += char_advance(scale);
            }
        }

        cursor_y += line_height(scale);
        if (cursor_y >= s_graphics.logical_height) {
            break;
        }
    }
}

int measure_string_width(const char *str, int scale)
{
    if (scale < 1) {
        scale = 1;
    }

    const char *cursor = str;
    int max_width = 0;

    while (*cursor) {
        int width = line_width(line_glyph_count(cursor), scale);
        if (width > max_width) {
            max_width = width;
        }

        /* Skip to the next line */
        while (*cursor) {
            if (utf8_next_codepoint(&cursor) == '\n') {
                break;
            }
        }
    }

    return max_width;
}

int measure_string_height(const char *str, int scale)
{
    if (scale < 1) {
        scale = 1;
    }

    const char *cursor = str;
    int lines = 0;

    while (*cursor) {
        lines++;
        while (*cursor) {
            if (utf8_next_codepoint(&cursor) == '\n') {
                break;
            }
        }
    }

    return lines * line_height(scale);
}
