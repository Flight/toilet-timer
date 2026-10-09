/**
 * @file trigger.h
 * @brief Key handling (PWR and BOOT) and last-change timestamp management
 */

#ifndef TRIGGER_H
#define TRIGGER_H

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/** Bit mask of both keys, for EXT1 wake-up configuration and status decoding */
#define TRIGGER_BUTTON_MASK ((1ULL << CONFIG_BUTTON_PWR_GPIO) | (1ULL << CONFIG_BUTTON_BOOT_GPIO))

/**
 * @brief Decide whether a key wake-up is a long press
 *
 * Waits (up to CONFIG_BUTTON_LONG_PRESS_MS after boot) for the keys in the
 * wake-up mask to be released.
 *
 * @param wakeup_mask EXT1 wake-up GPIO mask
 * @return true if a key was still held at the threshold
 */
bool trigger_is_long_press(uint64_t wakeup_mask);

/**
 * @brief Initialize the key interrupts for presses while awake
 */
void trigger_init_interrupt(void);

/**
 * @brief Remove the key interrupt handlers
 */
void trigger_deinit_interrupt(void);

/**
 * @brief Check if a key was pressed (and clear flag)
 * @return true if a key was pressed since last check
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
 * @brief Format the "days ago" caption in Ukrainian ("днів тому", "дні тому", "день тому")
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param days_since_trigger Days since last trigger (>= 2)
 */
void trigger_format_days_caption(char *buf, size_t buf_size, int days_since_trigger);

/**
 * @brief Format a timestamp as "dd-mm-yyyy hh:mm" in local time
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param timestamp Timestamp to format (0 = current time)
 */
void trigger_format_datetime(char *buf, size_t buf_size, time_t timestamp);

#endif /* TRIGGER_H */
