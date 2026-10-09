#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <stdlib.h>
#include <time.h>

#include "global_event_group.h"
#include "../nvs_utils/nvs_utils.h"
#include "../time_utils/time_utils.h"
#ifdef CONFIG_RTC_PCF85063_ENABLED
#include "../rtc_pcf85063/rtc_pcf85063.h"
#endif
#include "sntp.h"

static const char *TAG = "SNTP";
static bool sntp_initialized = false;

#define NVS_SNTP_NAMESPACE "sntp_info"
#define NVS_FIRST_SYNC_KEY "first_sync"
#define NVS_LAST_SYNC_KEY "last_sync"

#define SECONDS_PER_DAY 86400
#define TIME_CORRECTION_THRESHOLD_S 300

bool sntp_check_first_sync_done(void)
{
    uint8_t first_sync_done = 0;
    esp_err_t err = nvs_utils_read_u8(NVS_SNTP_NAMESPACE, NVS_FIRST_SYNC_KEY, &first_sync_done);
    return (err == ESP_OK && first_sync_done == 1);
}

static void sntp_save_first_sync_done(void)
{
    if (nvs_utils_write_u8(NVS_SNTP_NAMESPACE, NVS_FIRST_SYNC_KEY, 1) == ESP_OK) {
        ESP_LOGI(TAG, "First SNTP sync flag saved");
    }
}

static time_t sntp_get_last_sync(void)
{
    time_t last_sync = 0;
    if (nvs_utils_read_timestamp(NVS_SNTP_NAMESPACE, NVS_LAST_SYNC_KEY, &last_sync) != ESP_OK) {
        return 0;
    }
    return last_sync;
}

static void sntp_save_last_sync(time_t now)
{
    if (nvs_utils_write_timestamp(NVS_SNTP_NAMESPACE, NVS_LAST_SYNC_KEY, now) == ESP_OK) {
        ESP_LOGI(TAG, "Last sync timestamp saved: %ld", (long)now);
    }
}

bool sntp_is_sync_due(void)
{
    if (!time_utils_is_valid()) {
        ESP_LOGI(TAG, "Time unknown, sync due");
        return true;
    }

    time_t last_sync = sntp_get_last_sync();
    if (last_sync == 0) {
        ESP_LOGI(TAG, "No record of a previous sync, sync due");
        return true;
    }

    time_t now = 0;
    time(&now);
    if (now < last_sync) {
        ESP_LOGW(TAG, "Last sync is in the future, sync due");
        return true;
    }

    int64_t age_days = (int64_t)(now - last_sync) / SECONDS_PER_DAY;
    bool due = age_days >= CONFIG_TIME_SYNC_INTERVAL_DAYS;
    ESP_LOGI(TAG, "Last sync %lld days ago (interval %d days): sync %s",
             (long long)age_days, CONFIG_TIME_SYNC_INTERVAL_DAYS, due ? "due" : "not needed");
    return due;
}

static esp_err_t sync_time_with_sntp(void)
{
    const TickType_t sync_wait_ticks = pdMS_TO_TICKS(10000);

    if (!sntp_initialized) {
        const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
        esp_netif_sntp_init(&config);
        sntp_initialized = true;
        ESP_LOGI(TAG, "SNTP server: %s", CONFIG_SNTP_TIME_SERVER);
    } else {
        esp_netif_sntp_start();
    }

    esp_err_t sync_err = esp_netif_sntp_sync_wait(sync_wait_ticks);
    if (sync_err == ESP_OK) {
        time_t now = 0;
        struct tm timeinfo = {0};
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "SNTP time (local): %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900,
                 timeinfo.tm_mon + 1,
                 timeinfo.tm_mday,
                 timeinfo.tm_hour,
                 timeinfo.tm_min,
                 timeinfo.tm_sec);
    } else {
        ESP_LOGW(TAG, "SNTP sync failed (%s)", esp_err_to_name(sync_err));
    }

    return sync_err;
}

void sntp_task(void *pvParameter)
{
    ESP_LOGI(TAG, "SNTP task started");

    /* Set timezone immediately */
    time_utils_init_timezone();

    /* The time is known if we ever synced before, or if the RTC chip restored it at boot */
    bool rtc_time_valid = (xEventGroupGetBits(global_event_group) & IS_RTC_TIME_VALID) != 0;
    if (sntp_check_first_sync_done() || rtc_time_valid) {
        ESP_LOGI(TAG, "Time is known (%s), setting IS_SNTP_FIRST_SYNC_DONE",
                 rtc_time_valid ? "restored from RTC" : "previous sync found");
        xEventGroupSetBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE);
    }

    if (!(xEventGroupGetBits(global_event_group) & IS_WIFI_AVAILABLE)) {
        ESP_LOGI(TAG, "Wi-Fi not available, skipping time sync");
        xEventGroupSetBits(global_event_group, IS_SNTP_SYNC_DONE);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
    xEventGroupWaitBits(global_event_group, IS_WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Wi-Fi connected, syncing time");
    time_t before_sync = 0;
    time(&before_sync);
    if (sync_time_with_sntp() == ESP_OK) {
        time_t now = 0;
        time(&now);
        if (llabs((long long)(now - before_sync)) > TIME_CORRECTION_THRESHOLD_S) {
            ESP_LOGW(TAG, "Clock corrected by %lld seconds", (long long)(now - before_sync));
            xEventGroupSetBits(global_event_group, IS_TIME_CORRECTED);
        }
        sntp_save_last_sync(now);
#ifdef CONFIG_RTC_PCF85063_ENABLED
        rtc_pcf85063_save_system_time();
#endif
        /* Save first sync flag if not already saved */
        if (!sntp_check_first_sync_done()) {
            sntp_save_first_sync_done();
            xEventGroupSetBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE);
        }
    }

    xEventGroupSetBits(global_event_group, IS_SNTP_SYNC_DONE);
    ESP_LOGI(TAG, "SNTP sync done");

    vTaskDelete(NULL);
}
