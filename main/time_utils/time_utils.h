/**
 * @file time_utils.h
 * @brief Time and datetime utility functions
 */

#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize timezone from config
 */
void time_utils_init_timezone(void);

/**
 * @brief Check if current time is valid (year > 2020)
 * @return true if time is valid
 */
bool time_utils_is_valid(void);

/**
 * @brief Calculate calendar days between two timestamps
 * @param from Start timestamp
 * @param to End timestamp
 * @return Number of calendar days (0 if same day or invalid)
 */
int time_utils_days_between(time_t from, time_t to);

/**
 * @brief Calculate microseconds until next local midnight
 * @return Microseconds until midnight (minimum 60 seconds)
 */
uint64_t time_utils_us_until_midnight(void);

/**
 * @brief Get Ukrainian day suffix based on pluralization rules
 * @param days Number of days
 * @return Ukrainian suffix ("день", "дні", or "днів")
 */
const char *time_utils_get_days_suffix_uk(int days);

#endif /* TIME_UTILS_H */
