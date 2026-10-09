/**
 * @file trigger.c
 * @brief Key handling (PWR and BOOT) and last-change timestamp management
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <stdio.h>
#include <time.h>

#include "trigger.h"
#include "../nvs_utils/nvs_utils.h"
#include "../time_utils/time_utils.h"

static const char *TAG = "trigger";

#define TRIGGER_DEBOUNCE_MS 200
#define LONG_PRESS_POLL_MS 20

/* NVS key names are kept for compatibility with set_manual_timestamp/ */
#define NVS_TRIGGER_NAMESPACE "trigger_info"
#define NVS_LAST_TRIGGER_KEY "last_gpio4"

static volatile bool s_triggered = false;
static TickType_t s_last_press_time = 0;

static void IRAM_ATTR trigger_isr_handler(void *arg)
{
    TickType_t current_time = xTaskGetTickCountFromISR();
    if ((current_time - s_last_press_time) > pdMS_TO_TICKS(TRIGGER_DEBOUNCE_MS)) {
        s_triggered = true;
        s_last_press_time = current_time;
    }
}

static gpio_config_t button_input_config(uint64_t mask, gpio_int_type_t intr_type)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = intr_type
    };
    return io_conf;
}

bool trigger_is_long_press(uint64_t wakeup_mask)
{
    uint64_t keys = wakeup_mask & TRIGGER_BUTTON_MASK;
    if (keys == 0) {
        return false;
    }

    gpio_config_t io_conf = button_input_config(keys, GPIO_INTR_DISABLE);
    gpio_config(&io_conf);

    const int64_t threshold_us = (int64_t)CONFIG_BUTTON_LONG_PRESS_MS * 1000;
    while (esp_timer_get_time() < threshold_us) {
        bool any_held = false;
        for (int pin = 0; pin < 64; pin++) {
            if ((keys & (1ULL << pin)) && gpio_get_level((gpio_num_t)pin) == 0) {
                any_held = true;
                break;
            }
        }
        if (!any_held) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(LONG_PRESS_POLL_MS));
    }

    ESP_LOGI(TAG, "Long key press detected");
    return true;
}

void trigger_init_interrupt(void)
{
    gpio_config_t io_conf = button_input_config(TRIGGER_BUTTON_MASK, GPIO_INTR_NEGEDGE);
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(CONFIG_BUTTON_PWR_GPIO, trigger_isr_handler, NULL);
    gpio_isr_handler_add(CONFIG_BUTTON_BOOT_GPIO, trigger_isr_handler, NULL);

    ESP_LOGI(TAG, "Key interrupts configured on GPIO%d and GPIO%d", CONFIG_BUTTON_PWR_GPIO, CONFIG_BUTTON_BOOT_GPIO);
}

void trigger_deinit_interrupt(void)
{
    gpio_isr_handler_remove(CONFIG_BUTTON_PWR_GPIO);
    gpio_isr_handler_remove(CONFIG_BUTTON_BOOT_GPIO);
}

bool trigger_check_and_clear(void)
{
    if (s_triggered) {
        s_triggered = false;
        return true;
    }
    return false;
}

time_t trigger_get_last_timestamp(void)
{
    time_t timestamp = 0;
    esp_err_t err = nvs_utils_read_timestamp(NVS_TRIGGER_NAMESPACE, NVS_LAST_TRIGGER_KEY, &timestamp);
    if (err != ESP_OK) {
        return 0;
    }
    return timestamp;
}

esp_err_t trigger_save_timestamp(time_t timestamp)
{
    esp_err_t err = nvs_utils_write_timestamp(NVS_TRIGGER_NAMESPACE, NVS_LAST_TRIGGER_KEY, timestamp);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Trigger timestamp saved: %ld", (long)timestamp);
    }
    return err;
}

void trigger_format_days_caption(char *buf, size_t buf_size, int days_since_trigger)
{
    snprintf(buf, buf_size, "%s тому", time_utils_get_days_suffix_uk(days_since_trigger));
}

void trigger_format_datetime(char *buf, size_t buf_size, time_t timestamp)
{
    if (timestamp == 0) {
        time(&timestamp);
    }

    struct tm timestamp_tm = {0};
    localtime_r(&timestamp, &timestamp_tm);

    snprintf(buf, buf_size,
             "%02d-%02d-%04d %02d:%02d",
             timestamp_tm.tm_mday,
             timestamp_tm.tm_mon + 1,
             timestamp_tm.tm_year + 1900,
             timestamp_tm.tm_hour,
             timestamp_tm.tm_min);
}
