/**
 * @file trigger.h
 * @brief GPIO4 trigger button handling and timestamp management
 */

#ifndef TRIGGER_H
#define TRIGGER_H

#include <esp_err.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Initialize GPIO4 interrupt for button press detection
 */
void trigger_init_interrupt(void);

/**
 * @brief Remove GPIO4 interrupt handler
 */
void trigger_deinit_interrupt(void);

/**
 * @brief Check if GPIO4 button was pressed (and clear flag)
 * @return true if button was pressed since last check
 */
bool trigger_check_and_clear(void);

/**
 * @brief Get last trigger timestamp from NVS
 * @return Timestamp or 0 if not found
 */
time_t trigger_get_last_timestamp(void);

/**
 * @brief Save trigger timestamp to NVS
 * @param timestamp Timestamp to save
 * @return ESP_OK on success
 */
esp_err_t trigger_save_timestamp(time_t timestamp);

/**
 * @brief Format datetime string with days indicator
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param days_since_trigger Days since last trigger
 * @param trigger_timestamp Trigger timestamp (0 if none)
 */
void trigger_format_datetime(char *buf, size_t buf_size, int days_since_trigger, time_t trigger_timestamp);

#endif /* TRIGGER_H */
