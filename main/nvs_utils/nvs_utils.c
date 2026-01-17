/**
 * @file nvs_utils.c
 * @brief Common NVS (Non-Volatile Storage) utility functions
 */

#include <esp_log.h>
#include <nvs.h>
#include "nvs_utils.h"

static const char *TAG = "nvs_utils";

esp_err_t nvs_utils_read_blob(const char *namespace, const char *key, void *data, size_t size)
{
    nvs_handle_t handle;
    size_t required_size = size;

    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, key, data, &required_size);
    nvs_close(handle);

    return err;
}

esp_err_t nvs_utils_write_blob(const char *namespace, const char *key, const void *data, size_t size)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s' for writing", namespace);
        return err;
    }

    err = nvs_set_blob(handle, key, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return err;
}

esp_err_t nvs_utils_read_u8(const char *namespace, const char *key, uint8_t *value)
{
    return nvs_utils_read_blob(namespace, key, value, sizeof(*value));
}

esp_err_t nvs_utils_write_u8(const char *namespace, const char *key, uint8_t value)
{
    return nvs_utils_write_blob(namespace, key, &value, sizeof(value));
}

esp_err_t nvs_utils_read_timestamp(const char *namespace, const char *key, time_t *timestamp)
{
    return nvs_utils_read_blob(namespace, key, timestamp, sizeof(*timestamp));
}

esp_err_t nvs_utils_write_timestamp(const char *namespace, const char *key, time_t timestamp)
{
    return nvs_utils_write_blob(namespace, key, &timestamp, sizeof(timestamp));
}
