/**
 * @file utf8.c
 * @brief UTF-8 string decoder implementation
 */

#include "utf8.h"

uint32_t utf8_next_codepoint(const char **str)
{
    const unsigned char *s = (const unsigned char *)*str;

    if (s[0] == 0) {
        return 0;
    }

    /* 1-byte sequence: 0xxxxxxx */
    if (s[0] < 0x80) {
        (*str)++;
        return s[0];
    }

    /* 2-byte sequence: 110xxxxx 10xxxxxx */
    if ((s[0] & 0xE0) == 0xC0 && s[1] != 0) {
        if ((s[1] & 0xC0) == 0x80) {
            uint32_t codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            *str += 2;
            return codepoint;
        }
    }

    /* 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx */
    if ((s[0] & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0) {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            uint32_t codepoint = ((s[0] & 0x0F) << 12) |
                                ((s[1] & 0x3F) << 6) |
                                (s[2] & 0x3F);
            *str += 3;
            return codepoint;
        }
    }

    /* 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if ((s[0] & 0xF8) == 0xF0 && s[1] != 0 && s[2] != 0 && s[3] != 0) {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
            uint32_t codepoint = ((s[0] & 0x07) << 18) |
                                ((s[1] & 0x3F) << 12) |
                                ((s[2] & 0x3F) << 6) |
                                (s[3] & 0x3F);
            *str += 4;
            return codepoint;
        }
    }

    /* Invalid sequence - skip one byte and return replacement character */
    (*str)++;
    return '?';
}

size_t utf8_strlen(const char *str)
{
    size_t count = 0;
    const char *cursor = str;

    while (*cursor) {
        (void)utf8_next_codepoint(&cursor);
        count++;
    }

    return count;
}
