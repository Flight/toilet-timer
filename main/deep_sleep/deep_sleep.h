/**
 * @file deep_sleep.h
 * @brief Deep sleep management with button, RTC alarm and timer wake-up
 */

#ifndef DEEP_SLEEP_H
#define DEEP_SLEEP_H

#include <esp_err.h>

/**
 * @brief Configure deep sleep wake-up sources
 *
 * EXT1 on the reset and sync buttons (and the RTC alarm pin when the
 * PCF85063 is enabled), plus the ESP32 timer as the nightly fallback.
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
