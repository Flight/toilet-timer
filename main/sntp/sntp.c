#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <time.h>

#include "global_event_group.h"
#include "../nvs_utils/nvs_utils.h"
#include "../time_utils/time_utils.h"
#include "sntp.h"

static const char *TAG = "SNTP";
static bool sntp_initialized = false;

#define NVS_SNTP_NAMESPACE "sntp_info"
#define NVS_FIRST_SYNC_KEY "first_sync"

bool sntp_check_first_sync_done(void)
{
    uint8_t first_sync_done = 0;
    esp_err_t err = nvs_utils_read_u8(NVS_SNTP_NAMESPACE, NVS_FIRST_SYNC_KEY, &first_sync_done);
    return (err == ESP_OK && first_sync_done == 1);
}

static void sntp_save_first_sync_done(void)
{
    if (nvs_utils_write_u8(NVS_SNTP_NAMESPACE, NVS_FIRST_SYNC_KEY, 1) == ESP_OK) {
        ESP_LOGI(TAG, "First SNTP sync flag saved");
    }
}

static void sync_time_with_sntp(void)
{
    const TickType_t sync_wait_ticks = pdMS_TO_TICKS(10000);

    if (!sntp_initialized) {
        const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
        esp_netif_sntp_init(&config);
        sntp_initialized = true;
        ESP_LOGI(TAG, "SNTP server: %s", CONFIG_SNTP_TIME_SERVER);
    } else {
        esp_netif_sntp_start();
    }

    esp_err_t sync_err = esp_netif_sntp_sync_wait(sync_wait_ticks);
    if (sync_err == ESP_OK) {
        time_t now = 0;
        struct tm timeinfo = {0};
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "SNTP time (local): %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900,
                 timeinfo.tm_mon + 1,
                 timeinfo.tm_mday,
                 timeinfo.tm_hour,
                 timeinfo.tm_min,
                 timeinfo.tm_sec);
    } else {
        ESP_LOGW(TAG, "SNTP sync failed (%s)", esp_err_to_name(sync_err));
    }
}

void sntp_task(void *pvParameter)
{
    ESP_LOGI(TAG, "SNTP task started");

    /* Set timezone immediately */
    time_utils_init_timezone();

    /* Check if we've ever synced before */
    if (sntp_check_first_sync_done()) {
        ESP_LOGI(TAG, "Previous sync found, setting IS_SNTP_FIRST_SYNC_DONE");
        xEventGroupSetBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE);
    }

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
    xEventGroupWaitBits(global_event_group, IS_WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Wi-Fi connected, syncing time");
    sync_time_with_sntp();

    /* Save first sync flag if not already saved */
    if (!sntp_check_first_sync_done()) {
        sntp_save_first_sync_done();
        xEventGroupSetBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE);
    }

    xEventGroupSetBits(global_event_group, IS_SNTP_SYNC_DONE);
    ESP_LOGI(TAG, "SNTP sync done");

    vTaskDelete(NULL);
}
