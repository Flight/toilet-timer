/**
 * @file font_9x15.h
 * @brief 9x15 bitmap font definitions
 */

#ifndef FONT_9X15_H
#define FONT_9X15_H

#include <stdint.h>
#include <stddef.h>

extern const uint16_t font_9x15[][9];

typedef struct {
    uint16_t codepoint;
    uint16_t glyph[9];
} font_glyph_9x15_t;

extern const font_glyph_9x15_t font_ua_9x15[];
extern const size_t font_ua_9x15_count;

#endif /* FONT_9X15_H */
