/**
 * @file rtc_pcf85063.h
 * @brief PCF85063 real-time clock driver (I2C) for accurate timekeeping
 *
 * The RTC keeps UTC. The system time is restored from it on every boot and
 * written back after each SNTP sync. Its alarm output (active LOW) is used
 * as a deep-sleep wake-up source for the nightly display refresh.
 */

#ifndef RTC_PCF85063_H
#define RTC_PCF85063_H

#include <esp_err.h>
#include <time.h>

/**
 * @brief Initialize the I2C bus and probe the RTC
 *
 * @return ESP_OK if the chip answers, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t rtc_pcf85063_init(void);

/**
 * @brief Read the RTC time (UTC)
 *
 * @param utc_tm Output broken-down UTC time
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if the clock lost integrity (never set / power loss)
 */
esp_err_t rtc_pcf85063_get_time(struct tm *utc_tm);

/**
 * @brief Write the RTC time (UTC) and clear the oscillator-stop flag
 */
esp_err_t rtc_pcf85063_set_time(const struct tm *utc_tm);

/**
 * @brief Copy the RTC time into the system clock if it is valid
 *
 * @return ESP_OK if the system time was set from the RTC
 */
esp_err_t rtc_pcf85063_restore_system_time(void);

/**
 * @brief Copy the system clock into the RTC
 */
esp_err_t rtc_pcf85063_save_system_time(void);

/**
 * @brief Arm the alarm for an absolute point in time
 *
 * Only hour, minute and second are matched, so the alarm repeats daily
 * until re-armed. The interrupt pin goes LOW when the alarm fires and is
 * released by the next call (or by init).
 *
 * @param target Time at which the alarm should fire (must be in the future)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the RTC is not usable
 */
esp_err_t rtc_pcf85063_arm_alarm_at(time_t target);

/**
 * @brief Arm the alarm for the next occurrence of a local time of day
 *
 * The alarm repeats daily (same UTC time) until re-armed; it is re-armed on
 * every wake-up so DST changes are followed. The interrupt pin goes LOW when
 * the alarm fires and is released by the next call (or by init).
 *
 * @param local_hour Hour of day in local time (0-23)
 * @param local_minute Minute (0-59)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the RTC or the system time is not usable
 */
esp_err_t rtc_pcf85063_arm_daily_alarm(int local_hour, int local_minute);

#endif /* RTC_PCF85063_H */
