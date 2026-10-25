/**
 * @file battery_level.h
 * @brief Battery voltage measurement through the on-board divider
 */

#ifndef BATTERY_LEVEL_H
#define BATTERY_LEVEL_H

/**
 * @brief Measure the battery and return its charge level
 *
 * Takes a short burst of calibrated ADC samples (a few milliseconds).
 *
 * @param millivolts Optional output of the averaged battery voltage in mV
 * @return Charge level 0-100 %, or -1 if measurement is disabled or failed
 */
int battery_level_read_percent(int *millivolts);

/**
 * @brief Battery voltage of the last successful measurement on this wake-up
 *
 * @return Millivolts, or -1 if nothing was measured yet
 */
int battery_level_last_mv(void);

#endif /* BATTERY_LEVEL_H */
