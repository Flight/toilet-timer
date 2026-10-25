/**
 * @file battery_log.h
 * @brief Battery voltage history kept in NVS: one entry per wake-up
 *
 * The history is printed at every boot so the real drain can be measured
 * over days from the serial log, without a multimeter.
 */

#ifndef BATTERY_LOG_H
#define BATTERY_LOG_H

#include <stdint.h>

#define BATTERY_LOG_FLAG_WEAK_LATCH 0x01 /**< The battery latch was held by the internal pull-up */
#define BATTERY_LOG_FLAG_WIFI       0x02 /**< Wi-Fi was used on this wake-up */

/**
 * @brief Append this wake-up to the history and print the whole history
 *
 * @param millivolts Battery voltage measured on this wake-up (0 if unknown)
 * @param flags BATTERY_LOG_FLAG_* bits describing this wake-up
 */
void battery_log_record(int millivolts, uint8_t flags);

#endif /* BATTERY_LOG_H */
