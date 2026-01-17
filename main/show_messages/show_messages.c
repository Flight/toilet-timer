/**
 * @file show_messages.c
 * @brief Show messages on E-Paper display
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <time.h>

#include "../global_event_group.h"
#include "../display_epaper/display.h"
#include "../deep_sleep/deep_sleep.h"
#include "../trigger/trigger.h"
#include "../time_utils/time_utils.h"
#include "show_messages.h"

static const char *TAG = "show_messages";

static void handle_trigger_press(char *datetime_str, size_t buf_size)
{
    time_t now = 0;
    time(&now);

    trigger_save_timestamp(now);
    ESP_LOGI(TAG, "Trigger pressed: saved timestamp %ld", (long)now);

    display_wake();
    trigger_format_datetime(datetime_str, buf_size, 0, now);
    display_clear();
    display_draw_text(0, 0, datetime_str, 0);

    if (display_update() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display after trigger press");
    } else {
        ESP_LOGI(TAG, "Display updated: %s", datetime_str);
    }

    display_sleep();
}

static void get_trigger_info(bool is_gpio4_wakeup, time_t now, int *days_since, time_t *timestamp)
{
    if (is_gpio4_wakeup) {
        trigger_save_timestamp(now);
        *timestamp = now;
        *days_since = 0;
        ESP_LOGI(TAG, "GPIO4 wake-up: saved timestamp %ld", (long)*timestamp);
    } else {
        *timestamp = trigger_get_last_timestamp();
        *days_since = time_utils_days_between(*timestamp, now);
        ESP_LOGI(TAG, "Days since trigger: %d (ts: %ld)", *days_since, (long)*timestamp);
    }
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

    /* Check if SNTP has synced before */
    const TickType_t check_timeout = pdMS_TO_TICKS(100);
    EventBits_t bits = xEventGroupWaitBits(global_event_group,
                                           IS_SNTP_FIRST_SYNC_DONE | IS_GPIO4_WAKEUP,
                                           pdFALSE, pdFALSE, check_timeout);

    bool is_gpio4_wakeup = (bits & IS_GPIO4_WAKEUP) != 0;
    bool valid_time = (bits & IS_SNTP_FIRST_SYNC_DONE) && time_utils_is_valid();

    time_t now = 0;
    time(&now);

    int days_since_trigger = 0;
    time_t trigger_timestamp = 0;

    if (valid_time) {
        get_trigger_info(is_gpio4_wakeup, now, &days_since_trigger, &trigger_timestamp);
        trigger_format_datetime(datetime_str, sizeof(datetime_str), days_since_trigger, trigger_timestamp);
    } else {
        ESP_LOGI(TAG, "First boot, showing connecting message");
        snprintf(datetime_str, sizeof(datetime_str), " Підключаю\n Wi-Fi для\n отримання\n часу");
        first_boot = true;
    }

    display_clear();
    display_draw_text(0, 0, datetime_str, 0);

    if (display_update() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display");
        display_deinit();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Display updated: %s", datetime_str);

    /* On first boot, wait for SNTP sync */
    if (first_boot) {
        ESP_LOGI(TAG, "Waiting for SNTP sync...");
        xEventGroupWaitBits(global_event_group, IS_SNTP_SYNC_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

        time(&now);
        get_trigger_info(is_gpio4_wakeup, now, &days_since_trigger, &trigger_timestamp);
        trigger_format_datetime(datetime_str, sizeof(datetime_str), days_since_trigger, trigger_timestamp);

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

    /* Configure trigger interrupt for button presses while awake */
    trigger_init_interrupt();

    /* Wait for OTA and SNTP to complete, polling for trigger presses */
    ESP_LOGI(TAG, "Waiting for OTA and SNTP (1 min timeout)...");
    const TickType_t poll_interval = pdMS_TO_TICKS(100);
    const TickType_t max_wait = pdMS_TO_TICKS(60000);
    TickType_t elapsed = 0;
    EventBits_t wait_bits = 0;

    while (elapsed < max_wait) {
        if (trigger_check_and_clear()) {
            handle_trigger_press(datetime_str, sizeof(datetime_str));
        }

        wait_bits = xEventGroupWaitBits(global_event_group,
                                        IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE,
                                        pdFALSE, pdTRUE, poll_interval);

        if ((wait_bits & (IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE)) == (IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE)) {
            break;
        }

        elapsed += poll_interval;
    }

    /* If OTA update is running, wait for completion */
    if (wait_bits & IS_OTA_UPDATE_RUNNING) {
        ESP_LOGI(TAG, "OTA update in progress, waiting...");
        while (!(xEventGroupGetBits(global_event_group) & IS_OTA_CHECK_DONE)) {
            if (trigger_check_and_clear()) {
                handle_trigger_press(datetime_str, sizeof(datetime_str));
            }
            vTaskDelay(poll_interval);
        }
    }

    trigger_deinit_interrupt();

    ESP_LOGI(TAG, "Entering deep sleep");

    if (deep_sleep_configure_wakeup() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure deep sleep");
        vTaskDelete(NULL);
    }

    deep_sleep_enter();
    vTaskDelete(NULL);
}
