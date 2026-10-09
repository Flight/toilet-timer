/**
 * @file display.h
 * @brief High-level display interface with framebuffer management
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize the display subsystem
 *
 * Initializes the e-paper hardware and allocates the framebuffer.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NO_MEM if framebuffer allocation fails
 *      - ESP_FAIL if display initialization fails
 */
esp_err_t display_init(void);

/**
 * @brief Deinitialize the display and free resources
 */
void display_deinit(void);

/**
 * @brief Clear the framebuffer to white
 */
void display_clear(void);

/**
 * @brief Draw text on the framebuffer
 *
 * @param x X coordinate (logical pixels)
 * @param y Y coordinate (logical pixels)
 * @param text UTF-8 encoded text string
 * @param color 0=black, 1=white
 * @param scale Integer zoom factor (1 = native font size)
 */
void display_draw_text(int x, int y, const char *text, uint8_t color, int scale);

/**
 * @brief Fill a rectangle on the framebuffer
 *
 * @param x Left edge (logical pixels)
 * @param y Top edge (logical pixels)
 * @param width Width in pixels
 * @param height Height in pixels
 * @param color 0=black, 1=white
 */
void display_fill_rect(int x, int y, int width, int height, uint8_t color);

/**
 * @brief Draw text with every line centered horizontally
 *
 * @param y Y coordinate of the top of the first line
 * @param text UTF-8 encoded text string ('\n' separates lines)
 * @param color 0=black, 1=white
 * @param scale Integer zoom factor (1 = native font size)
 */
void display_draw_text_centered(int y, const char *text, uint8_t color, int scale);

/**
 * @brief Get the width of the widest line of a text string in pixels
 *
 * @param text UTF-8 encoded text string
 * @param scale Integer zoom factor
 * @return Width in pixels
 */
int display_measure_text(const char *text, int scale);

/**
 * @brief Get the total height of a (multi-line) text string in pixels
 *
 * @param text UTF-8 encoded text string
 * @param scale Integer zoom factor
 * @return Height in pixels (0 for an empty string)
 */
int display_text_height(const char *text, int scale);

/**
 * @brief Update the physical display with framebuffer contents (full refresh)
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if display update fails
 */
esp_err_t display_update(void);

/**
 * @brief Put display into deep sleep mode and cut its supply
 */
esp_err_t display_sleep(void);

/**
 * @brief Wake up display from sleep mode
 */
esp_err_t display_wake(void);

/**
 * @brief Get logical display width (after rotation)
 */
int display_get_width(void);

/**
 * @brief Get logical display height (after rotation)
 */
int display_get_height(void);

/**
 * @brief Enable the display supply pin early for stable battery operation
 *
 * Should be called before display_init() during early boot. Also releases the
 * deep-sleep hold that kept the panel unpowered while sleeping.
 */
void display_enable_power_early(void);

#endif /* DISPLAY_H */
