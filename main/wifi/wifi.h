#ifndef WIFI_H
#define WIFI_H

void wifi_task(void *pvParameter);
void wifi_stop(void);
void wifi_disconnect_task(void *pvParameter);

#endif // WIFI_H