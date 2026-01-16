/**
 * @file main.c
 * @brief Toilet Timer - E-Paper Display Application
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <nvs_flash.h>

#include "global_constants.h"

#include "global_event_group.h"

#include "display_epaper/display.h"
#include "system_state/system_state.h"
#include "battery_level/battery_level.h"
#include "show_messages/show_messages.h"
#include "wifi/wifi.h"
#include "sntp/sntp.h"
#include "ota_update/ota_update.h"
#include "deep_sleep/deep_sleep.h"

static const char *TAG = "toilet_timer";

EventGroupHandle_t global_event_group;

static void wifi_disconnect_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Waiting for OTA check and SNTP sync to complete...");
    xEventGroupWaitBits(global_event_group, IS_OTA_CHECK_DONE | IS_SNTP_SYNC_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "OTA and SNTP done, disconnecting Wi-Fi");
    wifi_stop();

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Toilet Timer");

    /* Check wake-up cause */
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    switch (wakeup_cause) {
        case ESP_SLEEP_WAKEUP_EXT1:
            ESP_LOGI(TAG, "Wake-up from deep sleep (EXT1 - GPIO button)");
            ESP_LOGI(TAG, "Wake-up GPIO mask: 0x%llx", esp_sleep_get_ext1_wakeup_status());
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

    // xTaskCreatePinnedToCore(&system_state_task, "System State", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&show_messages_task, "Show Messages", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&battery_level_task, "Battery", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&wifi_task, "Wi-Fi Keeper", configMINIMAL_STACK_SIZE * 3, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&sntp_task, "SNTP", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&ota_update_task, "OTA Update", configMINIMAL_STACK_SIZE * 8, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(&wifi_disconnect_task, "Wi-Fi Disconnect", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL, 1);
}
