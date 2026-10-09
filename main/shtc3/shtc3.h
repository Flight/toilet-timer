/**
 * @file shtc3.h
 * @brief SHTC3 temperature / humidity sensor (I2C)
 *
 * One measurement costs a few milliseconds; the sensor sleeps between
 * readings (well under 1 uA), so it does not affect battery life.
 */

#ifndef SHTC3_H
#define SHTC3_H

#include <esp_err.h>

typedef struct {
    int temperature_x10; /**< Temperature in tenths of a degree Celsius (e.g. 224 = 22.4 C) */
    int humidity;        /**< Relative humidity in percent (0-100) */
} shtc3_reading_t;

/**
 * @brief Wake the sensor, take one measurement and put it back to sleep
 *
 * @param reading Output reading
 * @return ESP_OK on success, error code if the sensor does not answer or the CRC fails
 */
esp_err_t shtc3_read(shtc3_reading_t *reading);

#endif /* SHTC3_H */
