#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_netif_sntp.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <string.h>
#include <time.h>

#include "global_event_group.h"

#include "wifi.h"

#define SSID CONFIG_WIFI_SSID
#define PASSWORD CONFIG_WIFI_PASSWORD

static const char *TAG = "Wi-Fi";
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

static void client_mode_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    return;
  }

  switch (event_id)
  {
  case WIFI_EVENT_STA_START:
    ESP_LOGI(TAG, "Connecting to SSID: %s", SSID);
    break;

  case WIFI_EVENT_STA_CONNECTED:
    ESP_LOGI(TAG, "Connected to SSID: %s", SSID);
    break;

  case WIFI_EVENT_STA_DISCONNECTED:
    xEventGroupClearBits(global_event_group, IS_WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "Lost connection.");
    xEventGroupSetBits(global_event_group, IS_WIFI_FAILED_BIT);
    break;

  case IP_EVENT_STA_GOT_IP:
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupClearBits(global_event_group, IS_WIFI_FAILED_BIT);
    xEventGroupSetBits(global_event_group, IS_WIFI_CONNECTED_BIT);
    sync_time_with_sntp();
    break;

  default:
    break;
  }
}

void wifi_task(void *pvParameter)
{
  ESP_LOGI(TAG, "Wi-Fi task started");
  esp_netif_init();
  esp_event_loop_create_default();

  esp_netif_create_default_wifi_sta();
  wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&wifi_initiation);

  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, client_mode_event_handler, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, client_mode_event_handler, NULL);

  wifi_config_t client_configuration = {
      .sta = {
          .threshold.authmode = WIFI_AUTH_WPA2_PSK,
      },
  };

  strlcpy((char *)client_configuration.sta.ssid, SSID, sizeof(client_configuration.sta.ssid));
  strlcpy((char *)client_configuration.sta.password, PASSWORD, sizeof(client_configuration.sta.password));

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &client_configuration);
  esp_wifi_set_ps(WIFI_PS_NONE);

  xEventGroupClearBits(global_event_group, IS_WIFI_FAILED_BIT);
  xEventGroupClearBits(global_event_group, IS_WIFI_CONNECTED_BIT);

  esp_wifi_start();

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
