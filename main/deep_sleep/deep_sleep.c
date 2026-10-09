/**
 * @file deep_sleep.c
 * @brief Deep sleep management with button, RTC alarm and timer wake-up
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <time.h>

#include "deep_sleep.h"
#include "../global_constants.h"
#include "../time_utils/time_utils.h"
#include "../board_power/board_power.h"
#include "../trigger/trigger.h"
#include "../led/led.h"
#ifdef CONFIG_RTC_PCF85063_ENABLED
#include "../rtc_pcf85063/rtc_pcf85063.h"
#endif

static const char *TAG = "deep_sleep";

#define WAKEUP_BUTTON_MASK TRIGGER_BUTTON_MASK

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
    int timer_hour = DAILY_WAKEUP_HOUR;

#ifdef CONFIG_RTC_PCF85063_ENABLED
    /* The crystal-driven RTC alarm is the primary nightly wake-up; the ESP32
     * timer (RC oscillator, drifts ~2%) only fires later as a fallback. */
#if CONFIG_RTC_ALARM_TEST_MINUTES > 0
    time_t now = 0;
    time(&now);
    ESP_LOGW(TAG, "TEST MODE: RTC alarm in %d minutes instead of the nightly refresh", CONFIG_RTC_ALARM_TEST_MINUTES);
    bool alarm_armed = (rtc_pcf85063_arm_alarm_at(now + CONFIG_RTC_ALARM_TEST_MINUTES * 60) == ESP_OK);
#else
    bool alarm_armed = (rtc_pcf85063_arm_daily_alarm(DAILY_WAKEUP_HOUR, 0) == ESP_OK);
#endif
    if (alarm_armed) {
        wakeup_mask |= (1ULL << CONFIG_RTC_INT_GPIO);
        timer_hour = (DAILY_WAKEUP_HOUR + DAILY_WAKEUP_FALLBACK_DELAY_HOURS) % 24;
        ESP_LOGI(TAG, "RTC alarm wake-up at %02d:00, timer fallback at %02d:00", DAILY_WAKEUP_HOUR, timer_hour);
    } else {
        ESP_LOGW(TAG, "RTC alarm not available, using the timer wake-up at %02d:00", timer_hour);
    }
#endif

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

    /* Timer wake-up for the nightly refresh (or its fallback) */
    uint64_t us_until_wakeup = time_utils_us_until_hour(timer_hour);
    err = esp_sleep_enable_timer_wakeup(us_until_wakeup);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure timer wake-up: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Timer wake-up configured for next %02d:00", timer_hour);

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
