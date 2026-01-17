/**
 * @file global_event_group.h
 * @brief Global event group for inter-task synchronization
 */

#ifndef GLOBAL_EVENT_GROUP_H
#define GLOBAL_EVENT_GROUP_H

#include <freertos/event_groups.h>

/* Global event group handle */
extern EventGroupHandle_t global_event_group;

/* Global battery level (0-100%) */
extern int global_battery_level;

/* Event bits */
#define IS_WIFI_CONNECTED_BIT   BIT2
#define IS_WIFI_FAILED_BIT      BIT3
#define IS_OTA_UPDATE_RUNNING   BIT4
#define IS_OTA_CHECK_DONE       BIT5
#define IS_SNTP_SYNC_DONE       BIT6
#define IS_SNTP_FIRST_SYNC_DONE BIT7
#define IS_GPIO4_WAKEUP         BIT8

#endif /* GLOBAL_EVENT_GROUP_H */
