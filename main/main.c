/**
 * @file main.c
 * @brief Toilet Timer - E-Paper Display Application
 */

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "config.h"

static const char *TAG = "toilet_timer";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Toilet Timer");

    /* Initialize display */
    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    /* Clear and draw text */
    display_clear();

    const char *text = "Киця-Кицюня";
    int y = (CONFIG_DISPLAY_HEIGHT - FONT_CHAR_HEIGHT) / 2;
    display_draw_text_centered(y, text, 0);

    /* Update physical display */
    if (display_update() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display");
        display_deinit();
        return;
    }

    /* Put display to sleep */
    display_sleep();

    ESP_LOGI(TAG, "Display updated successfully");

    /* Main loop */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
