/**
 * @file show_messages.c
 * @brief Show messages on E-Paper display
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "../global_constants.h"
#include "../display_epaper/display.h"
#include "show_messages.h"

static const char *TAG = "show_messages";

void show_messages_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Show messages task started");

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        vTaskDelete(NULL);
        return;
    }

    const char *texts[] = {
        "\n\n    Киця!",
        "\n Киця-Кицюня \n      !",
        "\n\n  Манюююня!"
    };
    int y = (CONFIG_DISPLAY_HEIGHT - FONT_CHAR_HEIGHT) / 2;

    while (true) {
        for (int i = 0; i < 3; i++) {
            /* Clear framebuffer */
            display_clear();

            /* Draw text */
            display_draw_text_centered(y, texts[i], 0);

            /* Update physical display */
            if (display_update() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update display");
                display_deinit();
                vTaskDelete(NULL);
                return;
            }

            ESP_LOGI(TAG, "Display updated: %s", texts[i]);

            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    /* Put display to sleep */
    display_sleep();

    ESP_LOGI(TAG, "Display sequence completed");

    vTaskDelete(NULL);
}
