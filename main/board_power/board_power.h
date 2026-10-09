/**
 * @file board_power.h
 * @brief Board power control for the Waveshare ESP32-S3-ePaper-1.54
 *
 * The board runs from a lithium battery through a power latch: pressing the
 * PWR key enables the 3.3V rail, and the firmware must drive BAT_Control
 * (GPIO17) HIGH to keep it enabled after the key is released. The pin is held
 * HIGH through deep sleep so the device keeps its power between wake-ups.
 */

#ifndef BOARD_POWER_H
#define BOARD_POWER_H

/**
 * @brief Assert the battery power latch and switch unused peripherals off
 *
 * Must be the first thing called in app_main(): on battery power the board
 * switches itself off as soon as the PWR key is released otherwise.
 * Also releases the GPIO holds left over from the previous deep sleep.
 */
void board_power_init_early(void);

/**
 * @brief Freeze the power-control pins for deep sleep
 *
 * Keeps the battery latch HIGH, the e-paper supply off and the audio codec
 * off while the chip sleeps. Call right before esp_deep_sleep_start().
 */
void board_power_prepare_for_deep_sleep(void);

#endif /* BOARD_POWER_H */
