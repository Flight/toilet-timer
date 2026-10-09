/**
 * @file led.c
 * @brief Green status LED feedback (active LOW on the Waveshare board)
 *
 * The LED cathode is on the GPIO and the anode is pulled to 3.3 V through
 * 24 kOhm, so driving the pin LOW lights it with ~0.1 mA. Leaving the pin as
 * an input keeps the LED dark without any current.
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_log.h>

#include "led.h"

static const char *TAG = "led";

#ifdef CONFIG_LED_FEEDBACK_ENABLED

#define LED_ON_LEVEL  0
#define LED_OFF_LEVEL 1

typedef struct {
    int times;
    int on_ms;
    int off_ms;
} led_sequence_t;

static const led_sequence_t s_sequences[] = {
    [LED_PATTERN_CHANGE_RECORDED] = { .times = 5,  .on_ms = 500,  .off_ms = 500 }, /* 5 s */
    [LED_PATTERN_SYNC_REQUESTED]  = { .times = 1,  .on_ms = 1000, .off_ms = 0 },
    [LED_PATTERN_LOW_BATTERY]     = { .times = 50, .on_ms = 100,  .off_ms = 100 }, /* 10 s */
};

static bool s_initialized = false;
static volatile bool s_running = false;
static SemaphoreHandle_t s_done = NULL;

void led_init(void)
{
    if (s_done == NULL) {
        s_done = xSemaphoreCreateBinary();
    }

    gpio_set_level(CONFIG_LED_GREEN_GPIO, LED_OFF_LEVEL);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_LED_GREEN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_LED_GREEN_GPIO, LED_OFF_LEVEL);
    s_initialized = true;
}

static void led_task(void *arg)
{
    const led_sequence_t *seq = (const led_sequence_t *)arg;

    for (int i = 0; i < seq->times; i++) {
        gpio_set_level(CONFIG_LED_GREEN_GPIO, LED_ON_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(seq->on_ms));
        gpio_set_level(CONFIG_LED_GREEN_GPIO, LED_OFF_LEVEL);
        if (i < seq->times - 1 && seq->off_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(seq->off_ms));
        }
    }

    s_running = false;
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

void led_play(led_pattern_t pattern)
{
    if (pattern >= sizeof(s_sequences) / sizeof(s_sequences[0])) {
        return;
    }
    if (!s_initialized) {
        led_init();
    }

    led_wait_done();

    s_running = true;
    if (xTaskCreate(led_task, "led", 2048, (void *)&s_sequences[pattern], 1, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start LED task");
        s_running = false;
    }
}

void led_wait_done(void)
{
    if (s_running && s_done != NULL) {
        xSemaphoreTake(s_done, portMAX_DELAY);
    }
}

void led_off(void)
{
    led_wait_done();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_LED_GREEN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    s_initialized = false;
    ESP_LOGD(TAG, "LED released");
}

#else /* CONFIG_LED_FEEDBACK_ENABLED */

void led_init(void) {}
void led_play(led_pattern_t pattern) { (void)pattern; }
void led_wait_done(void) {}
void led_off(void) {}

#endif /* CONFIG_LED_FEEDBACK_ENABLED */
