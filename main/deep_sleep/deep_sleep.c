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

#include "deep_sleep.h"
#include "../time_utils/time_utils.h"

static const char *TAG = "deep_sleep";

/* Wake-up GPIOs: GPIO0, GPIO3, GPIO4 */
#define WAKEUP_GPIO_MASK ((1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_4))

esp_err_t deep_sleep_configure_wakeup(void)
{
    ESP_LOGI(TAG, "Configuring deep sleep wake-up sources");

    /* Configure EXT1 wake-up (multiple RTC GPIOs with level trigger) */
    esp_err_t err = esp_sleep_enable_ext1_wakeup(WAKEUP_GPIO_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure EXT1 wake-up: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure GPIO pull-ups */
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
    uint64_t us_until_midnight = time_utils_us_until_midnight();
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
    ESP_LOGI(TAG, "Wake-up: GPIO0/3/4 LOW, or at midnight");

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}
