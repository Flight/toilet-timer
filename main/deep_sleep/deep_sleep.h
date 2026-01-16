/**
 * @file deep_sleep.h
 * @brief Deep sleep management with EXT1 wake-up
 */

#ifndef DEEP_SLEEP_H
#define DEEP_SLEEP_H

#include <esp_err.h>

/**
 * @brief Configure deep sleep wake-up sources (EXT1 on GPIO0, GPIO3, GPIO4)
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t deep_sleep_configure_wakeup(void);

/**
 * @brief Enter deep sleep mode
 *
 * This function does not return - the device will wake up and restart
 */
void deep_sleep_enter(void);

#endif // DEEP_SLEEP_H
