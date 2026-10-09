#ifndef SNTP_H
#define SNTP_H

#include <stdbool.h>

void sntp_task(void *pvParameter);

/**
 * @brief true if the time was ever synced from SNTP (flag in NVS)
 */
bool sntp_check_first_sync_done(void);

/**
 * @brief Decide whether a Wi-Fi time sync is needed on this wake-up
 *
 * true when the time is unknown, when there is no record of a previous sync,
 * or when the last sync is older than CONFIG_TIME_SYNC_INTERVAL_DAYS.
 * Call after the RTC chip has restored the system time.
 */
bool sntp_is_sync_due(void);

#endif // SNTP_H
