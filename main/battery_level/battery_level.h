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

#endif /* BATTERY_LEVEL_H */
