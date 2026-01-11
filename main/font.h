/**
 * @file font.h
 * @brief 5x7 bitmap font definitions
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>
#include <stddef.h>

extern const uint8_t font_5x7[][5];

typedef struct {
    uint16_t codepoint;
    uint8_t glyph[5];
} font_glyph_t;

extern const font_glyph_t font_ua_5x7[];
extern const size_t font_ua_5x7_count;

#endif /* FONT_H */
