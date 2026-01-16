/**
 * @file deep_sleep.c
 * @brief Deep sleep management with EXT1 wake-up
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#include "deep_sleep.h"

static const char *TAG = "deep_sleep";

/* Wake-up GPIOs: GPIO0, GPIO3, GPIO4 */
#define WAKEUP_GPIO_MASK ((1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_4))

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

    return ESP_OK;
}

void deep_sleep_enter(void)
{
    ESP_LOGI(TAG, "Entering deep sleep mode...");
    ESP_LOGI(TAG, "Device will wake up when GPIO0, GPIO3, or GPIO4 is pulled LOW");

    /* Small delay to allow log to flush */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Enter deep sleep - this function does not return */
    esp_deep_sleep_start();
}
