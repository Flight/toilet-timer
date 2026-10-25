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
 * EXT1 on the two keys (and the RTC alarm pin when the PCF85063 is enabled),
 * plus the ESP32 timer. With a working RTC alarm the timer is armed a couple
 * of hours after that alarm as a fallback only, so there is a single wake-up
 * per night; without the RTC the timer does the nightly wake-up itself.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t deep_sleep_configure_wakeup(void);

/**
 * @brief Enter deep sleep mode
 *
 * Waits for a running LED pattern, freezes the power pins and sleeps.
 * This function does not return - the device will wake up and restart.
 */
void deep_sleep_enter(void);

/**
 * @brief Go to sleep after something failed, instead of staying awake
 *
 * Configures the usual wake-up sources (or, if even that fails, a one-hour
 * timer) and enters deep sleep. Does not return.
 *
 * @param reason Short description for the log
 */
void deep_sleep_fallback(const char *reason);

/**
 * @brief Start the awake watchdog task
 *
 * Forces deep sleep after CONFIG_MAX_AWAKE_SECONDS, whatever the other tasks
 * are doing, so a hang can never drain the battery. A running OTA update is
 * granted the same time once more.
 */
void deep_sleep_start_awake_watchdog(void);

#endif // DEEP_SLEEP_H
