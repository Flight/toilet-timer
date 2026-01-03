/**
 * @file main.c
 * @brief Toilet Timer - E-Paper Display Application
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "epd_driver.h"
#include "graphics.h"

static const char *TAG = "toilet_timer";

#define EPD_PIN_MOSI    15
#define EPD_PIN_CLK     14
#define EPD_PIN_CS      13
#define EPD_PIN_DC      12
#define EPD_PIN_RST     11
#define EPD_PIN_BUSY    10
#define EPD_PIN_POWER   42

#define EPD_WIDTH       128
#define EPD_HEIGHT      80

static esp_err_t display_hello_world(void)
{
    size_t buffer_size = (EPD_WIDTH * EPD_HEIGHT) / 8;
    uint8_t *framebuffer = malloc(buffer_size);
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        return ESP_ERR_NO_MEM;
    }

    memset(framebuffer, 0xFF, buffer_size);

    const char *text = "Hello World";
    const int x = (80 - strlen(text) * 6) / 2;
    const int y = (128 - 7) / 2;

    draw_string(framebuffer, x, y, text, 0);

    esp_err_t ret = epd_display_buffer(framebuffer, buffer_size);
    free(framebuffer);

    return ret;
}

void app_main(void)
{
    const epd_config_t epd_config = {
        .pin_mosi = EPD_PIN_MOSI,
        .pin_clk = EPD_PIN_CLK,
        .pin_cs = EPD_PIN_CS,
        .pin_dc = EPD_PIN_DC,
        .pin_rst = EPD_PIN_RST,
        .pin_busy = EPD_PIN_BUSY,
        .pin_power = EPD_PIN_POWER,
        .width = EPD_WIDTH,
        .height = EPD_HEIGHT,
    };

    if (epd_init(&epd_config) != ESP_OK) {
        return;
    }

    display_hello_world();
    epd_sleep();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
