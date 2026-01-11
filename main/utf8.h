/**
 * @file utf8.h
 * @brief UTF-8 string decoder utilities
 */

#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Decode the next UTF-8 codepoint from a string
 *
 * @param str Pointer to string pointer (will be advanced)
 * @return Unicode codepoint, or 0 if end of string
 */
uint32_t utf8_next_codepoint(const char **str);

/**
 * @brief Count the number of characters in a UTF-8 string
 *
 * @param str UTF-8 encoded string
 * @return Number of characters (not bytes)
 */
size_t utf8_strlen(const char *str);

#endif /* UTF8_H */
