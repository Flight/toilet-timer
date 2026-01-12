/**
 * @file display.c
 * @brief High-level display interface implementation
 *

 */

#include "sdkconfig.h"
#include "display.h"
#include "driver/epd_driver_gdew0102t4.h"
#include "graphics.h"
#include "global_constants.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "display";

static struct {
    uint8_t *framebuffer;
    size_t buffer_size;
    bool initialized;
} s_display = {0};

esp_err_t display_init(void)
{
    if (s_display.initialized) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_OK;
    }

    /* Initialize e-paper hardware */
    const epd_config_t epd_config = {
        .pin_mosi = CONFIG_EPD_PIN_MOSI,
        .pin_clk = CONFIG_EPD_PIN_CLK,
        .pin_cs = CONFIG_EPD_PIN_CS,
        .pin_dc = CONFIG_EPD_PIN_DC,
        .pin_rst = CONFIG_EPD_PIN_RST,
        .pin_busy = CONFIG_EPD_PIN_BUSY,
        .pin_power = CONFIG_EPD_PIN_POWER,
        .width = CONFIG_DISPLAY_WIDTH,
        .height = CONFIG_DISPLAY_HEIGHT,
    };

    esp_err_t ret = epd_init(&epd_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize e-paper display");
        return ret;
    }

    /* Allocate framebuffer */
    s_display.buffer_size = (CONFIG_DISPLAY_WIDTH * CONFIG_DISPLAY_HEIGHT) / 8;
    s_display.framebuffer = malloc(s_display.buffer_size);
    if (s_display.framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        epd_deinit();
        return ESP_ERR_NO_MEM;
    }

    /* Initialize graphics context */
    graphics_init(CONFIG_DISPLAY_WIDTH, CONFIG_DISPLAY_HEIGHT);

    /* Clear to white */
    memset(s_display.framebuffer, 0xFF, s_display.buffer_size);

    s_display.initialized = true;
    ESP_LOGI(TAG, "Display initialized (%dx%d)", CONFIG_DISPLAY_WIDTH, CONFIG_DISPLAY_HEIGHT);

    return ESP_OK;
}

void display_deinit(void)
{
    if (!s_display.initialized) {
        return;
    }

    if (s_display.framebuffer != NULL) {
        free(s_display.framebuffer);
        s_display.framebuffer = NULL;
    }

    epd_deinit();
    s_display.initialized = false;

    ESP_LOGI(TAG, "Display deinitialized");
}

void display_clear(void)
{
    if (!s_display.initialized || s_display.framebuffer == NULL) {
        return;
    }

    memset(s_display.framebuffer, 0xFF, s_display.buffer_size);
}

void display_draw_text(int x, int y, const char *text, uint8_t color)
{
    if (!s_display.initialized || s_display.framebuffer == NULL) {
        return;
    }

    draw_string(s_display.framebuffer, x, y, text, color);
}

void display_draw_text_centered(int y, const char *text, uint8_t color)
{
    if (!s_display.initialized || s_display.framebuffer == NULL) {
        return;
    }

    int text_width = measure_string_width(text);
    int x = (CONFIG_DISPLAY_WIDTH - text_width) / 2;

    draw_string(s_display.framebuffer, x, y, text, color);
}

int display_measure_text(const char *text)
{
    return measure_string_width(text);
}

esp_err_t display_update(void)
{
    if (!s_display.initialized || s_display.framebuffer == NULL) {
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return epd_display_buffer(s_display.framebuffer, s_display.buffer_size);
}

esp_err_t display_sleep(void)
{
    if (!s_display.initialized) {
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return epd_sleep();
}

int display_get_width(void)
{
    return CONFIG_DISPLAY_WIDTH;
}

int display_get_height(void)
{
    return CONFIG_DISPLAY_HEIGHT;
}
