/**
 * @file show_messages.c
 * @brief Show messages on E-Paper display
 *
 * Screen layout (200x200 logical pixels):
 *
 *   [battery icon] 78%            22.4°C 55%     <- status bar (1x font)
 *
 *                     12                         <- days since the last change (4x font)
 *                  днів тому                     <- caption (1x), or "Сьогодні" / "Учора" at 2x
 *
 *              09-10-2026 14:32                  <- date and time of the last change (1x)
 *
 *                 [ big battery ]                <- only when the battery is low
 *
 * The content block is centered in the area between the status bar and the
 * (optional) low-battery icon.
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../global_constants.h"
#include "../global_event_group.h"
#include "../display_epaper/display.h"
#include "../deep_sleep/deep_sleep.h"
#include "../trigger/trigger.h"
#include "../time_utils/time_utils.h"
#include "../battery_level/battery_level.h"
#include "../battery_log/battery_log.h"
#include "../board_power/board_power.h"
#include "../led/led.h"
#ifdef CONFIG_SHTC3_ENABLED
#include "../shtc3/shtc3.h"
#endif
#include "show_messages.h"

static const char *TAG = "show_messages";

#define TEXT_BUF_SIZE 48

/* Layout constants (logical pixels) */
#define STATUS_BAR_Y        2
#define STATUS_BAR_MARGIN_X 4
#define STATUS_BAR_HEIGHT   (FONT_CHAR_HEIGHT + FONT_LINE_SPACING)
#define CONTENT_TOP         (STATUS_BAR_Y + STATUS_BAR_HEIGHT + 4)
#define DAYS_NUMBER_SCALE   4
#define DAYS_NUMBER_SCALE_COMPACT 3 /* used when the low-battery icon takes the bottom of the screen */
#define DAYS_WORD_SCALE     2
#define ROW_GAP_SMALL       2
#define ROW_GAP_LARGE       SCREEN_SECTION_GAP

/* Battery icon geometry (status bar) */
#define BATTERY_ICON_WIDTH  22
#define BATTERY_ICON_HEIGHT 10
#define BATTERY_ICON_NUB    2
#define BATTERY_ICON_BORDER 1

/* Large low-battery icon at the bottom of the screen */
#define LOW_BATTERY_ICON_WIDTH  60
#define LOW_BATTERY_ICON_HEIGHT 26
#define LOW_BATTERY_ICON_NUB    5
#define LOW_BATTERY_ICON_BORDER 3
#define LOW_BATTERY_ICON_MARGIN 5

#define FIRST_SYNC_WAIT_MS  90000

typedef struct {
    const char *text;
    int scale;
    int gap_after;
} screen_row_t;

static struct {
    int battery_percent;   /* -1 if unavailable */
    int battery_mv;        /* 0 if unavailable */
    bool battery_low;
#ifdef CONFIG_SHTC3_ENABLED
    bool climate_valid;
    shtc3_reading_t climate;
#endif
} s_sensors = { .battery_percent = -1 };

/* Sensors are read once per wake-up and reused for every render */
static void read_sensors(void)
{
    s_sensors.battery_percent = battery_level_read_percent(&s_sensors.battery_mv);
    if (s_sensors.battery_percent < 0) {
        s_sensors.battery_mv = 0;
    }
#ifdef CONFIG_IS_BATTERY_LEVEL_ENABLED
    s_sensors.battery_low = (s_sensors.battery_percent >= 0 && s_sensors.battery_percent <= CONFIG_BATTERY_LOW_PERCENT);
#endif
#ifdef CONFIG_SHTC3_ENABLED
    s_sensors.climate_valid = (shtc3_read(&s_sensors.climate) == ESP_OK);
#endif
}

/* Battery outline with a white interior, a nub on the right, and a fill proportional to the charge */
static void draw_battery_icon(int x, int y, int width, int height, int nub, int border, int percent)
{
    display_fill_rect(x, y, width, height, 0);
    display_fill_rect(x + border, y + border, width - 2 * border, height - 2 * border, 1);
    display_fill_rect(x + width, y + height / 3, nub, height - 2 * (height / 3), 0);

    int gap = border + 1;
    int inner_width = width - 2 * gap;
    int fill_width = inner_width * percent / 100;
    if (fill_width > 0) {
        display_fill_rect(x + gap, y + gap, fill_width, height - 2 * gap, 0);
    }
}

/* Height reserved at the bottom of the screen for the low-battery icon (0 if not shown) */
static int low_battery_area_height(void)
{
    return s_sensors.battery_low ? (LOW_BATTERY_ICON_HEIGHT + 2 * LOW_BATTERY_ICON_MARGIN) : 0;
}

static void draw_low_battery_icon(void)
{
    if (!s_sensors.battery_low) {
        return;
    }
    int x = (display_get_width() - (LOW_BATTERY_ICON_WIDTH + LOW_BATTERY_ICON_NUB)) / 2;
    int y = display_get_height() - LOW_BATTERY_ICON_MARGIN - LOW_BATTERY_ICON_HEIGHT;
    draw_battery_icon(x, y, LOW_BATTERY_ICON_WIDTH, LOW_BATTERY_ICON_HEIGHT,
                      LOW_BATTERY_ICON_NUB, LOW_BATTERY_ICON_BORDER, s_sensors.battery_percent);
}

static void draw_status_bar(void)
{
    char buf[TEXT_BUF_SIZE];

    if (s_sensors.battery_percent >= 0) {
        int icon_y = STATUS_BAR_Y + (FONT_CHAR_HEIGHT - BATTERY_ICON_HEIGHT) / 2;
        draw_battery_icon(STATUS_BAR_MARGIN_X, icon_y, BATTERY_ICON_WIDTH, BATTERY_ICON_HEIGHT,
                          BATTERY_ICON_NUB, BATTERY_ICON_BORDER, s_sensors.battery_percent);
        snprintf(buf, sizeof(buf), "%d%%", s_sensors.battery_percent);
        display_draw_text(STATUS_BAR_MARGIN_X + BATTERY_ICON_WIDTH + BATTERY_ICON_NUB + 4, STATUS_BAR_Y, buf, 0, 1);
    }

#ifdef CONFIG_SHTC3_ENABLED
    if (s_sensors.climate_valid) {
        int t = s_sensors.climate.temperature_x10;
        snprintf(buf, sizeof(buf), "%s%d.%d°C %d%%",
                 (t < 0) ? "-" : "", abs(t) / 10, abs(t) % 10, s_sensors.climate.humidity);
        int width = display_measure_text(buf, 1);
        display_draw_text(display_get_width() - STATUS_BAR_MARGIN_X - width, STATUS_BAR_Y, buf, 0, 1);
    }
#endif
}

/* Draw the status bar and the given rows centered in the remaining area, then refresh */
static esp_err_t render_rows(const screen_row_t *rows, int row_count)
{
    display_clear();
    draw_status_bar();
    draw_low_battery_icon();

    int total_height = 0;
    for (int i = 0; i < row_count; i++) {
        if (rows[i].text == NULL || *rows[i].text == '\0') {
            continue;
        }
        total_height += display_text_height(rows[i].text, rows[i].scale);
        if (i < row_count - 1) {
            total_height += rows[i].gap_after;
        }
    }

    int area_height = display_get_height() - CONTENT_TOP - low_battery_area_height();
    int y = CONTENT_TOP + (area_height - total_height) / 2;
    if (y < CONTENT_TOP) {
        y = CONTENT_TOP;
    }

    for (int i = 0; i < row_count; i++) {
        if (rows[i].text == NULL || *rows[i].text == '\0') {
            continue;
        }
        display_draw_text_centered(y, rows[i].text, 0, rows[i].scale);
        y += display_text_height(rows[i].text, rows[i].scale) + rows[i].gap_after;
        ESP_LOGI(TAG, "  row: %s", rows[i].text);
    }

    esp_err_t err = display_update();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display");
    } else {
        ESP_LOGI(TAG, "Display updated");
    }
    return err;
}

/* Days since the last change plus its date/time */
static esp_err_t render_days_screen(int days_since_trigger, time_t trigger_timestamp)
{
    char number[TEXT_BUF_SIZE];
    char caption[TEXT_BUF_SIZE];
    char datetime[TEXT_BUF_SIZE];

    trigger_format_datetime(datetime, sizeof(datetime), trigger_timestamp);

    if (trigger_timestamp == 0) {
        /* No record of a change yet: show the current time only */
        const screen_row_t rows[] = {
            { datetime, 1, 0 },
        };
        return render_rows(rows, 1);
    }

    if (days_since_trigger == 0 || days_since_trigger == 1) {
        const screen_row_t rows[] = {
            { (days_since_trigger == 0) ? "Сьогодні" : "Учора", DAYS_WORD_SCALE, ROW_GAP_LARGE },
            { datetime, 1, 0 },
        };
        return render_rows(rows, 2);
    }

    snprintf(number, sizeof(number), "%d", days_since_trigger);
    trigger_format_days_caption(caption, sizeof(caption), days_since_trigger);
    int number_scale = s_sensors.battery_low ? DAYS_NUMBER_SCALE_COMPACT : DAYS_NUMBER_SCALE;
    const screen_row_t rows[] = {
        { number, number_scale, ROW_GAP_SMALL },
        { caption, 1, ROW_GAP_LARGE },
        { datetime, 1, 0 },
    };
    return render_rows(rows, 3);
}

static esp_err_t render_message_screen(const char *message)
{
    const screen_row_t rows[] = {
        { message, 1, 0 },
    };
    return render_rows(rows, 1);
}

#ifdef CONFIG_SCREEN_DEMO
/* Cycle through every screen variant so the layout can be checked on the panel */
static void run_screen_demo(void)
{
    const int day_counts[] = { 0, 1, 2, 5, 21, 365 };
    time_t now = 0;
    time(&now);

    ESP_LOGW(TAG, "SCREEN DEMO: cycling through all screen variants");
    for (size_t i = 0; i < sizeof(day_counts) / sizeof(day_counts[0]); i++) {
        render_days_screen(day_counts[i], now - (time_t)day_counts[i] * 86400);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SCREEN_DEMO_HOLD_MS));
    }
    render_message_screen("Підключаю Wi-Fi\nдля отримання\nчасу");
    vTaskDelay(pdMS_TO_TICKS(CONFIG_SCREEN_DEMO_HOLD_MS));
    render_days_screen(0, 0);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_SCREEN_DEMO_HOLD_MS));

    /* Low battery: simulated 12 %, with the fast LED warning */
    int saved_percent = s_sensors.battery_percent;
    bool saved_low = s_sensors.battery_low;
    s_sensors.battery_percent = 12;
    s_sensors.battery_low = true;
    led_play(LED_PATTERN_LOW_BATTERY);
    render_days_screen(5, now - 5 * 86400);
    vTaskDelay(pdMS_TO_TICKS(2 * CONFIG_SCREEN_DEMO_HOLD_MS));
    led_wait_done();
    s_sensors.battery_percent = saved_percent;
    s_sensors.battery_low = saved_low;

    ESP_LOGW(TAG, "SCREEN DEMO: done");
}
#endif

static void handle_trigger_press(void)
{
    time_t now = 0;
    time(&now);

    trigger_save_timestamp(now);
    ESP_LOGI(TAG, "Trigger pressed: saved timestamp %ld", (long)now);
    led_play(LED_PATTERN_CHANGE_RECORDED);

    display_wake();
    render_days_screen(0, now);
    display_sleep();
}

static void get_trigger_info(bool is_reset_wakeup, time_t now, int *days_since, time_t *timestamp)
{
    if (is_reset_wakeup) {
        trigger_save_timestamp(now);
        *timestamp = now;
        *days_since = 0;
        ESP_LOGI(TAG, "Reset button wake-up: saved timestamp %ld", (long)*timestamp);
    } else {
        *timestamp = trigger_get_last_timestamp();
        *days_since = time_utils_days_between(*timestamp, now);
        ESP_LOGI(TAG, "Days since trigger: %d (ts: %ld)", *days_since, (long)*timestamp);
    }
}

static esp_err_t show_trigger_state(bool is_reset_wakeup, time_t now)
{
    int days_since_trigger = 0;
    time_t trigger_timestamp = 0;
    get_trigger_info(is_reset_wakeup, now, &days_since_trigger, &trigger_timestamp);
    return render_days_screen(days_since_trigger, trigger_timestamp);
}

void show_messages_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Show messages task started");

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        deep_sleep_fallback("display init failed");
    }

    read_sensors();

    /* Battery history: one entry per wake-up, printed at boot for drain measurements */
    uint8_t log_flags = 0;
    if (board_power_latch_is_weak()) {
        log_flags |= BATTERY_LOG_FLAG_WEAK_LATCH;
    }
    if (xEventGroupGetBits(global_event_group) & IS_WIFI_AVAILABLE) {
        log_flags |= BATTERY_LOG_FLAG_WIFI;
    }
    battery_log_record(s_sensors.battery_mv, log_flags);

#ifdef CONFIG_SCREEN_DEMO
    run_screen_demo();
#endif

    bool first_boot = false;

    /* Check if the time is already known (previous SNTP sync or RTC chip) */
    const TickType_t check_timeout = pdMS_TO_TICKS(100);
    EventBits_t bits = xEventGroupWaitBits(global_event_group,
                                           IS_SNTP_FIRST_SYNC_DONE | IS_RESET_BUTTON_WAKEUP,
                                           pdFALSE, pdFALSE, check_timeout);

    bool is_reset_wakeup = (bits & IS_RESET_BUTTON_WAKEUP) != 0;
    bool valid_time = (bits & IS_SNTP_FIRST_SYNC_DONE) && time_utils_is_valid();

    /* LED feedback (background task): recorded change, sync request, low battery warning */
    if (is_reset_wakeup) {
        led_play(LED_PATTERN_CHANGE_RECORDED);
    } else if (xEventGroupGetBits(global_event_group) & IS_SYNC_REQUESTED) {
        led_play(LED_PATTERN_SYNC_REQUESTED);
    }
    if (s_sensors.battery_low) {
        ESP_LOGW(TAG, "Battery low (%d%%)", s_sensors.battery_percent);
        led_play(LED_PATTERN_LOW_BATTERY);
    }

    time_t now = 0;
    time(&now);

    esp_err_t err;
    if (valid_time) {
        err = show_trigger_state(is_reset_wakeup, now);
    } else {
        ESP_LOGI(TAG, "First boot, showing connecting message");
        err = render_message_screen("Підключаю Wi-Fi\nдля отримання\nчасу");
        first_boot = true;
    }

    if (err != ESP_OK) {
        display_deinit();
        deep_sleep_fallback("display refresh failed");
    }

    /* On first boot, show saved date immediately, then update after SNTP sync */
    if (first_boot) {
        /* Show the saved trigger date/time without relative days (time not synced yet) */
        time_t trigger_timestamp = trigger_get_last_timestamp();
        if (trigger_timestamp != 0) {
            char datetime[TEXT_BUF_SIZE];
            trigger_format_datetime(datetime, sizeof(datetime), trigger_timestamp);
            render_message_screen(datetime);
        }

        ESP_LOGI(TAG, "Waiting for SNTP sync...");
        EventBits_t sync_bits = xEventGroupWaitBits(global_event_group, IS_SNTP_SYNC_DONE,
                                                    pdFALSE, pdTRUE, pdMS_TO_TICKS(FIRST_SYNC_WAIT_MS));
        if (!(sync_bits & IS_SNTP_SYNC_DONE)) {
            ESP_LOGW(TAG, "Timed out waiting for the first time sync");
        }

        time(&now);
        if (time_utils_is_valid()) {
            show_trigger_state(is_reset_wakeup, now);
        } else {
            ESP_LOGW(TAG, "Time still invalid after waiting for SNTP, skipping days display");
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
            handle_trigger_press();
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
                handle_trigger_press();
            }
            vTaskDelay(poll_interval);
        }
    }

    /* If SNTP moved the clock substantially (e.g. the RTC held a stale time), the
     * screen and a timestamp saved on this wake-up were based on the wrong time */
    if (!first_boot && (xEventGroupGetBits(global_event_group) & IS_TIME_CORRECTED)) {
        ESP_LOGI(TAG, "Clock was corrected, refreshing the screen");
        time(&now);
        display_wake();
        show_trigger_state(is_reset_wakeup, now);
        display_sleep();
    }

    trigger_deinit_interrupt();

    ESP_LOGI(TAG, "Entering deep sleep");

    if (deep_sleep_configure_wakeup() != ESP_OK) {
        deep_sleep_fallback("wake-up configuration failed");
    }

    deep_sleep_enter();
    vTaskDelete(NULL);
}
