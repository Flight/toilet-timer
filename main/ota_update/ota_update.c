#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_app_desc.h>
#include <string.h>
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <esp_mac.h>

#include "global_event_group.h"

#define FIRMWARE_UPGRADE_URL CONFIG_ESP32_FIRMWARE_UPGRADE_URL
#define HASH_LEN 32
#define OTA_BUF_SIZE 256

char *global_running_firmware_version = "Pending";

static const char *TAG = "OTA FW Update";

nvs_handle_t ota_storage_handle;
char *NVS_OTA_STORAGE_NAMESPACE = "ota_info";
char *NVS_OTA_FIRMWARE_HASH_KEY = "firmware_hash";
char *NVS_OTA_ACCESS_CODE_KEY = "access_code";

#ifdef CONFIG_IS_ESP32_FIRMWARE_UPGRADE_ENABLED
extern const uint8_t server_cert_pem_start[] asm("_binary_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_cert_pem_end");

static const uint8_t DELAY_BEFORE_UPDATE_CHECK_SECS = 10;
#endif

#ifdef CONFIG_IS_EXTERNAL_FIRMWARE_UPGRADE_ENABLED
#define CHUNK_SIZE 4096
static char EXTERNAL_FIRMWARE_UPGRADE_BASE_URL[128] = CONFIG_EXTERNAL_FIRMWARE_UPGRADE_BASE_URL;
static char EXTERNAL_FIRMWARE_UPGRADE_BINARY_NAME[128] = CONFIG_EXTERNAL_FIRMWARE_UPGRADE_BINARY_NAME;
static char EXTERNAL_FIRMWARE_UPGRADE_KEY_NAME[128] = CONFIG_EXTERNAL_FIRMWARE_UPGRADE_KEY_NAME;
static char EXTERNAL_FIRMWARE_UPGRADE_VERSION_NAME[128] = CONFIG_EXTERNAL_FIRMWARE_UPGRADE_VERSION_NAME;
#endif

static const esp_partition_t *running_partition;
static esp_app_desc_t running_app_info;

static size_t stored_hash_size = HASH_LEN;
static uint8_t sha_256_current[HASH_LEN] = {0};
static uint8_t sha_256_stored[HASH_LEN] = {0};

#ifdef CONFIG_IS_ESP32_FIRMWARE_UPGRADE_ENABLED
static uint8_t sha_256_boot[HASH_LEN] = {0};
#endif

static uint8_t esp32_mac_address[6] = {0};
static char esp32_mac_address_string[18];

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

  print_sha256(sha_256_stored, "Stored firmware hash: ");
  print_sha256(sha_256_current, "Current firmware hash: ");

  // Check if the running partition is the factory partition
  bool is_factory_partition = running_partition->type == ESP_PARTITION_TYPE_APP && running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY;

  if (is_factory_partition)
  {
    ESP_LOGI(TAG, "The current partition is factory one.");
    // If the stored hash is not the same as the factory one, save the current hash
    if (stored_hash_size != HASH_LEN || memcmp(sha_256_current, sha_256_stored, HASH_LEN) != 0)
    {
      nvs_set_blob(ota_storage_handle, NVS_OTA_FIRMWARE_HASH_KEY, &sha_256_current, HASH_LEN);
      nvs_commit(ota_storage_handle);
      ESP_LOGI(TAG, "Stored new firmware hash in NVS.");
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
      ESP_LOGI(TAG, "Diagnostics completed successfully! Marking partition as valid and cancelling rollback.");
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

void get_running_firmware_info()
{
  running_partition = esp_ota_get_running_partition();
  esp_err_t err = esp_partition_get_sha256(running_partition, sha_256_current);
  if (err != ESP_OK)
  {
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

  if (memcmp(new_app_info->version, global_running_firmware_version, sizeof(new_app_info->version)) == 0)
  {
    ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update. (%s)", global_running_firmware_version);
    return ESP_FAIL;
  }
  else
  {
    ESP_LOGI(TAG, "Running firmware version: %s", global_running_firmware_version);
    ESP_LOGI(TAG, "New firmware version: %s", new_app_info->version);
  }

  return ESP_OK;
}

static esp_err_t http_client_init_callback(esp_http_client_handle_t http_client)
{
  esp_http_client_set_header(http_client, "ESP32-MAC", esp32_mac_address_string);
  esp_http_client_set_header(http_client, "Access-Code", ota_access_code);

  return ESP_OK;
}

static esp_err_t initialize_https_ota(esp_https_ota_handle_t *handle)
{
  esp_http_client_config_t config = {
      .url = FIRMWARE_UPGRADE_URL,
      .cert_pem = (char *)server_cert_pem_start,
      .keep_alive_enable = true,
      .buffer_size_tx = DEFAULT_HTTP_BUF_SIZE * 4,
  };
  esp_https_ota_config_t ota_config = {
      .http_config = &config,
      .http_client_init_cb = http_client_init_callback,
  };

  return esp_https_ota_begin(&ota_config, handle);
}

static esp_err_t perform_ota_update(esp_https_ota_handle_t handle)
{
  esp_err_t err;
  while (1)
  {
    err = esp_https_ota_perform(handle);
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
    {
      break;
    }
    ESP_LOGD(TAG, "Image bytes read: %d", esp_https_ota_get_image_len_read(handle));
  }
  return err;
}

static void on_ota_failed(esp_https_ota_handle_t handle)
{
  if (handle)
  {
    esp_https_ota_abort(handle);
  }
  nvs_close(ota_storage_handle);
  vTaskDelete(NULL);
}

static void check_for_esp32_updates()
{
  esp_https_ota_handle_t https_ota_handle = NULL;
  esp_err_t err = initialize_https_ota(&https_ota_handle);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "HTTPS Init failed");
    on_ota_failed(https_ota_handle);
    return;
  }

  esp_app_desc_t app_desc;
  err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Image description read failed");
    on_ota_failed(https_ota_handle);
    return;
  }
  err = validate_image_header(&app_desc);
  if (err != ESP_OK)
  {
    return;
  }

  xEventGroupSetBits(global_event_group, IS_OTA_UPDATE_RUNNING);
  err = perform_ota_update(https_ota_handle);
  if (esp_https_ota_is_complete_data_received(https_ota_handle) != true)
  {
    ESP_LOGE(TAG, "Complete data was not received.");
    on_ota_failed(https_ota_handle);
    return;
  }

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Upgrade failed 0x%x", err);
    on_ota_failed(https_ota_handle);
    return;
  }

  esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
  if (ota_finish_err == ESP_OK)
  {
    ESP_LOGI(TAG, "OTA Update successful!");

    // If update was successful, store the new hash
    esp_partition_get_sha256(esp_ota_get_boot_partition(), sha_256_boot);
    print_sha256(sha_256_boot, "New hash: ");
    nvs_set_blob(ota_storage_handle, NVS_OTA_FIRMWARE_HASH_KEY, &sha_256_boot, HASH_LEN);
    nvs_commit(ota_storage_handle);
    nvs_close(ota_storage_handle);
    ESP_LOGI(TAG, "Stored new firmware hash in NVS.");
    ESP_LOGI(TAG, "Restarting to the new firmware...");
    esp_restart();
  }
  else
  {
    ESP_LOGE(TAG, "Upgrade failed 0x%x", ota_finish_err);
    on_ota_failed(https_ota_handle);
  }
}

void remove_file_from_flash(const char *file_path)
{
  ESP_LOGI(TAG, "Removing file %s from FLASH...", file_path);
  struct stat st;
  if (stat(file_path, &st) == 0)
  {
    int ret = unlink(file_path);
    if (ret == 0)
    {
      ESP_LOGI(TAG, "File %s deleted successfully", file_path);
    }
    else
    {
      ESP_LOGE(TAG, "Failed to delete file %s", file_path);
    }
  }
  else
  {
    ESP_LOGE(TAG, "File %s does not exist", file_path);
  }
}

char redirect_location[1024];
esp_err_t download_to_flash_http_event_handler(esp_http_client_event_t *evt)
{
  if (evt->event_id == HTTP_EVENT_ON_HEADER)
  {
    if (strcmp(evt->header_key, "location") == 0)
    {
      sprintf(redirect_location, "%s", evt->header_value);
    }
  }
  return ESP_OK;
}

static void download_file_to_flash(const char *file_name)
{
  ESP_LOGI(TAG, "Downloading file %s to FLASH...", file_name);
  char full_url[256];
  snprintf(full_url, sizeof(full_url), "%s%s", EXTERNAL_FIRMWARE_UPGRADE_BASE_URL, file_name);

  esp_http_client_config_t config = {
      .url = full_url,
      .cert_pem = (char *)server_cert_pem_start,
      .keep_alive_enable = true,
      .buffer_size_tx = DEFAULT_HTTP_BUF_SIZE * 4,
      .event_handler = download_to_flash_http_event_handler};

  esp_http_client_handle_t client = esp_http_client_init(&config);

  esp_http_client_set_header(client, "ESP32-MAC", esp32_mac_address_string);
  esp_http_client_set_header(client, "Access-Code", ota_access_code);

  if (client == NULL)
  {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return;
  }

  esp_err_t err = esp_http_client_open(client, 0);

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return;
  }

  esp_http_client_fetch_headers(client);

  int status_code = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);

  if (status_code == 302)
  {
    ESP_LOGI(TAG, "302 Redirected");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    client = esp_http_client_init(&config);
    esp_http_client_set_url(client, redirect_location);
    esp_http_client_open(client, 0);
  }

  int content_length = esp_http_client_fetch_headers(client);

  ESP_LOGI(TAG, "Content length: %d bytes", content_length);

  if (content_length <= 0)
  {
    ESP_LOGE(TAG, "Failed to get content length");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }

  char full_path[256];
  snprintf(full_path, sizeof(full_path), "/flash/%s", file_name);
  remove_file_from_flash(full_path);

  FILE *file = fopen(full_path, "w");
  if (file == NULL)
  {
    ESP_LOGE(TAG, "Failed to open file %s for writing", full_path);
    return;
  }

  xEventGroupSetBits(global_event_group, IS_OTA_UPDATE_RUNNING);

  // Download and write file in chunks
  uint8_t buffer[CHUNK_SIZE];
  int total_read_len = 0;
  int read_len;

  while (total_read_len < content_length)
  {
    // Read a chunk of data from the HTTP stream
    read_len = esp_http_client_read(client, (char *)buffer, CHUNK_SIZE);

    if (read_len < 0)
    {
      ESP_LOGE(TAG, "HTTP read error: %s", esp_err_to_name(read_len));
      break;
    }
    else if (read_len == 0)
    {
      ESP_LOGI(TAG, "End of file reached");
      break;
    }

    // Write the downloaded chunk to the file
    size_t written_len = fwrite(buffer, 1, read_len, file);
    if (written_len != read_len)
    {
      ESP_LOGE(TAG, "Failed to write chunk to file");
      break;
    }

    total_read_len += read_len;
    ESP_LOGI(TAG, "Written %d/%d bytes", total_read_len, content_length);
  }

  fclose(file);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  xEventGroupClearBits(global_event_group, IS_OTA_UPDATE_RUNNING);
}

static void check_for_flash_updates()
{
  ESP_LOGI(TAG, "Checking for FLASH updates...");

  download_file_to_flash(EXTERNAL_FIRMWARE_UPGRADE_BINARY_NAME);
  download_file_to_flash(EXTERNAL_FIRMWARE_UPGRADE_KEY_NAME);
  download_file_to_flash(EXTERNAL_FIRMWARE_UPGRADE_VERSION_NAME);
}
#endif

void ota_update_task(void *pvParameter)
{
  get_running_firmware_info();
  check_current_firmware();

#ifdef CONFIG_IS_ESP32_FIRMWARE_UPGRADE_ENABLED
  ESP_LOGI(TAG, "OTA Updates enabled.");
  ESP_LOGI(TAG, "Waiting for connection to public Wi-Fi");
  xEventGroupWaitBits(global_event_group, IS_WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

  vTaskDelay(1000 * DELAY_BEFORE_UPDATE_CHECK_SECS / portTICK_PERIOD_MS);

  esp_read_mac(esp32_mac_address, ESP_MAC_EFUSE_FACTORY);
  snprintf(esp32_mac_address_string, sizeof(esp32_mac_address_string), "%02X:%02X:%02X:%02X:%02X:%02X",
           esp32_mac_address[0], esp32_mac_address[1], esp32_mac_address[2], esp32_mac_address[3], esp32_mac_address[4], esp32_mac_address[5]);

  nvs_open(NVS_OTA_STORAGE_NAMESPACE, NVS_READWRITE, &ota_storage_handle);
  size_t access_code_size = sizeof(ota_access_code);
  nvs_get_str(ota_storage_handle, NVS_OTA_ACCESS_CODE_KEY, ota_access_code, &access_code_size);
  nvs_erase_key(ota_storage_handle, NVS_OTA_ACCESS_CODE_KEY);
  nvs_close(ota_storage_handle);

  check_for_esp32_updates();
  xEventGroupClearBits(global_event_group, IS_OTA_UPDATE_RUNNING);
#ifdef CONFIG_IS_EXTERNAL_FIRMWARE_UPGRADE_ENABLED
  check_for_flash_updates();
#endif

  ESP_LOGI(TAG, "Restarting the device to switch back to AP mode");
  esp_restart();
#else
  ESP_LOGW(TAG, "OTA Updates disabled in SDK config");
#endif

  vTaskDelete(NULL);
}
