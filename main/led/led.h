/**
 * @file led.h
 * @brief Green status LED feedback (active LOW on the Waveshare board)
 *
 * The red LED next to the USB port is driven by the charger chip (STAT) and
 * cannot be controlled by the firmware.
 */

#ifndef LED_H
#define LED_H

typedef enum {
    LED_PATTERN_CHANGE_RECORDED, /**< Slow blinks for 5 s: a litter change was recorded */
    LED_PATTERN_SYNC_REQUESTED,  /**< One long blink: Wi-Fi sync and update check requested */
    LED_PATTERN_LOW_BATTERY,     /**< Fast blinks for 10 s: battery needs charging */
} led_pattern_t;

/**
 * @brief Configure the LED pin (LED off)
 */
void led_init(void);

/**
 * @brief Play a pattern in a background task
 *
 * If a pattern is still playing, this waits for it to finish first.
 */
void led_play(led_pattern_t pattern);

/**
 * @brief Block until the current pattern has finished
 */
void led_wait_done(void);

/**
 * @brief Wait for the current pattern, then release the pin so no current
 *        flows during deep sleep
 */
void led_off(void);

#endif /* LED_H */
