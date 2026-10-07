#ifndef HOST_SERVICE_WIFI_H
#define HOST_SERVICE_WIFI_H
#include <stdbool.h>
bool service_wifi_direct_busy(void);
void service_wifi_direct_cancel(void);
void service_wifi_deinit(void);
#endif
