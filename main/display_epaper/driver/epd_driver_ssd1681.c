/**
 * @file epd_driver_ssd1681.c
 * @brief E-Paper Display Driver for the 1.54" 200x200 SSD1681 panel
 *        (Waveshare ESP32-S3-ePaper-1.54, panel "1.54inch e-Paper V2")
 *
 * The init / refresh sequence and the full-refresh waveform follow the
 * vendor's EPD_1in54_V2 reference driver.
 */

#include "epd_driver_ssd1681.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "epd_driver";

/* SSD1681 command set (subset) */
#define SSD1681_DRIVER_OUTPUT_CONTROL     0x01
#define SSD1681_GATE_VOLTAGE_CONTROL      0x03
#define SSD1681_SOURCE_VOLTAGE_CONTROL    0x04
#define SSD1681_DEEP_SLEEP_MODE           0x10
#define SSD1681_DATA_ENTRY_MODE           0x11
#define SSD1681_SW_RESET                  0x12
#define SSD1681_TEMP_SENSOR_CONTROL       0x18
#define SSD1681_MASTER_ACTIVATION         0x20
#define SSD1681_DISPLAY_UPDATE_CONTROL_2  0x22
#define SSD1681_WRITE_RAM_BW              0x24
#define SSD1681_WRITE_RAM_RED             0x26
#define SSD1681_WRITE_VCOM_REGISTER       0x2C
#define SSD1681_WRITE_LUT_REGISTER        0x32
#define SSD1681_BORDER_WAVEFORM_CONTROL   0x3C
#define SSD1681_END_OPTION                0x3F
#define SSD1681_SET_RAM_X_RANGE           0x44
#define SSD1681_SET_RAM_Y_RANGE           0x45
#define SSD1681_SET_RAM_X_COUNTER         0x4E
#define SSD1681_SET_RAM_Y_COUNTER         0x4F

/* Display update control 2 sequences */
#define SSD1681_UPDATE_LOAD_TEMP_LUT      0xB1  /* Load temperature + OTP LUT */
#define SSD1681_UPDATE_FULL               0xC7  /* Clock, analog, display mode 1, power down */

#define SSD1681_DEEP_SLEEP_MODE_1         0x01

/* SPI / timing constants */
#define EPD_SPI_CLOCK_SPEED_HZ   (10 * 1000 * 1000)
#define EPD_SPI_QUEUE_SIZE       7
#define EPD_RESET_PULSE_MS       20
#define EPD_RESET_SETTLE_MS      50
#define EPD_POWER_SETTLE_MS      20
#define EPD_BUSY_POLL_DELAY_MS   10
#define EPD_BUSY_TIMEOUT_MS      10000  /* A full refresh takes ~2 s, longer when cold */

/* Full-refresh waveform for the 1.54" V2 panel (vendor WF_Full_1IN54).
 * Bytes 0..152: LUT, 153: end option, 154: gate voltage,
 * 155..157: source voltage, 158: VCOM. */
static const uint8_t s_lut_full[159] = {
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x00, 0x08, 0x01, 0x00, 0x02,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x22, 0x17, 0x41, 0x00, 0x32, 0x20
};

/* Module state */
static spi_device_handle_t s_spi_handle = NULL;
static epd_config_t s_config = {0};
static bool s_bus_initialized = false;  /* SPI bus + GPIOs configured */
static bool s_panel_awake = false;      /* Panel powered and initialized */

static esp_err_t epd_wait_idle(void)
{
    int elapsed_ms = 0;

    /* SSD1681: BUSY is HIGH while the controller is busy */
    while (gpio_get_level(s_config.pin_busy) == 1) {
        vTaskDelay(pdMS_TO_TICKS(EPD_BUSY_POLL_DELAY_MS));
        elapsed_ms += EPD_BUSY_POLL_DELAY_MS;

        if (elapsed_ms >= EPD_BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "Timeout waiting for display (BUSY pin stuck HIGH)");
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

static esp_err_t epd_send_data_buffer(const uint8_t *data, size_t len)
{
    gpio_set_level(s_config.pin_dc, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_spi_handle, &t);
}

static void epd_set_power(bool on)
{
    if (s_config.pin_power < 0) {
        return;
    }
    int level = s_config.power_active_low ? !on : on;
    gpio_set_level(s_config.pin_power, level);
}

static void epd_reset(void)
{
    gpio_set_level(s_config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(EPD_RESET_SETTLE_MS));
    gpio_set_level(s_config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(EPD_RESET_PULSE_MS));
    gpio_set_level(s_config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(EPD_RESET_SETTLE_MS));
}

static esp_err_t epd_hardware_init(void)
{
    esp_err_t ret;

    /* Configure panel supply switch if present */
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
        epd_set_power(true);
        vTaskDelay(pdMS_TO_TICKS(EPD_POWER_SETTLE_MS));
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
    gpio_set_level(s_config.pin_rst, 1);

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

/* RAM window covering the whole panel, Y running from height-1 down to 0 */
static esp_err_t epd_set_window(void)
{
    esp_err_t ret;
    uint16_t x_end = s_config.width - 1;
    uint16_t y_start = s_config.height - 1;

    ret = epd_send_command(SSD1681_SET_RAM_X_RANGE);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x00);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data((x_end >> 3) & 0xFF);
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(SSD1681_SET_RAM_Y_RANGE);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(y_start & 0xFF);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data((y_start >> 8) & 0xFF);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x00);
    if (ret != ESP_OK) return ret;
    return epd_send_data(0x00);
}

/* Move the RAM address counter to the first byte of the window */
static esp_err_t epd_set_cursor(void)
{
    esp_err_t ret;
    uint16_t y_start = s_config.height - 1;

    ret = epd_send_command(SSD1681_SET_RAM_X_COUNTER);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x00);
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(SSD1681_SET_RAM_Y_COUNTER);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(y_start & 0xFF);
    if (ret != ESP_OK) return ret;
    return epd_send_data((y_start >> 8) & 0xFF);
}

static esp_err_t epd_set_lut(const uint8_t *lut)
{
    esp_err_t ret;

    ret = epd_send_command(SSD1681_WRITE_LUT_REGISTER);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data_buffer(lut, 153);
    if (ret != ESP_OK) return ret;
    ret = epd_wait_idle();
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(SSD1681_END_OPTION);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(lut[153]);
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(SSD1681_GATE_VOLTAGE_CONTROL);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(lut[154]);
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(SSD1681_SOURCE_VOLTAGE_CONTROL);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(lut[155]);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(lut[156]);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(lut[157]);
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(SSD1681_WRITE_VCOM_REGISTER);
    if (ret != ESP_OK) return ret;
    return epd_send_data(lut[158]);
}

static esp_err_t epd_panel_init_sequence(void)
{
    esp_err_t ret;
    uint16_t gate_lines = s_config.height - 1;

    epd_reset();

    ret = epd_wait_idle();
    if (ret != ESP_OK) return ret;
    ret = epd_send_command(SSD1681_SW_RESET);
    if (ret != ESP_OK) return ret;
    ret = epd_wait_idle();
    if (ret != ESP_OK) return ret;

    /* Driver output control: 200 gate lines, scan G199 -> G0 */
    ret = epd_send_command(SSD1681_DRIVER_OUTPUT_CONTROL);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(gate_lines & 0xFF);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data((gate_lines >> 8) & 0xFF);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x01);
    if (ret != ESP_OK) return ret;

    /* Data entry mode: X increment, Y decrement */
    ret = epd_send_command(SSD1681_DATA_ENTRY_MODE);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x01);
    if (ret != ESP_OK) return ret;

    ret = epd_set_window();
    if (ret != ESP_OK) return ret;

    /* Border waveform */
    ret = epd_send_command(SSD1681_BORDER_WAVEFORM_CONTROL);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x01);
    if (ret != ESP_OK) return ret;

    /* Use the internal temperature sensor */
    ret = epd_send_command(SSD1681_TEMP_SENSOR_CONTROL);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(0x80);
    if (ret != ESP_OK) return ret;

    /* Load temperature and waveform settings */
    ret = epd_send_command(SSD1681_DISPLAY_UPDATE_CONTROL_2);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(SSD1681_UPDATE_LOAD_TEMP_LUT);
    if (ret != ESP_OK) return ret;
    ret = epd_send_command(SSD1681_MASTER_ACTIVATION);
    if (ret != ESP_OK) return ret;

    ret = epd_set_cursor();
    if (ret != ESP_OK) return ret;
    ret = epd_wait_idle();
    if (ret != ESP_OK) return ret;

    return epd_set_lut(s_lut_full);
}

esp_err_t epd_init(const epd_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bus_initialized) {
        ESP_LOGW(TAG, "E-Paper display already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing E-Paper display (%dx%d)", config->width, config->height);

    memcpy(&s_config, config, sizeof(epd_config_t));

    esp_err_t ret = epd_hardware_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Hardware initialization failed");
        return ret;
    }
    s_bus_initialized = true;

    ret = epd_panel_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization sequence failed");
        epd_deinit();
        return ret;
    }

    s_panel_awake = true;
    ESP_LOGI(TAG, "E-Paper display initialized successfully");

    return ESP_OK;
}

esp_err_t epd_deinit(void)
{
    if (!s_bus_initialized) {
        return ESP_OK;
    }

    if (s_spi_handle != NULL) {
        spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
    }

    spi_bus_free(SPI2_HOST);
    s_bus_initialized = false;
    s_panel_awake = false;

    ESP_LOGI(TAG, "E-Paper display deinitialized");

    return ESP_OK;
}

esp_err_t epd_display_buffer(const uint8_t *buffer, size_t size)
{
    if (!s_panel_awake) {
        ESP_LOGE(TAG, "E-Paper display not initialized or asleep");
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer == NULL) {
        ESP_LOGE(TAG, "Buffer cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    size_t expected_size = (s_config.width * s_config.height) / 8;
    if (size != expected_size) {
        ESP_LOGE(TAG, "Invalid buffer size: %u (expected %u)", (unsigned)size, (unsigned)expected_size);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    /* New image */
    ret = epd_set_cursor();
    if (ret != ESP_OK) return ret;
    ret = epd_send_command(SSD1681_WRITE_RAM_BW);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data_buffer(buffer, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send B/W RAM data");
        return ret;
    }

    /* "Previous image" RAM: keep it in sync so the next refresh starts from a known state */
    ret = epd_set_cursor();
    if (ret != ESP_OK) return ret;
    ret = epd_send_command(SSD1681_WRITE_RAM_RED);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data_buffer(buffer, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send previous-image RAM data");
        return ret;
    }

    /* Full refresh */
    ret = epd_send_command(SSD1681_DISPLAY_UPDATE_CONTROL_2);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(SSD1681_UPDATE_FULL);
    if (ret != ESP_OK) return ret;
    ret = epd_send_command(SSD1681_MASTER_ACTIVATION);
    if (ret != ESP_OK) return ret;

    return epd_wait_idle();
}

esp_err_t epd_clear(void)
{
    if (!s_panel_awake) {
        ESP_LOGE(TAG, "E-Paper display not initialized or asleep");
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
    if (!s_bus_initialized) {
        ESP_LOGE(TAG, "E-Paper display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_panel_awake) {
        return ESP_OK;
    }

    esp_err_t ret = epd_wait_idle();
    if (ret != ESP_OK) return ret;

    ret = epd_send_command(SSD1681_DEEP_SLEEP_MODE);
    if (ret != ESP_OK) return ret;
    ret = epd_send_data(SSD1681_DEEP_SLEEP_MODE_1);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Keep the control lines low while the panel is unpowered to avoid back-powering it */
    gpio_set_level(s_config.pin_rst, 0);
    gpio_set_level(s_config.pin_dc, 0);
    epd_set_power(false);

    s_panel_awake = false;
    return ESP_OK;
}

esp_err_t epd_wake(void)
{
    if (!s_bus_initialized) {
        ESP_LOGE(TAG, "E-Paper display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_panel_awake) {
        ESP_LOGW(TAG, "E-Paper display already awake");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Waking up E-Paper display");

    epd_set_power(true);
    vTaskDelay(pdMS_TO_TICKS(EPD_POWER_SETTLE_MS));

    esp_err_t ret = epd_panel_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init sequence failed during wake");
        return ret;
    }

    s_panel_awake = true;
    ESP_LOGI(TAG, "E-Paper display woke up successfully");

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
