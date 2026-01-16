/**
 * @file display.h
 * @brief High-level display interface with framebuffer management
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Display context structure (opaque)
 */
typedef struct display_ctx display_ctx_t;

/**
 * @brief Initialize the display subsystem
 *
 * Initializes the e-paper hardware and allocates framebuffer.
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
 * @param x X coordinate (pixels)
 * @param y Y coordinate (pixels)
 * @param text UTF-8 encoded text string
 * @param color 0=white, 1=black
 */
void display_draw_text(int x, int y, const char *text, uint8_t color);

/**
 * @brief Get the width of a text string in pixels
 *
 * @param text UTF-8 encoded text string
 * @return Width in pixels
 */
int display_measure_text(const char *text);

/**
 * @brief Update the physical display with framebuffer contents
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if display update fails
 */
esp_err_t display_update(void);

/**
 * @brief Put display into deep sleep mode
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if sleep operation fails
 */
esp_err_t display_sleep(void);

/**
 * @brief Wake up display from sleep mode
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if wake operation fails
 */
esp_err_t display_wake(void);

/**
 * @brief Get display width
 *
 * @return Width in pixels
 */
int display_get_width(void);

/**
 * @brief Get display height
 *
 * @return Height in pixels
 */
int display_get_height(void);

#endif /* DISPLAY_H */
