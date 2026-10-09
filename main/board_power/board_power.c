/**
 * @file board_power.c
 * @brief Board power control for the Waveshare ESP32-S3-ePaper-1.54
 */

#include "sdkconfig.h"
#include "board_power.h"
#include <driver/gpio.h>
#include <esp_log.h>

static const char *TAG = "board_power";

#define LATCH_ON_LEVEL      1 /* BAT_Control HIGH keeps the 3.3V rail enabled */

/* Audio supply switch (PA_EN) is active LOW. The codec normally stays powered
 * because it shares the I2C bus with the RTC and the SHTC3 sensor. */
#ifdef CONFIG_BOARD_AUDIO_POWER_KEEP_ON
#define AUDIO_LEVEL         0
#define AUDIO_STATE_TEXT    "on"
#else
#define AUDIO_LEVEL         1
#define AUDIO_STATE_TEXT    "off"
#endif

#ifdef CONFIG_EPD_POWER_ACTIVE_LOW
#define EPD_OFF_LEVEL       1
#else
#define EPD_OFF_LEVEL       0
#endif

/* Configure a pin as output without ever glitching it to the wrong level */
static void drive_output(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(pin, level);
}

void board_power_init_early(void)
{
    /* Battery latch: configure HIGH *before* releasing the deep-sleep hold so
     * the rail never drops while the pad switches from "held" to "driven". */
    drive_output(CONFIG_BOARD_POWER_LATCH_GPIO, LATCH_ON_LEVEL);
    gpio_hold_dis(CONFIG_BOARD_POWER_LATCH_GPIO);

    /* Audio codec supply (see CONFIG_BOARD_AUDIO_POWER_KEEP_ON) */
    drive_output(CONFIG_BOARD_AUDIO_POWER_GPIO, AUDIO_LEVEL);
    gpio_hold_dis(CONFIG_BOARD_AUDIO_POWER_GPIO);

    gpio_deep_sleep_hold_dis();

    ESP_LOGI(TAG, "Battery latch GPIO%d asserted, audio power GPIO%d %s",
             CONFIG_BOARD_POWER_LATCH_GPIO, CONFIG_BOARD_AUDIO_POWER_GPIO, AUDIO_STATE_TEXT);
}

void board_power_prepare_for_deep_sleep(void)
{
    /* E-paper supply off: the panel keeps its image without power */
    gpio_set_level(CONFIG_EPD_PIN_POWER, EPD_OFF_LEVEL);
    gpio_hold_en(CONFIG_EPD_PIN_POWER);

    gpio_set_level(CONFIG_BOARD_AUDIO_POWER_GPIO, AUDIO_LEVEL);
    gpio_hold_en(CONFIG_BOARD_AUDIO_POWER_GPIO);

    gpio_set_level(CONFIG_BOARD_POWER_LATCH_GPIO, LATCH_ON_LEVEL);
    gpio_hold_en(CONFIG_BOARD_POWER_LATCH_GPIO);

    /* Required for the hold to survive deep sleep on digital (non-RTC) pads */
    gpio_deep_sleep_hold_en();

    ESP_LOGI(TAG, "Power pins held for deep sleep (latch on, e-paper off, audio %s)", AUDIO_STATE_TEXT);
}
