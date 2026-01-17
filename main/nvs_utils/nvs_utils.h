/**
 * @file nvs_utils.h
 * @brief Common NVS (Non-Volatile Storage) utility functions
 */

#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include <esp_err.h>
#include <nvs.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Read a blob from NVS
 * @param namespace NVS namespace
 * @param key Key name
 * @param data Pointer to data buffer
 * @param size Size of data buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t nvs_utils_read_blob(const char *namespace, const char *key, void *data, size_t size);

/**
 * @brief Write a blob to NVS
 * @param namespace NVS namespace
 * @param key Key name
 * @param data Pointer to data
 * @param size Size of data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t nvs_utils_write_blob(const char *namespace, const char *key, const void *data, size_t size);

/**
 * @brief Read a uint8_t flag from NVS
 * @param namespace NVS namespace
 * @param key Key name
 * @param value Pointer to store value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t nvs_utils_read_u8(const char *namespace, const char *key, uint8_t *value);

/**
 * @brief Write a uint8_t flag to NVS
 * @param namespace NVS namespace
 * @param key Key name
 * @param value Value to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t nvs_utils_write_u8(const char *namespace, const char *key, uint8_t value);

/**
 * @brief Read a timestamp from NVS
 * @param namespace NVS namespace
 * @param key Key name
 * @param timestamp Pointer to store timestamp
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t nvs_utils_read_timestamp(const char *namespace, const char *key, time_t *timestamp);

/**
 * @brief Write a timestamp to NVS
 * @param namespace NVS namespace
 * @param key Key name
 * @param timestamp Timestamp to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t nvs_utils_write_timestamp(const char *namespace, const char *key, time_t timestamp);

#endif /* NVS_UTILS_H */
