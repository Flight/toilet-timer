/**
 * @file board_power.h
 * @brief Board power control for the Waveshare ESP32-S3-ePaper-1.54
 *
 * The board runs from a lithium battery through a power latch: pressing the
 * PWR key enables the 3.3V rail, and the firmware must assert BAT_Control
 * (GPIO17) to keep it enabled after the key is released. The pin is held
 * through deep sleep so the device keeps its power between wake-ups.
 *
 * Power budget (from the board schematic): BAT_Control feeds the base of
 * the latch transistor through a 1 kOhm resistor, so a pin driven HIGH
 * sinks about 2.6 mA for as long as the board is on - the single biggest
 * consumer in deep sleep. CONFIG_BOARD_POWER_LATCH_WEAK replaces the HIGH
 * drive with the internal pull-up (~60 uA) and verifies itself across
 * reboots. The audio rail's load switch wastes 0.33 mA in its gate pull-up
 * while on, so it is always switched off for deep sleep.
 */

#ifndef BOARD_POWER_H
#define BOARD_POWER_H

#include <stdbool.h>

/**
 * @brief Assert the battery power latch (driven HIGH) and set the peripheral rails
 *
 * Must be the first thing called in app_main(): on battery power the board
 * switches itself off as soon as the PWR key is released otherwise.
 * Also releases the GPIO holds left over from the previous deep sleep.
 */
void board_power_init_early(void);

/**
 * @brief Decide how the latch is held for the rest of this wake-up
 *
 * Needs NVS. With CONFIG_BOARD_POWER_LATCH_WEAK it evaluates how the previous
 * wake-up ended (a power-on reset after running on the weak latch means the
 * latch dropped), stores the verdict, and switches BAT_Control to the
 * internal pull-up unless the weak latch has failed on this board.
 */
void board_power_latch_apply_policy(void);

/**
 * @brief Whether BAT_Control is currently held by the internal pull-up
 */
bool board_power_latch_is_weak(void);

/**
 * @brief Freeze the power-control pins for deep sleep
 *
 * Keeps the battery latch held (strong or weak), switches the e-paper and
 * the audio rail off and holds them. Call right before esp_deep_sleep_start().
 */
void board_power_prepare_for_deep_sleep(void);

#endif /* BOARD_POWER_H */
