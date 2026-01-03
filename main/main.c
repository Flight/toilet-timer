/**
 * @file main.c
 * @brief Toilet Timer - LED control application
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "toilet_timer";

#define BLINK_GPIO CONFIG_BLINK_GPIO
#define LED_OFF 0
#define LED_ON 1

static bool s_led_state = false;

#ifdef CONFIG_BLINK_LED_STRIP

static led_strip_handle_t led_strip;

#define LED_STRIP_PIXEL_INDEX 0
#define LED_COLOR_RED   1
#define LED_COLOR_GREEN 10
#define LED_COLOR_BLUE  1

static void blink_led(void)
{
    if (s_led_state) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, LED_STRIP_PIXEL_INDEX,
                                            LED_COLOR_RED, LED_COLOR_GREEN, LED_COLOR_BLUE));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    } else {
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
    }
}

static esp_err_t configure_led(void)
{
    ESP_LOGI(TAG, "Configuring addressable LED strip on GPIO %d", BLINK_GPIO);

    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
    };

#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else
#error "Unsupported LED strip backend"
#endif

    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    ESP_LOGI(TAG, "LED strip configured successfully");

    return ESP_OK;
}

#elif CONFIG_BLINK_LED_GPIO

static void blink_led(void)
{
    ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, s_led_state));
}

static esp_err_t configure_led(void)
{
    ESP_LOGI(TAG, "Configuring GPIO LED on pin %d", BLINK_GPIO);

    ESP_ERROR_CHECK(gpio_reset_pin(BLINK_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT));

    ESP_LOGI(TAG, "GPIO LED configured successfully");
    return ESP_OK;
}

#else
#error "Unsupported LED type. Please configure LED type in menuconfig."
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Toilet Timer starting...");

    ESP_ERROR_CHECK(configure_led());

    while (true) {
        ESP_LOGI(TAG, "LED: %s", s_led_state ? "ON" : "OFF");
        blink_led();
        s_led_state = !s_led_state;
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BLINK_PERIOD));
    }
}
