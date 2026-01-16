#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <nvs.h>
#include <time.h>

#include "global_event_group.h"
#include "sntp.h"

static const char *TAG = "SNTP";
static bool sntp_initialized = false;

static const char *NVS_SNTP_NAMESPACE = "sntp_info";
static const char *NVS_FIRST_SYNC_KEY = "first_sync";

bool sntp_check_first_sync_done(void)
{
  nvs_handle_t nvs_handle;
  uint8_t first_sync_done = 0;
  size_t required_size = sizeof(first_sync_done);

  esp_err_t err = nvs_open(NVS_SNTP_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    return false;
  }

  err = nvs_get_blob(nvs_handle, NVS_FIRST_SYNC_KEY, &first_sync_done, &required_size);
  nvs_close(nvs_handle);

  return (err == ESP_OK && first_sync_done == 1);
}

static void sntp_save_first_sync_done(void)
{
  nvs_handle_t nvs_handle;
  uint8_t first_sync_done = 1;

  esp_err_t err = nvs_open(NVS_SNTP_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to open NVS for writing first sync flag");
    return;
  }

  err = nvs_set_blob(nvs_handle, NVS_FIRST_SYNC_KEY, &first_sync_done, sizeof(first_sync_done));
  if (err == ESP_OK) {
    nvs_commit(nvs_handle);
    ESP_LOGI(TAG, "First SNTP sync flag saved to NVS");
  }
  nvs_close(nvs_handle);
}

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

  /* Check if we've ever synced before and set the bit */
  if (sntp_check_first_sync_done()) {
    ESP_LOGI(TAG, "First SNTP sync was done previously, setting IS_SNTP_FIRST_SYNC_DONE bit");
    xEventGroupSetBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE);
  }

  ESP_LOGI(TAG, "Waiting for WIFI_CONNECTED bit");
  xEventGroupWaitBits(global_event_group, IS_WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

  ESP_LOGI(TAG, "Wi-Fi connected, syncing time with SNTP");
  sync_time_with_sntp();

  /* Save the first sync flag if not already saved */
  if (!sntp_check_first_sync_done()) {
    sntp_save_first_sync_done();
    xEventGroupSetBits(global_event_group, IS_SNTP_FIRST_SYNC_DONE);
  }

  xEventGroupSetBits(global_event_group, IS_SNTP_SYNC_DONE);
  ESP_LOGI(TAG, "SNTP sync done");

  vTaskDelete(NULL);
}
