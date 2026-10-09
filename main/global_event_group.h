/**
 * @file global_event_group.h
 * @brief Global event group for inter-task synchronization
 */

#ifndef GLOBAL_EVENT_GROUP_H
#define GLOBAL_EVENT_GROUP_H

#include <freertos/event_groups.h>

/* Global event group handle */
extern EventGroupHandle_t global_event_group;

/* Event bits */
#define IS_WIFI_CONNECTED_BIT    BIT2
#define IS_WIFI_FAILED_BIT       BIT3
#define IS_OTA_UPDATE_RUNNING    BIT4
#define IS_OTA_CHECK_DONE        BIT5
#define IS_SNTP_SYNC_DONE        BIT6
#define IS_SNTP_FIRST_SYNC_DONE  BIT7  /* Time is known: synced at least once, or restored from the RTC chip */
#define IS_RESET_BUTTON_WAKEUP   BIT8  /* Woke up by a short key press: litter change recorded */
#define IS_WIFI_AVAILABLE        BIT9
#define IS_SYNC_REQUESTED        BIT10 /* Woke up by a long key press: Wi-Fi sync and OTA check */
#define IS_RTC_TIME_VALID        BIT11 /* System time was restored from the PCF85063 RTC at boot */
#define IS_TIME_CORRECTED        BIT12 /* SNTP moved the clock by more than a few minutes */

#endif /* GLOBAL_EVENT_GROUP_H */
