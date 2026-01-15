#include <nvs.h>
#include <freertos/event_groups.h>

#ifndef GLOBAL_EVENT_GROUP_H
#define GLOBAL_EVENT_GROUP_H

#include "global_constants.h"


extern EventGroupHandle_t global_event_group;
extern char global_log_buffer[LOG_BUFFER_SIZE];

extern char public_wifi_ssid[128];
extern char public_wifi_password[128];

extern int global_battery_level;

extern nvs_handle_t ota_storage_handle;
extern char *NVS_OTA_STORAGE_NAMESPACE;
extern char *NVS_OTA_FIRMWARE_HASH_KEY;

#define TRIGGER_WIFI_CONNECT BIT1
#define IS_WIFI_CONNECTED_BIT BIT2
#define IS_WIFI_FAILED_BIT BIT3
#define IS_OTA_UPDATE_RUNNING BIT4

#endif // GLOBAL_EVENT_GROUP_H