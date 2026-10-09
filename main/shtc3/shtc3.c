/**
 * @file shtc3.c
 * @brief SHTC3 temperature / humidity sensor (I2C)
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "shtc3.h"
#include "../board_i2c/board_i2c.h"

static const char *TAG = "shtc3";

#define SHTC3_I2C_ADDRESS        0x70
#define SHTC3_I2C_SPEED_HZ       100000
#define SHTC3_I2C_TIMEOUT_MS     100

/* Commands (MSB first) */
#define SHTC3_CMD_WAKEUP         0x3517
#define SHTC3_CMD_SLEEP          0xB098
#define SHTC3_CMD_MEASURE        0x7866 /* T first, normal mode, clock stretching disabled */

/* Delays are rounded up to whole FreeRTOS ticks (10 ms at 100 Hz): the sensor
 * needs ~240 us after wake-up and up to 12.1 ms for a normal-mode measurement */
#define SHTC3_WAKEUP_DELAY_TICKS   1
#define SHTC3_MEASURE_DELAY_TICKS  2
#define SHTC3_READ_RETRIES         3

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t shtc3_attach(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus;
    esp_err_t err = board_i2c_get_bus(&bus);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_I2C_ADDRESS,
        .scl_speed_hz = SHTC3_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &dev_config, &s_dev);
}

static esp_err_t shtc3_send_command(uint16_t command)
{
    uint8_t tx[2] = { (uint8_t)(command >> 8), (uint8_t)(command & 0xFF) };
    return i2c_master_transmit(s_dev, tx, sizeof(tx), SHTC3_I2C_TIMEOUT_MS);
}

/* CRC-8, polynomial 0x31, init 0xFF (Sensirion) */
static uint8_t shtc3_crc(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t shtc3_read(shtc3_reading_t *reading)
{
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = shtc3_attach();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot attach to the I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    err = shtc3_send_command(SHTC3_CMD_WAKEUP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor not responding: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(SHTC3_WAKEUP_DELAY_TICKS);

    err = shtc3_send_command(SHTC3_CMD_MEASURE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Measure command rejected: %s", esp_err_to_name(err));
        shtc3_send_command(SHTC3_CMD_SLEEP);
        return err;
    }

    /* The sensor NACKs reads until the measurement is finished */
    uint8_t rx[6] = {0};
    for (int attempt = 0; attempt < SHTC3_READ_RETRIES; attempt++) {
        vTaskDelay(SHTC3_MEASURE_DELAY_TICKS);
        err = i2c_master_receive(s_dev, rx, sizeof(rx), SHTC3_I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            break;
        }
    }

    shtc3_send_command(SHTC3_CMD_SLEEP);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Measurement read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (shtc3_crc(&rx[0], 2) != rx[2] || shtc3_crc(&rx[3], 2) != rx[5]) {
        ESP_LOGW(TAG, "CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    uint32_t raw_temperature = ((uint32_t)rx[0] << 8) | rx[1];
    uint32_t raw_humidity = ((uint32_t)rx[3] << 8) | rx[4];

    /* T[C] = -45 + 175 * raw / 65535 ; RH[%] = 100 * raw / 65535 */
    reading->temperature_x10 = -450 + (int)((1750UL * raw_temperature + 32767UL) / 65535UL);
    reading->humidity = (int)((100UL * raw_humidity + 32767UL) / 65535UL);

    ESP_LOGI(TAG, "Temperature %d.%d C, humidity %d%%",
             reading->temperature_x10 / 10, abs(reading->temperature_x10 % 10), reading->humidity);
    return ESP_OK;
}
