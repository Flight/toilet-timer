#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_event.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <string.h>

#include "global_event_group.h"

#include "wifi.h"

#define SSID CONFIG_WIFI_SSID
#define PASSWORD CONFIG_WIFI_PASSWORD
#define WIFI_CONNECTED_DELAY_MS 5000

#define IP_OBTAINED_BIT BIT0

static const char *TAG = "Wi-Fi";
static EventGroupHandle_t wifi_internal_event_group;

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
    xEventGroupClearBits(wifi_internal_event_group, IP_OBTAINED_BIT);
    ESP_LOGI(TAG, "Lost connection. Reconnecting...");
    xEventGroupSetBits(global_event_group, IS_WIFI_FAILED_BIT);
    esp_wifi_connect();
    break;

  case IP_EVENT_STA_GOT_IP:
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupClearBits(global_event_group, IS_WIFI_FAILED_BIT);
    xEventGroupSetBits(wifi_internal_event_group, IP_OBTAINED_BIT);
    break;

  default:
    break;
  }
}

void wifi_task(void *pvParameter)
{
  ESP_LOGI(TAG, "Wi-Fi task started");

  wifi_internal_event_group = xEventGroupCreate();

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
    // Wait for IP address to be obtained
    EventBits_t bits = xEventGroupWaitBits(wifi_internal_event_group,
                                           IP_OBTAINED_BIT,
                                           pdFALSE,
                                           pdTRUE,
                                           portMAX_DELAY);

    if (bits & IP_OBTAINED_BIT) {
      // Wi-Fi connected successfully, wait 5 seconds before setting WIFI_CONNECTED bit
      ESP_LOGI(TAG, "Waiting %d ms before activating WIFI_CONNECTED bit", WIFI_CONNECTED_DELAY_MS);
      vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECTED_DELAY_MS));

      xEventGroupSetBits(global_event_group, IS_WIFI_CONNECTED_BIT);
      ESP_LOGI(TAG, "WIFI_CONNECTED bit activated");

      // Wait for disconnection by checking if the bit gets cleared
      while (xEventGroupGetBits(wifi_internal_event_group) & IP_OBTAINED_BIT) {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }

      ESP_LOGI(TAG, "Disconnected, waiting for reconnection");
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
