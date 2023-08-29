#ifndef __INC_MY_WIFI_H
#define __INC_MY_WIFI_H

#include <Arduino.h>

/* WiFi connection functions */
void initialize_wifi(void);
bool wifi_isConnected(void);
void check_wifi_config_requested(void);

#endif		/* ifndef __INC_MY_WIFI_H */
