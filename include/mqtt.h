#ifndef __INC_MQTT_HEADER_H
#define __INC_MQTT_HEADER_H

#include <Arduino.h>

#define MQTT_HOST_DOMAIN_MAX_LEN        30
#define MQTT_HOST_IP_MAX_LEN            15
#define MQTT_BROKER_USER_MAX_LEN        20
#define MQTT_BROKER_PASS_MAX_LEN        20

#define MQTT_RECONNECT_ATTEMPT_PERIOD   2.0f

/* broker's domain name/IP Address */
extern char broker_host[];
/* broker's username */
extern char broker_username[];
/* broker's password */
extern char broker_password[];
/* IP Address object to store the MQTT broker's IP Address, if given */
extern IPAddress broker_ip;
/* flag indicating whether a domain name was given for the MQTT broker instead of an IP Address */
extern bool domain;


void mqtt_setup_once();
void mqtt_setup_repeated();
void mqtt_stop_reconnect();
void connect_to_mqtt();
bool mqtt_isConnected();
void mqtt_send_scanned_card();

#endif		/* ifndef __INC_MQTT_HEADER_H */
