/**
 * @file font_10x20.h
 * @brief 10x20 bitmap font definitions (X11 Misc-Fixed, public domain)
 *
 * Regenerate with:
 *   python3 tools/bdf2font.py 10x20.bdf font_10x20.c
 */

#ifndef FONT_10X20_H
#define FONT_10X20_H

#include <stdint.h>
#include <stddef.h>

#define FONT_10X20_WIDTH  10
#define FONT_10X20_HEIGHT 20

/* ASCII 32..126, indexed by (codepoint - 32). Each row: bit 15 = left-most pixel. */
extern const uint16_t font_10x20_ascii[95][FONT_10X20_HEIGHT];

typedef struct {
    uint16_t codepoint;
    uint16_t rows[FONT_10X20_HEIGHT];
} font_glyph_10x20_t;

/* Degree sign and Cyrillic glyphs (Ukrainian alphabet plus Ё/ё) */
extern const font_glyph_10x20_t font_10x20_extra[];
extern const size_t font_10x20_extra_count;

#endif /* FONT_10X20_H */
