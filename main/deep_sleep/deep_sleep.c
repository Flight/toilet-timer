/**
 * @file deep_sleep.c
 * @brief Deep sleep management with EXT1 wake-up and timer wake-up
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <time.h>
#include <stdlib.h>

#include "deep_sleep.h"

static const char *TAG = "deep_sleep";

/* Wake-up GPIOs: GPIO0, GPIO3, GPIO4 */
#define WAKEUP_GPIO_MASK ((1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_4))

/* Calculate microseconds until next midnight */
static uint64_t calculate_us_until_midnight(void)
{
    /* Ensure timezone is set for correct local time calculation */
    setenv("TZ", CONFIG_SNTP_TIMEZONE, 1);
    tzset();

    time_t now = 0;
    time(&now);

    struct tm now_tm = {0};
    localtime_r(&now, &now_tm);

    /* Create time for next midnight */
    struct tm midnight_tm = now_tm;
    midnight_tm.tm_hour = 0;
    midnight_tm.tm_min = 0;
    midnight_tm.tm_sec = 0;
    midnight_tm.tm_mday += 1;  /* Next day */

    time_t midnight = mktime(&midnight_tm);

    /* Calculate seconds until midnight */
    int64_t seconds_until_midnight = (int64_t)(midnight - now);

    /* Ensure we have at least 1 minute (in case of timing edge cases) */
    if (seconds_until_midnight < 60) {
        seconds_until_midnight = 60;
    }

    ESP_LOGI(TAG, "Seconds until midnight: %lld", seconds_until_midnight);

    return (uint64_t)seconds_until_midnight * 1000000ULL;
}

esp_err_t deep_sleep_configure_wakeup(void)
{
    ESP_LOGI(TAG, "Configuring deep sleep wake-up sources");

    /* Configure EXT1 wake-up (multiple RTC GPIOs with level trigger) */
    /* ESP_EXT1_WAKEUP_ANY_LOW: wake up when ANY of the pins is LOW */
    esp_err_t err = esp_sleep_enable_ext1_wakeup(WAKEUP_GPIO_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure EXT1 wake-up: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure GPIO pull-ups to ensure stable HIGH state when not pressed */
    /* Assuming buttons connect GPIO to GND when pressed */
    gpio_config_t io_conf = {
        .pin_bit_mask = WAKEUP_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Wake-up configured: GPIO0, GPIO3, GPIO4 (active LOW)");

    /* Configure timer wake-up for next midnight */
    uint64_t us_until_midnight = calculate_us_until_midnight();
    err = esp_sleep_enable_timer_wakeup(us_until_midnight);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure timer wake-up: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Timer wake-up configured for next midnight");

    return ESP_OK;
}

void deep_sleep_enter(void)
{
    ESP_LOGI(TAG, "Entering deep sleep mode...");
    ESP_LOGI(TAG, "Device will wake up when GPIO0, GPIO3, or GPIO4 is pulled LOW, or at midnight");

    /* Small delay to allow log to flush */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Enter deep sleep - this function does not return */
    esp_deep_sleep_start();
}
