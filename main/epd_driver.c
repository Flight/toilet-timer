/**
 * @file epd_driver.c
 * @brief E-Paper Display Driver Implementation for LilyGo Mini E-Paper S3
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "epd_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "epd_driver";

/* UC8175 display controller command definitions for GDEW0102T4 */
#define UC8175_PSR         0x00   /* Panel Setting Register */
#define UC8175_PWR         0x01   /* Power Setting */
#define UC8175_POF         0x02   /* Power OFF */
#define UC8175_PON         0x04   /* Power ON */
#define UC8175_BTST        0x06   /* Booster Soft Start */
#define UC8175_DSLP        0x07   /* Deep Sleep */
#define UC8175_DTM1        0x10   /* Data Start Transmission 1 (Black/White) */
#define UC8175_DSP         0x11   /* Data Stop */
#define UC8175_DRF         0x12   /* Display Refresh */
#define UC8175_DTM2        0x13   /* Data Start Transmission 2 (Red - not used) */
#define UC8175_PLL         0x30   /* PLL Control */
#define UC8175_CDI         0x50   /* VCOM and Data Interval Setting */
#define UC8175_TCON        0x60   /* TCON Setting */
#define UC8175_TRES        0x61   /* Resolution Setting */
#define UC8175_PWS         0xE3   /* Power Saving */

/* SPI configuration constants */
#define EPD_SPI_CLOCK_SPEED_HZ              (4 * 1000 * 1000)
#define EPD_SPI_QUEUE_SIZE                  7
#define EPD_RESET_DELAY_MS                  20
#define EPD_BUSY_POLL_DELAY_MS              10

/* Module state */
static spi_device_handle_t s_spi_handle = NULL;
static epd_config_t s_config = {0};
static bool s_initialized = false;

static esp_err_t epd_wait_idle(void)
{
    const int max_wait_ms = 5000;  /* 5 second timeout */
    int elapsed_ms = 0;

    /* Wait for BUSY to go HIGH (inverted logic: LOW=busy, HIGH=idle) */
    while (gpio_get_level(s_config.pin_busy) == 0) {
        vTaskDelay(pdMS_TO_TICKS(EPD_BUSY_POLL_DELAY_MS));
        elapsed_ms += EPD_BUSY_POLL_DELAY_MS;

        if (elapsed_ms >= max_wait_ms) {
            ESP_LOGE(TAG, "Timeout waiting for display (BUSY pin stuck LOW)");
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

static esp_err_t epd_send_command(uint8_t cmd)
{
    gpio_set_level(s_config.pin_dc, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_polling_transmit(s_spi_handle, &t);
}

static esp_err_t epd_send_data(uint8_t data)
{
    gpio_set_level(s_config.pin_dc, 1);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    return spi_device_polling_transmit(s_spi_handle, &t);
}

static void epd_reset(void)
{
    gpio_set_level(s_config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(EPD_RESET_DELAY_MS));
    gpio_set_level(s_config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(EPD_RESET_DELAY_MS));
}

static esp_err_t epd_hardware_init(void)
{
    esp_err_t ret;

    /* Configure power enable pin if present */
    if (s_config.pin_power >= 0) {
        gpio_config_t pwr_conf = {
            .pin_bit_mask = (1ULL << s_config.pin_power),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ret = gpio_config(&pwr_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure power enable pin");
            return ret;
        }
        gpio_set_level(s_config.pin_power, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Configure output GPIO pins (DC and RST) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_config.pin_dc) | (1ULL << s_config.pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DC/RST GPIO pins");
        return ret;
    }

    /* Configure input GPIO pin (BUSY) */
    io_conf.pin_bit_mask = (1ULL << s_config.pin_busy);
    io_conf.mode = GPIO_MODE_INPUT;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure BUSY GPIO pin");
        return ret;
    }

    /* Configure SPI bus */
    spi_bus_config_t bus_config = {
        .mosi_io_num = s_config.pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = s_config.pin_clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (s_config.width * s_config.height) / 8,
    };

    ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return ret;
    }

    /* Configure SPI device */
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = EPD_SPI_CLOCK_SPEED_HZ,
        .mode = 0,
        .spics_io_num = s_config.pin_cs,
        .queue_size = EPD_SPI_QUEUE_SIZE,
    };

    ret = spi_bus_add_device(SPI2_HOST, &dev_config, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device");
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    return ESP_OK;
}

static esp_err_t epd_display_init_sequence(void)
{
    esp_err_t ret;

    /* Hardware reset */
    epd_reset();
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Panel Setting Register - KW mode, LUT from register */
    ret = epd_send_command(UC8175_PSR);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x0F);
    if (ret != ESP_OK) return ret;

    /* Power Setting */
    ret = epd_send_command(UC8175_PWR);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x03);  /* VDH/VDL */
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x00);  /* VGH/VGL */
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x2B);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x2B);
    if (ret != ESP_OK) return ret;

    /* Booster Soft Start - 50ms, strength 4, 8kHz */
    ret = epd_send_command(UC8175_BTST);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x3F);
    if (ret != ESP_OK) return ret;

    /* PLL Control - 30Hz frame rate */
    ret = epd_send_command(UC8175_PLL);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x13);
    if (ret != ESP_OK) return ret;

    /* Power ON and wait for ready */
    ret = epd_send_command(UC8175_PON);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(5));
    ret = epd_wait_idle();
    if (ret != ESP_OK) return ret;

    /* VCOM and Data Interval Setting */
    ret = epd_send_command(UC8175_CDI);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x57);
    if (ret != ESP_OK) return ret;

    /* TCON Setting */
    ret = epd_send_command(UC8175_TCON);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x22);
    if (ret != ESP_OK) return ret;

    /* Resolution Setting: 80x128 */
    ret = epd_send_command(UC8175_TRES);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(80);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(128);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t epd_init(const epd_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        ESP_LOGW(TAG, "E-Paper display already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing E-Paper display (%dx%d)", config->width, config->height);

    /* Store configuration */
    memcpy(&s_config, config, sizeof(epd_config_t));

    /* Initialize hardware */
    esp_err_t ret = epd_hardware_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Hardware initialization failed");
        return ret;
    }

    /* Initialize display */
    ret = epd_display_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization sequence failed");
        epd_deinit();
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "E-Paper display initialized successfully");

    return ESP_OK;
}

esp_err_t epd_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_spi_handle != NULL) {
        spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
    }

    spi_bus_free(SPI2_HOST);
    s_initialized = false;

    ESP_LOGI(TAG, "E-Paper display deinitialized");

    return ESP_OK;
}

esp_err_t epd_display_buffer(const uint8_t *buffer, size_t size)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "E-Paper display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer == NULL) {
        ESP_LOGE(TAG, "Buffer cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    size_t expected_size = (s_config.width * s_config.height) / 8;
    if (size != expected_size) {
        ESP_LOGE(TAG, "Invalid buffer size: %d (expected %d)", size, expected_size);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    /* Allocate inverted buffer for DTM1 */
    uint8_t *inverted_buffer = malloc(size);
    if (inverted_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate inverted buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Prepare inverted data for DTM1 (old buffer) */
    for (size_t i = 0; i < size; i++) {
        inverted_buffer[i] = ~buffer[i];
    }

    /* Write inverted data to DTM1 */
    ret = epd_send_command(UC8175_DTM1);
    if (ret != ESP_OK) {
        free(inverted_buffer);
        return ret;
    }

    gpio_set_level(s_config.pin_dc, 1);
    spi_transaction_t t1 = {
        .length = size * 8,
        .tx_buffer = inverted_buffer,
    };
    ret = spi_device_polling_transmit(s_spi_handle, &t1);
    free(inverted_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send DTM1 data");
        return ret;
    }

    /* Write normal data to DTM2 (new buffer) */
    ret = epd_send_command(UC8175_DTM2);
    if (ret != ESP_OK) return ret;

    gpio_set_level(s_config.pin_dc, 1);
    spi_transaction_t t2 = {
        .length = size * 8,
        .tx_buffer = buffer,
    };
    ret = spi_device_polling_transmit(s_spi_handle, &t2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send DTM2 data");
        return ret;
    }

    /* Data Stop */
    ret = epd_send_command(UC8175_DSP);
    if (ret != ESP_OK) return ret;

    /* Trigger display refresh */
    ret = epd_send_command(UC8175_DRF);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(100));

    /* Wait for refresh to complete */
    return epd_wait_idle();
}

esp_err_t epd_clear(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "E-Paper display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    size_t buffer_size = (s_config.width * s_config.height) / 8;
    uint8_t *white_buffer = malloc(buffer_size);
    if (white_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate clear buffer");
        return ESP_ERR_NO_MEM;
    }

    memset(white_buffer, 0xFF, buffer_size);
    esp_err_t ret = epd_display_buffer(white_buffer, buffer_size);
    free(white_buffer);

    return ret;
}

esp_err_t epd_sleep(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "E-Paper display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = epd_wait_idle();
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(UC8175_POF);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));

    ret = epd_send_command(UC8175_DSLP);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0xA5);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    if (s_config.pin_power >= 0) {
        gpio_set_level(s_config.pin_power, 0);
    }

    epd_deinit();
    return ESP_OK;
}

uint16_t epd_get_width(void)
{
    return s_config.width;
}

uint16_t epd_get_height(void)
{
    return s_config.height;
}
