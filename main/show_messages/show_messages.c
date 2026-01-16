/**
 * @file show_messages.c
 * @brief Show messages on E-Paper display
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs.h>
#include <time.h>
#include <driver/gpio.h>

#include "../global_constants.h"
#include "../global_event_group.h"
#include "../display_epaper/display.h"
#include "../deep_sleep/deep_sleep.h"
#include "show_messages.h"

static const char *TAG = "show_messages";

#define GPIO4_TRIGGER_PIN GPIO_NUM_4
#define GPIO4_DEBOUNCE_MS 200

static volatile bool gpio4_triggered = false;
static TickType_t last_gpio4_press_time = 0;

static void IRAM_ATTR gpio4_isr_handler(void *arg)
{
    TickType_t current_time = xTaskGetTickCountFromISR();
    if ((current_time - last_gpio4_press_time) > pdMS_TO_TICKS(GPIO4_DEBOUNCE_MS)) {
        gpio4_triggered = true;
        last_gpio4_press_time = current_time;
    }
}

static void configure_gpio4_interrupt(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO4_TRIGGER_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  /* Trigger on falling edge (button press) */
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO4_TRIGGER_PIN, gpio4_isr_handler, NULL);

    ESP_LOGI(TAG, "GPIO4 interrupt configured");
}

static const char *NVS_TRIGGER_NAMESPACE = "trigger_info";
static const char *NVS_LAST_GPIO4_KEY = "last_gpio4";

static time_t get_last_gpio4_timestamp(void)
{
    nvs_handle_t nvs_handle;
    time_t last_timestamp = 0;
    size_t required_size = sizeof(last_timestamp);

    esp_err_t err = nvs_open(NVS_TRIGGER_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return 0;
    }

    err = nvs_get_blob(nvs_handle, NVS_LAST_GPIO4_KEY, &last_timestamp, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        return last_timestamp;
    }
    return 0;
}

static void save_last_gpio4_timestamp(time_t timestamp)
{
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open(NVS_TRIGGER_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for writing GPIO4 timestamp");
        return;
    }

    err = nvs_set_blob(nvs_handle, NVS_LAST_GPIO4_KEY, &timestamp, sizeof(timestamp));
    if (err == ESP_OK) {
        nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Last GPIO4 timestamp saved to NVS");
    }
    nvs_close(nvs_handle);
}

static const char* get_days_suffix(int days)
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

static void format_datetime_with_days(char *buf, size_t buf_size, int days_since_trigger, time_t trigger_timestamp)
{
    if (trigger_timestamp != 0) {
        struct tm trigger_timeinfo = {0};
        localtime_r(&trigger_timestamp, &trigger_timeinfo);

        if (days_since_trigger == 0) {
            snprintf(buf, buf_size,
                        "\n %02d-%02d-%04d\n  %02d:%02d:%02d\n  Сьогодні",
                        trigger_timeinfo.tm_mday,
                        trigger_timeinfo.tm_mon + 1,
                        trigger_timeinfo.tm_year + 1900,
                        trigger_timeinfo.tm_hour,
                        trigger_timeinfo.tm_min,
                        trigger_timeinfo.tm_sec);
        } else if (days_since_trigger == 1) {
            snprintf(buf, buf_size,
                        "\n %02d-%02d-%04d\n  %02d:%02d:%02d\n  Учора",
                        trigger_timeinfo.tm_mday,
                        trigger_timeinfo.tm_mon + 1,
                        trigger_timeinfo.tm_year + 1900,
                        trigger_timeinfo.tm_hour,
                        trigger_timeinfo.tm_min,
                        trigger_timeinfo.tm_sec);
        } else {
            snprintf(buf, buf_size,
                        "\n %02d-%02d-%04d\n  %02d:%02d:%02d\n  %d %s тому",
                        trigger_timeinfo.tm_mday,
                        trigger_timeinfo.tm_mon + 1,
                        trigger_timeinfo.tm_year + 1900,
                        trigger_timeinfo.tm_hour,
                        trigger_timeinfo.tm_min,
                        trigger_timeinfo.tm_sec,
                        days_since_trigger,
                        get_days_suffix(days_since_trigger));
        }
    } else {
        /* No trigger record - show current time without days */
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
}

static void handle_gpio4_trigger_and_update_display(char *datetime_str, size_t buf_size)
{
    time_t now = 0;
    time(&now);

    /* Save new timestamp */
    save_last_gpio4_timestamp(now);
    ESP_LOGI(TAG, "GPIO4 pressed while awake: saved new timestamp %ld", (long)now);

    /* Wake up display and update */
    display_wake();

    /* Format and display */
    format_datetime_with_days(datetime_str, buf_size, 0, now);

    display_clear();
    display_draw_text(0, 0, datetime_str, 0);

    if (display_update() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display after GPIO4 press");
    } else {
        ESP_LOGI(TAG, "Display updated after GPIO4 press: %s", datetime_str);
    }

    display_sleep();
}

static int calculate_days_since_trigger(time_t now)
{
    time_t last_trigger = get_last_gpio4_timestamp();
    if (last_trigger == 0 || now <= last_trigger) {
        return 0;
    }

    /* Calculate difference in calendar days (not 24-hour periods) */
    struct tm now_tm = {0};
    struct tm trigger_tm = {0};
    localtime_r(&now, &now_tm);
    localtime_r(&last_trigger, &trigger_tm);

    /* Convert both dates to days since epoch (ignoring time of day) */
    /* Create time_t for midnight of each day */
    struct tm now_midnight = now_tm;
    now_midnight.tm_hour = 0;
    now_midnight.tm_min = 0;
    now_midnight.tm_sec = 0;

    struct tm trigger_midnight = trigger_tm;
    trigger_midnight.tm_hour = 0;
    trigger_midnight.tm_min = 0;
    trigger_midnight.tm_sec = 0;

    time_t now_day = mktime(&now_midnight);
    time_t trigger_day = mktime(&trigger_midnight);

    /* Calculate difference in days */
    int days = (int)((now_day - trigger_day) / (24 * 60 * 60));
    return days;
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
    EventBits_t bits = xEventGroupWaitBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE | IS_GPIO4_WAKEUP, pdFALSE, pdFALSE, check_timeout);

    bool is_gpio4_wakeup = (bits & IS_GPIO4_WAKEUP) != 0;

    time_t now = 0;
    time(&now);

    /* Check if we've synced before AND the RTC has a valid time (year > 2020) */
    struct tm timeinfo = {0};
    gmtime_r(&now, &timeinfo);

    /* Calculate days since last GPIO4 trigger */
    int days_since_trigger = 0;
    time_t trigger_timestamp = 0;
    bool valid_time = (bits & IS_SNTP_FIRST_SYNC_DONE) && (timeinfo.tm_year + 1900 > 2020);

    /* If GPIO4 triggered the wake-up and we have valid time, save the timestamp first */
    if (is_gpio4_wakeup && valid_time) {
        save_last_gpio4_timestamp(now);
        trigger_timestamp = now;
        days_since_trigger = 0;
        ESP_LOGI(TAG, "GPIO4 wake-up: saved new timestamp %ld", (long)trigger_timestamp);
    } else if (valid_time) {
        days_since_trigger = calculate_days_since_trigger(now);
        trigger_timestamp = get_last_gpio4_timestamp();
        ESP_LOGI(TAG, "Days since last GPIO4 trigger: %d (trigger_ts: %ld)", days_since_trigger, (long)trigger_timestamp);
    }

    if (valid_time) {
        /* We've synced before and RTC has valid time - use it immediately */
        ESP_LOGI(TAG, "First sync was done before, using saved time immediately");
        format_datetime_with_days(datetime_str, sizeof(datetime_str), days_since_trigger, trigger_timestamp);
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

        /* Recalculate now that we have valid time */
        time(&now);

        /* If GPIO4 triggered the wake-up, save the timestamp now */
        if (is_gpio4_wakeup) {
            save_last_gpio4_timestamp(now);
            trigger_timestamp = now;
            days_since_trigger = 0;
            ESP_LOGI(TAG, "GPIO4 wake-up: saved new timestamp after SNTP sync %ld", (long)trigger_timestamp);
        } else {
            days_since_trigger = calculate_days_since_trigger(now);
            trigger_timestamp = get_last_gpio4_timestamp();
            ESP_LOGI(TAG, "Days since last GPIO4 trigger: %d (trigger_ts: %ld)", days_since_trigger, (long)trigger_timestamp);
        }

        /* Get the synced time with days */
        format_datetime_with_days(datetime_str, sizeof(datetime_str), days_since_trigger, trigger_timestamp);

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

    /* Configure GPIO4 interrupt to detect button presses while awake */
    configure_gpio4_interrupt();

    /* Wait for OTA check and SNTP sync to complete, polling for GPIO4 presses */
    ESP_LOGI(TAG, "Waiting for OTA check and SNTP sync to complete (1 min timeout)...");
    const TickType_t poll_interval = pdMS_TO_TICKS(100);
    const TickType_t max_wait_time = pdMS_TO_TICKS(60000);
    TickType_t elapsed_time = 0;
    EventBits_t wait_bits = 0;

    while (elapsed_time < max_wait_time) {
        /* Check for GPIO4 press */
        if (gpio4_triggered) {
            gpio4_triggered = false;
            handle_gpio4_trigger_and_update_display(datetime_str, sizeof(datetime_str));
        }

        /* Check if OTA and SNTP are done */
        wait_bits = xEventGroupWaitBits(global_event_group, IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE, pdFALSE, pdTRUE, poll_interval);
        if ((wait_bits & (IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE)) == (IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE)) {
            break;
        }

        elapsed_time += poll_interval;
    }

    /* If OTA update is running, wait for it to complete while still checking GPIO4 */
    if (wait_bits & IS_OTA_UPDATE_RUNNING) {
        ESP_LOGI(TAG, "OTA update in progress, waiting for it to complete...");
        while (!(xEventGroupGetBits(global_event_group) & IS_OTA_CHECK_DONE)) {
            if (gpio4_triggered) {
                gpio4_triggered = false;
                handle_gpio4_trigger_and_update_display(datetime_str, sizeof(datetime_str));
            }
            vTaskDelay(poll_interval);
        }
    }

    /* Remove GPIO4 interrupt handler before entering deep sleep */
    gpio_isr_handler_remove(GPIO4_TRIGGER_PIN);

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
