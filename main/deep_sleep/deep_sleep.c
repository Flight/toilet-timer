/**
 * @file deep_sleep.c
 * @brief Deep sleep management with button, RTC alarm and timer wake-up
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <time.h>

#include "deep_sleep.h"
#include "../global_constants.h"
#include "../global_event_group.h"
#include "../time_utils/time_utils.h"
#include "../board_power/board_power.h"
#include "../trigger/trigger.h"
#include "../led/led.h"
#ifdef CONFIG_RTC_PCF85063_ENABLED
#include "../rtc_pcf85063/rtc_pcf85063.h"
#endif

static const char *TAG = "deep_sleep";

#define WAKEUP_BUTTON_MASK TRIGGER_BUTTON_MASK

#define FALLBACK_RETRY_HOURS  1    /* Last-resort timer when even the wake-up configuration failed */
#define AWAKE_WATCHDOG_STACK  4096

/* Input with pull-up that stays active in deep sleep (all wake pins are RTC GPIOs) */
static esp_err_t configure_wakeup_pin(gpio_num_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    rtc_gpio_pulldown_dis(pin);
    return rtc_gpio_pullup_en(pin);
}

esp_err_t deep_sleep_configure_wakeup(void)
{
    ESP_LOGI(TAG, "Configuring deep sleep wake-up sources");

    uint64_t wakeup_mask = WAKEUP_BUTTON_MASK;
    uint64_t us_until_timer = 0;

#ifdef CONFIG_RTC_PCF85063_ENABLED
    /* The crystal-driven RTC alarm is the nightly wake-up. The ESP32 timer
     * (RC oscillator, drifts ~2%) only backs up the alarm it was armed with:
     * it is set a couple of hours after that alarm, so with a working RTC
     * there is one wake-up per night and the fallback is simply re-armed. */
    time_t now = 0;
    time(&now);
    time_t alarm_at = 0;
    bool alarm_armed = false;
#if CONFIG_RTC_ALARM_TEST_MINUTES > 0
    alarm_at = now + CONFIG_RTC_ALARM_TEST_MINUTES * 60;
    ESP_LOGW(TAG, "TEST MODE: RTC alarm in %d minutes instead of the nightly refresh", CONFIG_RTC_ALARM_TEST_MINUTES);
    alarm_armed = (rtc_pcf85063_arm_alarm_at(alarm_at) == ESP_OK);
#else
    alarm_armed = (rtc_pcf85063_arm_daily_alarm(DAILY_WAKEUP_HOUR, 0, &alarm_at) == ESP_OK);
#endif
    if (alarm_armed) {
        wakeup_mask |= (1ULL << CONFIG_RTC_INT_GPIO);
        int64_t fallback_s = (int64_t)(alarm_at - now) + (int64_t)DAILY_WAKEUP_FALLBACK_DELAY_HOURS * 3600;
        if (fallback_s < 60) {
            fallback_s = 60;
        }
        us_until_timer = (uint64_t)fallback_s * 1000000ULL;
        ESP_LOGI(TAG, "RTC alarm wake-up in %lld s, timer fallback %d h after it",
                 (long long)(alarm_at - now), DAILY_WAKEUP_FALLBACK_DELAY_HOURS);
    } else {
        ESP_LOGW(TAG, "RTC alarm not available, using the timer wake-up at %02d:00", DAILY_WAKEUP_HOUR);
    }
#endif

    if (us_until_timer == 0) {
        us_until_timer = time_utils_us_until_hour(DAILY_WAKEUP_HOUR);
    }

    /* Configure EXT1 wake-up (RTC GPIOs, any LOW) */
    esp_err_t err = esp_sleep_enable_ext1_wakeup_io(wakeup_mask, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure EXT1 wake-up: %s", esp_err_to_name(err));
        return err;
    }

    for (int pin = 0; pin < 64; pin++) {
        if (!(wakeup_mask & (1ULL << pin))) {
            continue;
        }
        err = configure_wakeup_pin((gpio_num_t)pin);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure wake-up GPIO%d: %s", pin, esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "Wake-up configured: GPIO mask 0x%llx (active LOW)", wakeup_mask);

    err = esp_sleep_enable_timer_wakeup(us_until_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure timer wake-up: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Timer wake-up in %llu s", (unsigned long long)(us_until_timer / 1000000ULL));

    return ESP_OK;
}

void deep_sleep_enter(void)
{
    ESP_LOGI(TAG, "Entering deep sleep mode...");

    led_off();
    board_power_prepare_for_deep_sleep();

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

void deep_sleep_fallback(const char *reason)
{
    ESP_LOGE(TAG, "Going to sleep after a failure (%s) so the battery is not drained", reason);

    if (deep_sleep_configure_wakeup() != ESP_OK) {
        ESP_LOGE(TAG, "Wake-up configuration failed too, retrying in %d h", FALLBACK_RETRY_HOURS);
        esp_sleep_enable_timer_wakeup((uint64_t)FALLBACK_RETRY_HOURS * 3600ULL * 1000000ULL);
    }

    deep_sleep_enter();
}

static void awake_watchdog_task(void *arg)
{
    (void)arg;
    const TickType_t limit = pdMS_TO_TICKS((uint32_t)CONFIG_MAX_AWAKE_SECONDS * 1000U);

    vTaskDelay(limit);

    if (global_event_group != NULL && (xEventGroupGetBits(global_event_group) & IS_OTA_UPDATE_RUNNING)) {
        ESP_LOGW(TAG, "Awake for %d s with an OTA update running, allowing the same time once more", CONFIG_MAX_AWAKE_SECONDS);
        vTaskDelay(limit);
    }

    ESP_LOGE(TAG, "Awake for too long (%d s), forcing deep sleep", CONFIG_MAX_AWAKE_SECONDS);

    if (deep_sleep_configure_wakeup() != ESP_OK) {
        esp_sleep_enable_timer_wakeup((uint64_t)FALLBACK_RETRY_HOURS * 3600ULL * 1000000ULL);
    }

    /* Not deep_sleep_enter(): never wait for an LED pattern that may be stuck */
    board_power_prepare_for_deep_sleep();
    esp_deep_sleep_start();
}

void deep_sleep_start_awake_watchdog(void)
{
    if (xTaskCreatePinnedToCore(awake_watchdog_task, "Awake WDT", AWAKE_WATCHDOG_STACK, NULL, 2, NULL, 1) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start the awake watchdog");
        return;
    }
    ESP_LOGI(TAG, "Awake watchdog: deep sleep is forced after %d s", CONFIG_MAX_AWAKE_SECONDS);
}
