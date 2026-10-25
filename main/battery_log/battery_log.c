/**
 * @file battery_log.c
 * @brief Battery voltage history kept in NVS: one entry per wake-up
 */

#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <nvs.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "battery_log.h"
#include "../trigger/trigger.h"
#include "../time_utils/time_utils.h"

static const char *TAG = "battery_log";

#ifdef CONFIG_BATTERY_LOG_ENABLED

#define NVS_LOG_NAMESPACE "battery_log"
#define NVS_LOG_KEY       "history"
#define LOG_MAGIC         0x31474C42u /* "BLG1" */
#define LOG_CAPACITY      CONFIG_BATTERY_LOG_ENTRIES
#define CHARGE_STEP_MV    30          /* A rise bigger than this between two wake-ups means the battery was charged */

typedef enum {
    CAUSE_POWER_ON = 0,
    CAUSE_KEY,
    CAUSE_RTC_ALARM,
    CAUSE_TIMER,
    CAUSE_OTHER,
} log_cause_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp;  /* Seconds since the epoch, 0 if the time was unknown */
    uint16_t millivolts; /* 0 if the measurement failed */
    uint8_t cause;       /* log_cause_t */
    uint8_t flags;       /* BATTERY_LOG_FLAG_* */
} log_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t capacity;
    uint16_t count;      /* Valid entries */
    uint16_t next;       /* Index written next */
    uint16_t reserved;
    log_entry_t entries[LOG_CAPACITY];
} log_store_t;

static log_store_t s_store;

static const char *cause_name(uint8_t cause)
{
    static const char *const names[] = { "power-on", "key", "rtc-alarm", "timer", "other" };
    return (cause < sizeof(names) / sizeof(names[0])) ? names[cause] : "?";
}

static uint8_t detect_cause(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t mask = esp_sleep_get_ext1_wakeup_status();
            if (mask & TRIGGER_BUTTON_MASK) {
                return CAUSE_KEY;
            }
#ifdef CONFIG_RTC_PCF85063_ENABLED
            if (mask & (1ULL << CONFIG_RTC_INT_GPIO)) {
                return CAUSE_RTC_ALARM;
            }
#endif
            return CAUSE_OTHER;
        }
        case ESP_SLEEP_WAKEUP_TIMER:
            return CAUSE_TIMER;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            return CAUSE_POWER_ON;
        default:
            return CAUSE_OTHER;
    }
}

static void reset_store(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.magic = LOG_MAGIC;
    s_store.capacity = LOG_CAPACITY;
}

static void load_store(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_LOG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        reset_store();
        return;
    }

    size_t size = sizeof(s_store);
    esp_err_t err = nvs_get_blob(handle, NVS_LOG_KEY, &s_store, &size);
    nvs_close(handle);

    bool valid = (err == ESP_OK) && (size == sizeof(s_store)) && (s_store.magic == LOG_MAGIC) &&
                 (s_store.capacity == LOG_CAPACITY) && (s_store.count <= LOG_CAPACITY) && (s_store.next < LOG_CAPACITY);
    if (!valid) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "History reset (%s)", (err == ESP_OK) ? "format changed" : esp_err_to_name(err));
        }
        reset_store();
    }
}

static void save_store(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot open NVS: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(handle, NVS_LOG_KEY, &s_store, sizeof(s_store));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save the history: %s", esp_err_to_name(err));
    }
}

static void format_time(uint32_t timestamp, char *buf, size_t size)
{
    if (timestamp == 0) {
        snprintf(buf, size, "%-16s", "(time unknown)");
        return;
    }
    time_t t = (time_t)timestamp;
    struct tm tm_local = {0};
    localtime_r(&t, &tm_local);
    strftime(buf, size, "%Y-%m-%d %H:%M", &tm_local);
}

/* Print every entry, oldest first, then the drain since the last charge */
static void print_history(void)
{
    ESP_LOGI(TAG, "Battery history (%u of %u wake-ups):", s_store.count, LOG_CAPACITY);

    uint16_t start = (s_store.count < LOG_CAPACITY) ? 0 : s_store.next;
    const log_entry_t *stretch_start = NULL; /* First entry of the current discharge stretch */
    const log_entry_t *last = NULL;
    const log_entry_t *previous = NULL;

    for (uint16_t i = 0; i < s_store.count; i++) {
        const log_entry_t *e = &s_store.entries[(start + i) % LOG_CAPACITY];
        char when[24];
        format_time(e->timestamp, when, sizeof(when));
        ESP_LOGI(TAG, "  %s  %4u mV  %-9s%s%s", when, e->millivolts, cause_name(e->cause),
                 (e->flags & BATTERY_LOG_FLAG_WEAK_LATCH) ? "  weak-latch" : "",
                 (e->flags & BATTERY_LOG_FLAG_WIFI) ? "  wifi" : "");

        if (e->timestamp == 0 || e->millivolts == 0) {
            continue;
        }
        if (stretch_start == NULL || (previous != NULL && (int)e->millivolts > (int)previous->millivolts + CHARGE_STEP_MV)) {
            stretch_start = e; /* First valid entry, or the battery was charged in between */
        }
        previous = e;
        last = e;
    }

    if (stretch_start != NULL && last != NULL && last != stretch_start && last->timestamp > stretch_start->timestamp) {
        int64_t seconds = (int64_t)last->timestamp - (int64_t)stretch_start->timestamp;
        int delta_mv = (int)last->millivolts - (int)stretch_start->millivolts;
        int64_t tenths_of_days = seconds * 10 / 86400;
        int64_t mv_per_day = (int64_t)delta_mv * 86400 / seconds;
        ESP_LOGI(TAG, "  Since the last charge: %d mV in %lld.%lld days (%lld mV/day)",
                 delta_mv, (long long)(tenths_of_days / 10), (long long)(tenths_of_days % 10), (long long)mv_per_day);
    }
}

void battery_log_record(int millivolts, uint8_t flags)
{
    load_store();

    log_entry_t *e = &s_store.entries[s_store.next];
    time_t now = 0;
    time(&now);
    e->timestamp = time_utils_is_valid() ? (uint32_t)now : 0;
    e->millivolts = (millivolts > 0 && millivolts < 65535) ? (uint16_t)millivolts : 0;
    e->cause = detect_cause();
    e->flags = flags;

    s_store.next = (uint16_t)((s_store.next + 1) % LOG_CAPACITY);
    if (s_store.count < LOG_CAPACITY) {
        s_store.count++;
    }

    save_store();
    print_history();
}

#else /* CONFIG_BATTERY_LOG_ENABLED */

void battery_log_record(int millivolts, uint8_t flags)
{
    (void)millivolts;
    (void)flags;
    ESP_LOGD(TAG, "Battery history disabled");
}

#endif /* CONFIG_BATTERY_LOG_ENABLED */
