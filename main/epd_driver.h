/**
 * @file epd_driver.h
 * @brief E-Paper Display Driver for LilyGo Mini E-Paper S3
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef EPD_DRIVER_H
#define EPD_DRIVER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief E-Paper display configuration structure
 */
typedef struct {
    uint8_t pin_mosi;       /**< SPI MOSI pin */
    uint8_t pin_clk;        /**< SPI clock pin */
    uint8_t pin_cs;         /**< SPI chip select pin */
    uint8_t pin_dc;         /**< Data/Command pin */
    uint8_t pin_rst;        /**< Reset pin */
    uint8_t pin_busy;       /**< Busy status pin */
    int8_t pin_power;       /**< Power enable pin (-1 if not used) */
    uint16_t width;         /**< Display width in pixels */
    uint16_t height;        /**< Display height in pixels */
} epd_config_t;

/**
 * @brief Initialize the E-Paper display
 *
 * @param config Pointer to display configuration
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if config is NULL
 *      - ESP_FAIL if initialization fails
 */
esp_err_t epd_init(const epd_config_t *config);

/**
 * @brief Deinitialize the E-Paper display and free resources
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if deinitialization fails
 */
esp_err_t epd_deinit(void);

/**
 * @brief Display a buffer on the E-Paper screen
 *
 * @param buffer Pointer to the framebuffer (1 bit per pixel)
 * @param size Size of the buffer in bytes
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if buffer is NULL
 *      - ESP_FAIL if display operation fails
 */
esp_err_t epd_display_buffer(const uint8_t *buffer, size_t size);

/**
 * @brief Clear the E-Paper display to white
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if clear operation fails
 */
esp_err_t epd_clear(void);

/**
 * @brief Power off the panel and enter deep sleep to retain the image without power
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if display is not initialized
 *      - ESP_FAIL if power off fails
 */
esp_err_t epd_sleep(void);

/**
 * @brief Get display width
 *
 * @return Display width in pixels
 */
uint16_t epd_get_width(void);

/**
 * @brief Get display height
 *
 * @return Display height in pixels
 */
uint16_t epd_get_height(void);

#ifdef __cplusplus
}
#endif

#endif /* EPD_DRIVER_H */
