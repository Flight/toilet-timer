/**
 * @file trigger.c
 * @brief GPIO4 trigger button handling and timestamp management
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <stdio.h>
#include <time.h>

#include "trigger.h"
#include "../nvs_utils/nvs_utils.h"
#include "../time_utils/time_utils.h"

static const char *TAG = "trigger";

#define TRIGGER_GPIO GPIO_NUM_4
#define TRIGGER_DEBOUNCE_MS 200

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

void trigger_init_interrupt(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TRIGGER_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(TRIGGER_GPIO, trigger_isr_handler, NULL);

    ESP_LOGI(TAG, "GPIO4 interrupt configured");
}

void trigger_deinit_interrupt(void)
{
    gpio_isr_handler_remove(TRIGGER_GPIO);
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

void trigger_format_datetime(char *buf, size_t buf_size, int days_since_trigger, time_t trigger_timestamp)
{
    if (trigger_timestamp != 0) {
        struct tm trigger_tm = {0};
        localtime_r(&trigger_timestamp, &trigger_tm);

        if (days_since_trigger < 0) {
            /* Time not yet synced - show only saved date/time */
            snprintf(buf, buf_size,
                     "\n %02d-%02d-%04d\n  %02d:%02d:%02d",
                     trigger_tm.tm_mday,
                     trigger_tm.tm_mon + 1,
                     trigger_tm.tm_year + 1900,
                     trigger_tm.tm_hour,
                     trigger_tm.tm_min,
                     trigger_tm.tm_sec);
        } else if (days_since_trigger == 0) {
            snprintf(buf, buf_size,
                     "\n %02d-%02d-%04d\n  %02d:%02d:%02d\n  Сьогодні",
                     trigger_tm.tm_mday,
                     trigger_tm.tm_mon + 1,
                     trigger_tm.tm_year + 1900,
                     trigger_tm.tm_hour,
                     trigger_tm.tm_min,
                     trigger_tm.tm_sec);
        } else if (days_since_trigger == 1) {
            snprintf(buf, buf_size,
                     "\n %02d-%02d-%04d\n  %02d:%02d:%02d\n   Учора",
                     trigger_tm.tm_mday,
                     trigger_tm.tm_mon + 1,
                     trigger_tm.tm_year + 1900,
                     trigger_tm.tm_hour,
                     trigger_tm.tm_min,
                     trigger_tm.tm_sec);
        } else {
            snprintf(buf, buf_size,
                     "\n %02d-%02d-%04d\n  %02d:%02d:%02d\n %d %s тому",
                     trigger_tm.tm_mday,
                     trigger_tm.tm_mon + 1,
                     trigger_tm.tm_year + 1900,
                     trigger_tm.tm_hour,
                     trigger_tm.tm_min,
                     trigger_tm.tm_sec,
                     days_since_trigger,
                     time_utils_get_days_suffix_uk(days_since_trigger));
        }
    } else {
        /* No trigger record - show current time without days */
        time_t now = 0;
        struct tm now_tm = {0};
        time(&now);
        localtime_r(&now, &now_tm);

        snprintf(buf, buf_size,
                 "\n %02d-%02d-%04d\n  %02d:%02d:%02d",
                 now_tm.tm_mday,
                 now_tm.tm_mon + 1,
                 now_tm.tm_year + 1900,
                 now_tm.tm_hour,
                 now_tm.tm_min,
                 now_tm.tm_sec);
    }
}
