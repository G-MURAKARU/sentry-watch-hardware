#include <Arduino.h>
#include "main.h"
#include "mqtt.h"
#include "my_wifi.h"
#include "alarm.h"
#include "rtc.h"
#include "rfid.h"

/*
	library to work with JSON data, used to send info to the backend server
*/
#include <ArduinoJson.h>

/*
	MQTT library helps with setting up the MQTT client
	and asynchronous connection handling
*/
#include <AsyncMqttClient.h>

/*
	Library for performing fixed interval operations in a
	non-blocking manner
*/
#include <Ticker.h>

/* setting default values for MQTT broker info */

/* broker's domain name/IP Address */
char broker_host[MQTT_HOST_DOMAIN_MAX_LEN] = "N/A";
/* broker's username */
char broker_username[MQTT_BROKER_USER_MAX_LEN] = "default username";
/* broker's password */
char broker_password[MQTT_BROKER_PASS_MAX_LEN] = "default password";

/* IP Address object to store the MQTT broker's IP Address, if given */
IPAddress broker_ip;

/* flag indicating whether a domain name was given for the MQTT broker instead of an IP Address */
bool domain = false;

/* MQTT instantiations */

/* asynchronous MQTT Client instance */
AsyncMqttClient mqtt_client;

/* created MQTT client's ID */
const char *mqtt_client_id = "checkpoint-A";

/* defining MQTT topics */

/* subscribe topics */

/* topic to receive the shift started/over message */
#define SHIFT_ON_OFF "sentry-platform/backend-server/shift-status"
/* topic to receive when a scan is overdue */
#define CHKS_OVERDUE "sentry-platform/circuit-handler/overdue-scan"
/* topic to receive alerts from the circuit handler */
#define RESPONSE "sentry-platform/checkpoints/A/response"
/* topic to receive alarm signal */
#define ALARM "sentry-platform/backend-server/alarm"

/* publish topics */
 
/* topic to publish scanned sentry's information */
#define SENTRY_SCAN_INFO "sentry-platform/checkpoints/sentry-scan-info"
/* topic to publish connection status */
#define CONNECTED "sentry-platform/checkpoints/connected"
/* topic to publish a scan outside the shift */
#define OUTSIDE_SHIFT_SCAN "sentry-platform/checkpoints/outside-shift-scan"

/* MQTT client reconnection timer */
Ticker mqtt_reconnection_timer;

/* MQTT publish dummy variables for dummy code */
volatile unsigned long previous_millis = 0;
const unsigned short buffer_millis = 10000;

/* JSON instantiations */

/* JSON object to store the checkpoint ID, RFID UID and time of scan */
StaticJsonDocument<128> sentry_scan_info;
/* JSON object to ferry connected info */
StaticJsonDocument<128> connected_to_mqtt;

/* prototyping functions */

/* WiFi and MQTT functions */

void on_mqtt_connect(bool);
void on_mqtt_disconnect(AsyncMqttClientDisconnectReason);
void on_mqtt_subscribe(uint16_t, uint8_t);
void on_mqtt_unsubscribe(uint16_t);
void on_mqtt_message(char *, char *, AsyncMqttClientMessageProperties, size_t, size_t, size_t);
void on_mqtt_publish(uint16_t);

/**
 * mqtt_setup_once - MQTT client configs that should only be set once, at device startup
 * 
 * Return: Nothing
*/
void mqtt_setup_once()
{
	/* setting up Async MQTT client event-handling callback functions */

	/* called handler when device connects to MQTT broker */
	mqtt_client.onConnect(on_mqtt_connect);
	/* handler for when device disconnects from MQTT broker */
	mqtt_client.onDisconnect(on_mqtt_disconnect);
	/* handler for when device subscribes to an MQTT topic */
	mqtt_client.onSubscribe(on_mqtt_subscribe);
	/* handler for when device unsubscribes from an MQTT topic */
	mqtt_client.onUnsubscribe(on_mqtt_unsubscribe);
	/* handler for when device receives a message published on any subscribed MQTT topic */
	mqtt_client.onMessage(on_mqtt_message);
	/* handler for when device publishes a message to an MQTT topic */
	mqtt_client.onPublish(on_mqtt_publish);

	/* setting a client ID, needed for final message retention */
	mqtt_client.setClientId(mqtt_client_id);
	/* setting up client keep-alive (heartbeat packet) timer */
	mqtt_client.setKeepAlive(60);

	/* setting up LWT for the client in case of unprecedented disconnection */
	connected_to_mqtt["id"] = mqtt_client_id; /* checkpoint */
	connected_to_mqtt["connected"] = 0; /* connected to MQTT */

	/* serialising JSON object to JSON string */
	String connection_info;
	serializeJson(connected_to_mqtt, connection_info);

	mqtt_client.setWill(CONNECTED, 2, false, connection_info.c_str());
}

/**
 * mqtt_setup_repeated - MQTT client setup code that should be run on every WiFi (re)connection
 * 
 * Return: Nothing
*/
void mqtt_setup_repeated()
{
	/* setting the MQTT client's credentials to connect to the server */

	/* configuring the broker credentials into the client object to connect */
	mqtt_client.setCredentials(broker_username, broker_password);

	/*
		set up with either domain name or IP Address,
		depending on which was given
	*/
	if (domain)
	{
		Serial.println("using domain name");
		mqtt_client.setServer(broker_host, 1883);
	}
	else if (!domain)
	{
		Serial.println("using IP address");
		mqtt_client.setServer(broker_ip, 1883);
	}
}

/**
 * connect_to_mqtt - connects the ESP MQTT client to the MQTT broker over WiFi
 * 
 * Return: Nothing
*/
void connect_to_mqtt()
{
	// display_connecting_to_mqtt();
	Serial.println("Connecting to MQTT broker...");
	mqtt_client.connect();
}

void mqtt_stop_reconnect()
{
    mqtt_reconnection_timer.detach();
}

/**
 * on_mqtt_connect - event handler for post MQTT connection actions
 * 
 * @session_present: id of the present session
 * 
 * Return: nothing
*/
void on_mqtt_connect(bool session_present)
{
	Serial.println("Connected to MQTT!");
	Serial.print("Session present: ");
	Serial.println(session_present);

	/* publish to the web app that the device is MQTT (and WiFi) connected */

	connected_to_mqtt["id"] = mqtt_client_id; /* checkpoint */

	connected_to_mqtt["connected"] = true; /* connected to MQTT */

	/* serialising JSON object to JSON string */

	String connection_info;
	serializeJson(connected_to_mqtt, connection_info);
	Serial.println(connection_info.c_str());

	delayMicroseconds(3000000);

	mqtt_client.publish(CONNECTED, 2, false, connection_info.c_str());

	/* subscribe to the relevant topics */

	mqtt_client.subscribe(SHIFT_ON_OFF, 2);
	mqtt_client.subscribe(RESPONSE, 2);
	mqtt_client.subscribe(ALARM, 2);
	mqtt_client.subscribe(CHKS_OVERDUE, 2);
}

/**
 * on_mqtt_disconnect - event handler for post MQTT disconnection actions
 * 
 * @reason: reason code for MQTT disconnection
 * 
 * Return: nothing
*/
void on_mqtt_disconnect(AsyncMqttClientDisconnectReason reason)
{
	Serial.println("Disconnected from MQTT.");
	Serial.printf("Reason: %d\n", reason);

	if (wifi_isConnected())
		mqtt_reconnection_timer.attach(MQTT_RECONNECT_ATTEMPT_PERIOD, connect_to_mqtt);
}

/**
 * on_mqtt_subscribe - event handler for post MQTT subscription actions
 * 
 * @packet_id: ID of subscription return packet
 * @qos: subscription's quality-of-service level
 * 
 * Return: nothing
*/
void on_mqtt_subscribe(uint16_t packet_id, uint8_t qos)
{
	Serial.println("Subscribe acknowledged.");
	Serial.print("  packet ID: ");
	Serial.println(packet_id);
	Serial.print("  qos: ");
	Serial.println(qos);
}

/**
 * on_mqtt_unsubscribe - event handler for post MQTT unsubscription actions
 * 
 * @packet_id: ID of unsubscription return packet
 * 
 * Return: nothing
*/
void on_mqtt_unsubscribe(uint16_t packet_id)
{
	Serial.print("Unsubscribe acknowledged.");
	Serial.print(" packet ID: ");
	Serial.println(packet_id);
}

/**
 * on_mqtt_message - event handler for post MQTT message reception actions
 *                   main controller for directing follow-up actions for messages received from subscriptions
 * 
 * @topic: MQTT topic on which message was posted
 * @payload: message contents
 * @properties: MQTT message properties (retain flag, QoS, etc)
 * @len: length of the MQTT payload string
 * @index: index of the incoming MQTT message
 * @total: total length of topic + payload
 * 
 * Return: Nothing
*/
void on_mqtt_message(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{ 
	Serial.println("Publish received.");
	Serial.print("  topic: ");
	Serial.println(topic);

	/* dumping the received bytearray payload into a string */
	String message;
	for (unsigned int i = 0; i < len; i++)
		message += (char)payload[i];

	Serial.println(message);

	/* checking the topic on which the incoming message was published */
	if (!strcmp(topic, SHIFT_ON_OFF))
	{
		if (message == "ON")
			shift_status = true;
		else
		{
			/* also used to notify the checkpoints that the monitoring platform has disconnected from the broker */
			/* reason could be that the server has gone down */

			shift_status = false; /* set shift to 'over' */
			silence_alarm(); /* deactivate the alarm */
		}
	}

	else if (!strcmp(topic, ALARM))
	{
		if (message == "ON")
		{
			Serial.println("alarm triggered");
			trigger_alarm();
		}
		else
		{
			alarm_reason = 0;
			Serial.println("alarm silenced");
			silence_alarm();
		}
	}

	else if (!strcmp(topic, CHKS_OVERDUE))
	{
		alarm_reason = 6;
	}

	else if (!strcmp(topic, RESPONSE))
	{
		uint8_t code = atoi(message.c_str());

        extern bool display_success;
		if (code == 1)
		/* set flag to display success message on the LCD screen */
			display_success = true;
		else
			alarm_reason = code;
	}
}

/**
 * on_mqtt_publish - event handler for post MQTT publish actions
 * 
 * @packet_id: ID of publish return packet
 * 
 * Return: nothing
*/
void on_mqtt_publish(uint16_t packet_id)
{
	Serial.print("Publish acknowledged.");
	Serial.print(" packet ID: ");
	Serial.println(packet_id);
}


bool mqtt_isConnected()
{
    return mqtt_client.connected();
}


void mqtt_send_scanned_card()
{
	/* extracting the current epoch time */
	DateTime now = get_time_now();

    /* saving the checkpoint's ID, scanned RFID UID and time of scan (epoch) into a JSON object */

	sentry_scan_info["checkpoint-id"] = CHECKPOINT_ID; /* checkpoint */
	sentry_scan_info["sentry-id"] = card_id; /* RFID UID */
	sentry_scan_info["scan-time"] = now.unixtime() + 46; /* epoch time of scan */

	/* serialising JSON object to JSON string */
	String sent_sentry_info;
	serializeJson(sentry_scan_info, sent_sentry_info);

	/* 
		publish - (required) convert JSON string to byte(char) array before publish
		String.c_str() = const char*
	*/
 
	/* if scan not during shift - PROBLEM */
	if (!shift_status)
	{
		mqtt_client.publish(OUTSIDE_SHIFT_SCAN, 2, false, sent_sentry_info.c_str());
		alarm_reason = NO_SHIFT_SCAN;
	}
	else
		mqtt_client.publish(SENTRY_SCAN_INFO, 2, false, sent_sentry_info.c_str());
}
