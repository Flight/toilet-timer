/**
 * @file board_i2c.h
 * @brief Shared I2C master bus of the Waveshare ESP32-S3-ePaper-1.54
 *        (PCF85063 RTC, SHTC3 sensor, ES8311 codec, optional touch)
 */

#ifndef BOARD_I2C_H
#define BOARD_I2C_H

#include <esp_err.h>
#include <driver/i2c_master.h>

/**
 * @brief Get the board I2C bus, creating it on first use
 *
 * @param bus Output bus handle
 * @return ESP_OK on success
 */
esp_err_t board_i2c_get_bus(i2c_master_bus_handle_t *bus);

#endif /* BOARD_I2C_H */
