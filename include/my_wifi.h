#ifndef __INC_MY_WIFI_H
#define __INC_MY_WIFI_H

#include <Arduino.h>

/* WiFi connection functions */
void initialize_wifi();
bool wifi_isConnected();
void check_wifi_config_requested();

#endif		/* ifndef __INC_MY_WIFI_H */
