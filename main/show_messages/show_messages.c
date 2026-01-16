/**
 * @file show_messages.c
 * @brief Show messages on E-Paper display
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <time.h>

#include "../global_constants.h"
#include "../global_event_group.h"
#include "../display_epaper/display.h"
#include "../deep_sleep/deep_sleep.h"
#include "show_messages.h"

static const char *TAG = "show_messages";

static void format_datetime(char *buf, size_t buf_size)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(buf, buf_size,
                "\n %02d-%02d-%04d\n  %02d:%02d:%02d",
                timeinfo.tm_mday,
                timeinfo.tm_mon + 1,
                timeinfo.tm_year + 1900,
                timeinfo.tm_hour,
                timeinfo.tm_min,
                timeinfo.tm_sec);
}

void show_messages_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Show messages task started");

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        vTaskDelete(NULL);
    }

    char datetime_str[64];
    bool first_boot = false;

    /* Wait briefly for SNTP task to check NVS and set IS_SNTP_FIRST_SYNC_DONE if applicable */
    ESP_LOGI(TAG, "Checking if first SNTP sync was done before...");
    const TickType_t check_timeout = pdMS_TO_TICKS(100);
    EventBits_t bits = xEventGroupWaitBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE, pdFALSE, pdTRUE, check_timeout);

    time_t now = 0;
    time(&now);

    /* Check if we've synced before AND the RTC has a valid time (year > 2020) */
    struct tm timeinfo = {0};
    gmtime_r(&now, &timeinfo);

    if ((bits & IS_SNTP_FIRST_SYNC_DONE) && (timeinfo.tm_year + 1900 > 2020)) {
        /* We've synced before and RTC has valid time - use it immediately */
        ESP_LOGI(TAG, "First sync was done before, using saved time immediately");
        format_datetime(datetime_str, sizeof(datetime_str));
    } else {
        /* First boot or RTC time invalid - show connecting message */
        ESP_LOGI(TAG, "First boot or invalid RTC time, showing connecting message");
        snprintf(datetime_str, sizeof(datetime_str),
                    " Підключаю\n Wi-Fi для\n отримання\n часу");
        first_boot = true;
    }

    /* Clear framebuffer */
    display_clear();

    display_draw_text(0, 0, datetime_str, 0);

    /* Update physical display */
    if (display_update() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display");
        display_deinit();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Display updated: %s", datetime_str);

    /* On first boot, wait for SNTP sync and update display with actual time */
    if (first_boot) {
        ESP_LOGI(TAG, "Waiting for SNTP sync to show actual time...");
        xEventGroupWaitBits(global_event_group, IS_SNTP_SYNC_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

        /* Get the synced time */
        format_datetime(datetime_str, sizeof(datetime_str));

        /* Update display with actual time */
        display_clear();
        display_draw_text(0, 0, datetime_str, 0);

        if (display_update() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update display with time");
        } else {
            ESP_LOGI(TAG, "Display updated with synced time: %s", datetime_str);
        }
    }

    display_sleep();

    ESP_LOGI(TAG, "Display sequence completed");

    /* Wait for OTA check and SNTP sync to complete, with 1 minute timeout */
    ESP_LOGI(TAG, "Waiting for OTA check and SNTP sync to complete (1 min timeout)...");
    const TickType_t timeout_ticks = pdMS_TO_TICKS(60000);
    EventBits_t wait_bits = xEventGroupWaitBits(global_event_group, IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE, pdFALSE, pdTRUE, timeout_ticks);

    /* If OTA update is running, wait indefinitely for it to complete */
    if (wait_bits & IS_OTA_UPDATE_RUNNING) {
        ESP_LOGI(TAG, "OTA update in progress, waiting for it to complete...");
        xEventGroupWaitBits(global_event_group, IS_OTA_CHECK_DONE, pdFALSE, pdTRUE, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Entering deep sleep");

    /* Configure deep sleep wake-up */
    if (deep_sleep_configure_wakeup() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure deep sleep wake-up");
        vTaskDelete(NULL);
    }

    /* Enter deep sleep - device will restart on wake-up */
    deep_sleep_enter();

    vTaskDelete(NULL);
}
