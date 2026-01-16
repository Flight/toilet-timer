#ifndef SNTP_H
#define SNTP_H

#include <stdbool.h>

void sntp_task(void *pvParameter);
bool sntp_check_first_sync_done(void);

#endif // SNTP_H
