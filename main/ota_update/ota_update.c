#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <esp_app_desc.h>
#include <esp_mac.h>
#include <string.h>

#include "ota_update.h"
#include "global_event_group.h"

#define FIRMWARE_UPGRADE_URL CONFIG_ESP32_FIRMWARE_UPGRADE_URL
#define HASH_LEN 32

static const char *TAG = "OTA Update";

char *global_running_firmware_version = "Pending";

nvs_handle_t ota_storage_handle;
char *NVS_OTA_STORAGE_NAMESPACE = "ota_info";
char *NVS_OTA_FIRMWARE_HASH_KEY = "firmware_hash";

static uint8_t esp32_mac_address[6] = {0};
static char esp32_mac_address_string[18];

static uint8_t sha_256_current[HASH_LEN] = {0};
static uint8_t sha_256_stored[HASH_LEN] = {0};
static size_t stored_hash_size = HASH_LEN;

static const esp_partition_t *running_partition;
static esp_app_desc_t running_app_info;

#ifdef CONFIG_IS_ESP32_FIRMWARE_UPGRADE_ENABLED
extern const uint8_t server_cert_pem_start[] asm("_binary_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_cert_pem_end");
static const uint8_t DELAY_BEFORE_UPDATE_CHECK_SECS = 10;
#endif

static void print_sha256(const uint8_t *image_hash, const char *label)
{
  char hash_print[HASH_LEN * 2 + 1];
  hash_print[HASH_LEN * 2] = 0;
  for (uint8_t i = 0; i < HASH_LEN; ++i)
  {
    sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
  }
  ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void check_current_firmware(void)
{
  ESP_LOGI(TAG, "Checking current firmware...");

  // Read the stored hash
  nvs_open(NVS_OTA_STORAGE_NAMESPACE, NVS_READWRITE, &ota_storage_handle);
  esp_err_t err = nvs_get_blob(ota_storage_handle, NVS_OTA_FIRMWARE_HASH_KEY, &sha_256_stored, &stored_hash_size);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGE(TAG, "Failed to read hash from NVS: %s", esp_err_to_name(err));
    return;
  }

  print_sha256(sha_256_stored, "Stored firmware hash:");
  print_sha256(sha_256_current, "Current firmware hash:");

  // Check if the running partition is the factory partition
  bool is_factory_partition = running_partition->type == ESP_PARTITION_TYPE_APP &&
                               running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY;

  if (is_factory_partition)
  {
    ESP_LOGI(TAG, "Current partition is factory partition");
    // If the stored hash is not the same as the factory one, save the current hash
    if (stored_hash_size != HASH_LEN || memcmp(sha_256_current, sha_256_stored, HASH_LEN) != 0)
    {
      nvs_set_blob(ota_storage_handle, NVS_OTA_FIRMWARE_HASH_KEY, &sha_256_current, HASH_LEN);
      nvs_commit(ota_storage_handle);
      ESP_LOGI(TAG, "Stored new firmware hash in NVS");
    }
    return;
  }

  // If the stored hash is the same size and matches the current one
  // We can mark it as valid and cancel the rollback
  if (err == ESP_ERR_NVS_NOT_FOUND || (stored_hash_size == HASH_LEN && memcmp(sha_256_current, sha_256_stored, HASH_LEN) == 0))
  {
    esp_ota_img_states_t ota_state;
    esp_ota_get_state_partition(running_partition, &ota_state);

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
      ESP_LOGI(TAG, "Diagnostics completed! Marking partition as valid and cancelling rollback");
      if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
      {
        ESP_LOGI(TAG, "Rollback cancelled successfully");
        esp_ota_erase_last_boot_app_partition();
        ESP_LOGI(TAG, "Erased old partition");
      }
      else
      {
        ESP_LOGE(TAG, "Failed to cancel rollback");
      }
    }
  }
}

static void get_running_firmware_info(void)
{
  running_partition = esp_ota_get_running_partition();
  esp_err_t err = esp_partition_get_sha256(running_partition, sha_256_current);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to get partition SHA256");
    vTaskDelete(NULL);
  }

  if (esp_ota_get_partition_description(running_partition, &running_app_info) == ESP_OK)
  {
    global_running_firmware_version = running_app_info.version;
    ESP_LOGI(TAG, "Running firmware version: %s", global_running_firmware_version);
  }
}

#ifdef CONFIG_IS_ESP32_FIRMWARE_UPGRADE_ENABLED

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
  if (new_app_info == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Running firmware version: %s", global_running_firmware_version);
  ESP_LOGI(TAG, "New firmware version: %s", new_app_info->version);

  if (strcmp(new_app_info->version, global_running_firmware_version) == 0)
  {
    ESP_LOGW(TAG, "Current version matches new version. Skipping update.");
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t http_client_init_callback(esp_http_client_handle_t http_client)
{
  esp_http_client_set_header(http_client, "ESP32-MAC", esp32_mac_address_string);
  return ESP_OK;
}

static void check_for_esp32_updates(void)
{
  ESP_LOGI(TAG, "Starting OTA update check...");

  esp_http_client_config_t http_config = {
      .url = FIRMWARE_UPGRADE_URL,
      .cert_pem = (char *)server_cert_pem_start,
      .keep_alive_enable = true,
      .timeout_ms = 10000,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &http_config,
      .http_client_init_cb = http_client_init_callback,
  };

  esp_https_ota_handle_t https_ota_handle = NULL;
  esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
    return;
  }

  // Validate the new firmware version
  esp_app_desc_t new_app_info;
  err = esp_https_ota_get_img_desc(https_ota_handle, &new_app_info);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to get image description: %s", esp_err_to_name(err));
    esp_https_ota_abort(https_ota_handle);
    return;
  }

  err = validate_image_header(&new_app_info);
  if (err != ESP_OK)
  {
    ESP_LOGI(TAG, "Image validation failed, aborting OTA");
    esp_https_ota_abort(https_ota_handle);
    return;
  }

  // Perform the OTA update
  xEventGroupSetBits(global_event_group, IS_OTA_UPDATE_RUNNING);

  while (1)
  {
    err = esp_https_ota_perform(https_ota_handle);
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
    {
      break;
    }
    ESP_LOGD(TAG, "Downloaded %d bytes", esp_https_ota_get_image_len_read(https_ota_handle));
  }

  if (!esp_https_ota_is_complete_data_received(https_ota_handle))
  {
    ESP_LOGE(TAG, "Complete data was not received");
    esp_https_ota_abort(https_ota_handle);
    xEventGroupClearBits(global_event_group, IS_OTA_UPDATE_RUNNING);
    return;
  }

  err = esp_https_ota_finish(https_ota_handle);
  if (err == ESP_OK)
  {
    ESP_LOGI(TAG, "OTA update successful!");

    // Store the new firmware hash
    uint8_t sha_256_boot[HASH_LEN] = {0};
    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
    if (boot_partition != NULL)
    {
      esp_partition_get_sha256(boot_partition, sha_256_boot);
      print_sha256(sha_256_boot, "New firmware hash:");

      nvs_set_blob(ota_storage_handle, NVS_OTA_FIRMWARE_HASH_KEY, &sha_256_boot, HASH_LEN);
      nvs_commit(ota_storage_handle);
      ESP_LOGI(TAG, "Stored new firmware hash in NVS");
    }

    nvs_close(ota_storage_handle);
    ESP_LOGI(TAG, "Restarting to new firmware...");
    esp_restart();
  }
  else
  {
    ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
    xEventGroupClearBits(global_event_group, IS_OTA_UPDATE_RUNNING);
  }
}

#endif // CONFIG_IS_ESP32_FIRMWARE_UPGRADE_ENABLED

void ota_update_task(void *pvParameter)
{
  get_running_firmware_info();
  check_current_firmware();

#ifdef CONFIG_IS_ESP32_FIRMWARE_UPGRADE_ENABLED
  ESP_LOGI(TAG, "OTA Updates enabled");
  ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
  xEventGroupWaitBits(global_event_group, IS_WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

  vTaskDelay(1000 * DELAY_BEFORE_UPDATE_CHECK_SECS / portTICK_PERIOD_MS);

  esp_read_mac(esp32_mac_address, ESP_MAC_EFUSE_FACTORY);
  snprintf(esp32_mac_address_string, sizeof(esp32_mac_address_string), "%02X:%02X:%02X:%02X:%02X:%02X",
           esp32_mac_address[0], esp32_mac_address[1], esp32_mac_address[2],
           esp32_mac_address[3], esp32_mac_address[4], esp32_mac_address[5]);

  check_for_esp32_updates();
  xEventGroupClearBits(global_event_group, IS_OTA_UPDATE_RUNNING);

  ESP_LOGI(TAG, "OTA check completed");
#else
  ESP_LOGW(TAG, "OTA Updates disabled in SDK config");
#endif

  vTaskDelete(NULL);
}
