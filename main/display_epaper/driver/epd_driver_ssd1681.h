/**
 * @file epd_driver_ssd1681.h
 * @brief E-Paper Display Driver for the 1.54" 200x200 SSD1681 panel
 *        (Waveshare ESP32-S3-ePaper-1.54)
 */

#ifndef EPD_DRIVER_SSD1681_H
#define EPD_DRIVER_SSD1681_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief E-Paper display configuration structure
 */
typedef struct {
    uint8_t pin_mosi;       /**< SPI MOSI pin */
    uint8_t pin_clk;        /**< SPI clock pin */
    uint8_t pin_cs;         /**< SPI chip select pin */
    uint8_t pin_dc;         /**< Data/Command pin */
    uint8_t pin_rst;        /**< Reset pin */
    uint8_t pin_busy;       /**< Busy status pin (HIGH while busy) */
    int8_t pin_power;       /**< Panel supply enable pin (-1 if not used) */
    bool power_active_low;  /**< true if LOW on pin_power turns the panel on */
    uint16_t width;         /**< Display width in pixels */
    uint16_t height;        /**< Display height in pixels */
} epd_config_t;

/**
 * @brief Initialize the E-Paper display (SPI bus, GPIOs, panel init sequence)
 *
 * @param config Pointer to display configuration
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if config is NULL
 *      - ESP_FAIL / ESP_ERR_TIMEOUT if initialization fails
 */
esp_err_t epd_init(const epd_config_t *config);

/**
 * @brief Deinitialize the E-Paper display and free the SPI bus
 */
esp_err_t epd_deinit(void);

/**
 * @brief Display a buffer on the E-Paper screen with a full refresh
 *
 * @param buffer Framebuffer, 1 bit per pixel, row-major, MSB first,
 *               bit set = white, bit clear = black
 * @param size Size of the buffer in bytes (width * height / 8)
 */
esp_err_t epd_display_buffer(const uint8_t *buffer, size_t size);

/**
 * @brief Clear the E-Paper display to white
 */
esp_err_t epd_clear(void);

/**
 * @brief Put the controller into deep sleep and cut the panel supply.
 *        The image is retained without power. Only epd_wake() can resume.
 */
esp_err_t epd_sleep(void);

/**
 * @brief Power the panel back on and re-run the init sequence
 */
esp_err_t epd_wake(void);

/**
 * @brief Get display width in pixels
 */
uint16_t epd_get_width(void);

/**
 * @brief Get display height in pixels
 */
uint16_t epd_get_height(void);

#endif /* EPD_DRIVER_SSD1681_H */
