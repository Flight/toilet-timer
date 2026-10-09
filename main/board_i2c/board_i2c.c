/**
 * @file board_i2c.c
 * @brief Shared I2C master bus of the Waveshare ESP32-S3-ePaper-1.54
 */

#include <sdkconfig.h>
#include <esp_log.h>

#include "board_i2c.h"

static const char *TAG = "board_i2c";

static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t board_i2c_get_bus(i2c_master_bus_handle_t *bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bus == NULL) {
        i2c_master_bus_config_t bus_config = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_NUM_0,
            .scl_io_num = CONFIG_BOARD_I2C_SCL_GPIO,
            .sda_io_num = CONFIG_BOARD_I2C_SDA_GPIO,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus (SDA=%d SCL=%d): %s",
                     CONFIG_BOARD_I2C_SDA_GPIO, CONFIG_BOARD_I2C_SCL_GPIO, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "I2C bus ready (SDA=%d SCL=%d)", CONFIG_BOARD_I2C_SDA_GPIO, CONFIG_BOARD_I2C_SCL_GPIO);
    }

    *bus = s_bus;
    return ESP_OK;
}
