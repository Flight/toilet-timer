/**
 * @file main.c
 * @brief Toilet Timer - E-Paper Display Application
 *        (Waveshare ESP32-S3-ePaper-1.54)
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_attr.h>
#include <nvs_flash.h>
#include <time.h>

#include "global_constants.h"

#include "global_event_group.h"

#include "board_power/board_power.h"
#include "display_epaper/display.h"
#include "system_state/system_state.h"
#include "show_messages/show_messages.h"
#include "wifi/wifi.h"
#include "sntp/sntp.h"
#include "ota_update/ota_update.h"
#include "deep_sleep/deep_sleep.h"
#include "time_utils/time_utils.h"
#include "trigger/trigger.h"
#include "led/led.h"
#ifdef CONFIG_RTC_PCF85063_ENABLED
#include "rtc_pcf85063/rtc_pcf85063.h"
#endif

static const char *TAG = "toilet_timer";

EventGroupHandle_t global_event_group;

/* Record of how the previous boot started. It lives in no-init RTC memory, so
 * it survives deep sleep and ordinary resets (including the USB reset that
 * opening the serial console causes) and lets the next boot's log tell what
 * woke the device. Only a power loss clears it. */
#define WAKE_RECORD_MAGIC 0x57414B45

typedef struct {
    uint32_t magic;
    int cause;
    uint64_t ext1_mask;
    time_t boot_time;
} wake_record_t;

static RTC_NOINIT_ATTR wake_record_t s_previous_wake;

static const char *wakeup_cause_name(int cause)
{
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT1: return "EXT1 (GPIO)";
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "power-on/reset";
        default: return "other";
    }
}

static void log_and_store_wake_record(esp_sleep_wakeup_cause_t cause, uint64_t ext1_mask)
{
    if (s_previous_wake.magic == WAKE_RECORD_MAGIC) {
        struct tm boot_tm = {0};
        localtime_r(&s_previous_wake.boot_time, &boot_tm);
        ESP_LOGI(TAG, "Previous boot: %s, GPIO mask 0x%llx, at %04d-%02d-%02d %02d:%02d:%02d",
                 wakeup_cause_name(s_previous_wake.cause), s_previous_wake.ext1_mask,
                 boot_tm.tm_year + 1900, boot_tm.tm_mon + 1, boot_tm.tm_mday,
                 boot_tm.tm_hour, boot_tm.tm_min, boot_tm.tm_sec);
    }

    s_previous_wake.magic = WAKE_RECORD_MAGIC;
    s_previous_wake.cause = cause;
    s_previous_wake.ext1_mask = ext1_mask;
    time(&s_previous_wake.boot_time);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Toilet Timer");

    /* Keep the battery power latched before anything else, then power the display */
    board_power_init_early();
    display_enable_power_early();
    time_utils_init_timezone();

    /* Check wake-up cause */
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    bool reset_button_wakeup = false;
    bool sync_requested = false;
    uint64_t wakeup_gpio_mask = 0;
    switch (wakeup_cause) {
        case ESP_SLEEP_WAKEUP_EXT1: {
            wakeup_gpio_mask = esp_sleep_get_ext1_wakeup_status();
            ESP_LOGI(TAG, "Wake-up from deep sleep (EXT1 - GPIO)");
            ESP_LOGI(TAG, "Wake-up GPIO mask: 0x%llx", wakeup_gpio_mask);
            if (wakeup_gpio_mask & TRIGGER_BUTTON_MASK) {
                /* Short press: litter change recorded. Long press: sync and update check. */
                if (trigger_is_long_press(wakeup_gpio_mask)) {
                    sync_requested = true;
                    ESP_LOGI(TAG, "Key held: Wi-Fi sync and OTA check requested");
                } else {
                    reset_button_wakeup = true;
                    ESP_LOGI(TAG, "Key pressed: marking a litter change");
                }
            }
#ifdef CONFIG_RTC_PCF85063_ENABLED
            if (wakeup_gpio_mask & (1ULL << CONFIG_RTC_INT_GPIO)) {
                ESP_LOGI(TAG, "RTC alarm (GPIO%d) triggered wake-up - nightly refresh", CONFIG_RTC_INT_GPIO);
            }
#endif
            break;
        }
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wake-up from deep sleep (timer - nightly refresh)");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(TAG, "Power-on reset or other wake-up cause");
            break;
    }

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS (%s)", esp_err_to_name(nvs_err));
        return;
    }

    global_event_group = xEventGroupCreate();

#ifdef CONFIG_RTC_PCF85063_ENABLED
    /* Accurate time from the crystal-driven RTC chip, before anything reads the clock */
    if (rtc_pcf85063_init() == ESP_OK && rtc_pcf85063_restore_system_time() == ESP_OK) {
        xEventGroupSetBits(global_event_group, IS_RTC_TIME_VALID);
    }
#endif

    log_and_store_wake_record(wakeup_cause, wakeup_gpio_mask);

    if (reset_button_wakeup) {
        xEventGroupSetBits(global_event_group, IS_RESET_BUTTON_WAKEUP);
    }
    if (sync_requested) {
        xEventGroupSetBits(global_event_group, IS_SYNC_REQUESTED);
    }

    /* Wi-Fi policy: a long key press always connects (time sync + OTA check);
     * otherwise connect only when the time is unknown or the last sync is
     * older than CONFIG_TIME_SYNC_INTERVAL_DAYS. */
    bool sync_due = sntp_is_sync_due();
    bool wifi_available = sync_requested || sync_due;
    ESP_LOGI(TAG, "Wi-Fi %s (%s)", wifi_available ? "enabled" : "not needed",
             sync_requested ? "requested by key" : sync_due ? "time sync due" : "time is fresh");
    if (wifi_available) {
        xEventGroupSetBits(global_event_group, IS_WIFI_AVAILABLE);
    }

    // xTaskCreatePinnedToCore(&system_state_task, "System State", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&show_messages_task, "Show Messages", configMINIMAL_STACK_SIZE * 3, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&wifi_task, "Wi-Fi Keeper", configMINIMAL_STACK_SIZE * 3, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&sntp_task, "SNTP", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&ota_update_task, "OTA Update", configMINIMAL_STACK_SIZE * 8, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&wifi_disconnect_task, "Wi-Fi Disconnect", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
}
