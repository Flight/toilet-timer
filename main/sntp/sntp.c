#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <time.h>

#include "global_event_group.h"
#include "sntp.h"

static const char *TAG = "SNTP";
static bool sntp_initialized = false;

static void sync_time_with_sntp(void)
{
  const TickType_t sync_wait_ticks = pdMS_TO_TICKS(10000);

  if (!sntp_initialized) {
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    sntp_initialized = true;
  } else {
    esp_netif_sntp_start();
  }

  esp_err_t sync_err = esp_netif_sntp_sync_wait(sync_wait_ticks);
  if (sync_err == ESP_OK) {
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    gmtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "SNTP time (UTC): %04d-%02d-%02d %02d:%02d:%02d",
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

  ESP_LOGI(TAG, "Waiting for WIFI_CONNECTED bit");
  xEventGroupWaitBits(global_event_group, IS_WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

  ESP_LOGI(TAG, "Wi-Fi connected, syncing time with SNTP");
  sync_time_with_sntp();

  vTaskDelete(NULL);
}
