/**
 * @file time_utils.c
 * @brief Time and datetime utility functions
 */

#include <sdkconfig.h>
#include <esp_log.h>
#include <stdlib.h>
#include "time_utils.h"

static const char *TAG = "time_utils";

void time_utils_init_timezone(void)
{
    setenv("TZ", CONFIG_SNTP_TIMEZONE, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", CONFIG_SNTP_TIMEZONE);
}

bool time_utils_is_valid(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    gmtime_r(&now, &timeinfo);
    return (timeinfo.tm_year + 1900) > 2020;
}

int time_utils_days_between(time_t from, time_t to)
{
    if (from == 0 || to <= from) {
        return 0;
    }

    struct tm from_tm = {0};
    struct tm to_tm = {0};
    localtime_r(&from, &from_tm);
    localtime_r(&to, &to_tm);

    /* Normalize to midnight */
    struct tm from_midnight = from_tm;
    from_midnight.tm_hour = 0;
    from_midnight.tm_min = 0;
    from_midnight.tm_sec = 0;

    struct tm to_midnight = to_tm;
    to_midnight.tm_hour = 0;
    to_midnight.tm_min = 0;
    to_midnight.tm_sec = 0;

    time_t from_day = mktime(&from_midnight);
    time_t to_day = mktime(&to_midnight);

    return (int)((to_day - from_day) / (24 * 60 * 60));
}

uint64_t time_utils_us_until_midnight(void)
{
    /* Ensure timezone is set */
    time_utils_init_timezone();

    time_t now = 0;
    time(&now);

    struct tm now_tm = {0};
    localtime_r(&now, &now_tm);

    /* Target 1:00 AM for the daily OTA check wake-up.
     * The ESP32's internal oscillator drifts ~27 min/day, causing early
     * wake-ups. After SNTP corrects the time, the remaining sleep time
     * can be very short (seconds/minutes), causing repeated wake-ups.
     * To prevent this, if the time until target is under 1 hour,
     * we skip to the next day's 1:00 AM instead. */
    struct tm target_tm = now_tm;
    target_tm.tm_hour = 1;
    target_tm.tm_min = 0;
    target_tm.tm_sec = 0;

    /* If it's already past 1:00 AM, target next day */
    if (now_tm.tm_hour >= 1) {
        target_tm.tm_mday += 1;
    }

    time_t target = mktime(&target_tm);

    int64_t seconds_until_target = (int64_t)(target - now);

    /* If less than 1 hour until target, skip to next day to avoid
     * multiple short wake-sleep cycles from RTC drift correction */
    if (seconds_until_target < 3600) {
        ESP_LOGI(TAG, "Only %lld seconds until 1:00 AM, targeting next day", seconds_until_target);
        target_tm.tm_mday += 1;
        target = mktime(&target_tm);
        seconds_until_target = (int64_t)(target - now);
    }

    ESP_LOGI(TAG, "Seconds until 1:00 AM: %lld", seconds_until_target);

    return (uint64_t)seconds_until_target * 1000000ULL;
}

const char *time_utils_get_days_suffix_uk(int days)
{
    /* Ukrainian pluralization rules:
     * 1 день
     * 2, 3, 4 дні
     * 5-20 днів
     * 21 день, 22-24 дні, 25-30 днів
     * etc.
     */
    int last_two_digits = days % 100;
    int last_digit = days % 10;

    if (last_two_digits >= 11 && last_two_digits <= 19) {
        return "днів";
    }

    if (last_digit == 1) {
        return "день";
    }

    if (last_digit >= 2 && last_digit <= 4) {
        return "дні";
    }

    return "днів";
}
