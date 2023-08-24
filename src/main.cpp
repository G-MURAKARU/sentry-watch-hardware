#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "main.h"

/* necessary WiFi library */
#include <WiFi.h>

/*
	WiFi Manager to help with setting WiFi credentials at runtime
	and asynchronous connection handling
*/
#include <ESPAsyncWiFiManager.h>

/* Functions to interact with RFID card reader */
#include "rfid.h"

/* Functions to interact with the real-time clock */
#include "rtc.h"

/*
	library to work with JSON data, used to send info to the backend server
*/
#include <ArduinoJson.h>

/*
	MQTT library helps with setting up the MQTT client
	and asynchronous connection handling
*/
#include <AsyncMqttClient.h>

/* Functions to interact with the LCD Screen */
#include "lcd.h"

/*
	FreeRTOS software timers used because
	there is no built-in Ticker library
*/
extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}

/* Functions to interact with the alarm LED and buzzer */
#include "alarm.h"

#define WIFI_CONFIG_PIN 0

/* object instantiation for WiFi Manager setup */

AsyncWebServer server(80);
DNSServer dns;
AsyncWiFiManager wifi_manager(&server, &dns);

/* custom WiFi Manager parameters to capture MQTT broker info */

/* message instructing user to key in either MQTT broker domain name or MQTT broker IP Address*/
AsyncWiFiManagerParameter domain_or_ip("<p>Enter either the broker's IP Address or domain name(URL).</p><p>Leave the other box blank.</p>");
/* broker domain name form field */
AsyncWiFiManagerParameter mqtt_host_domain("broker-host-domain", "MQTT Domain", NULL, 30);
/* broker IP address form field */
AsyncWiFiManagerParameter mqtt_host_ip("broker-host-ip", "MQTT IP Address", NULL, 15);
/* broker username form field */
AsyncWiFiManagerParameter mqtt_user("broker-user", "MQTT Username", NULL, 20);
/* broker password form field */
AsyncWiFiManagerParameter mqtt_pass("broker-pass", "MQTT Password", NULL, 20);


/* setting default values for MQTT broker info */

/* broker's domain name/IP Address */
char broker_host[30] = "N/A";
/* broker's username */
char broker_username[20] = "default username";
/* broker's password */
char broker_password[20] = "default password";

/* IP Address object to store the MQTT broker's IP Address, if given */
IPAddress broker_ip;

/* initialising variables to handle WiFi disconnection */

/* WiFi reconnection timeout duration [ms] */
const unsigned int reconnect_timeout = 120000;
/* if device cannot reconnect to WiFi after 2 minutes, it resets */

/* epoch time at disconnect, used to measure elapsed time since disconnect */
volatile unsigned long reconnect_millis = 0;

/* device's WiFi reconnection state flag */
volatile bool reconnecting = false;

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
TimerHandle_t mqtt_reconnection_timer;

/* MQTT publish dummy variables for dummy code */
volatile unsigned long previous_millis = 0;
const unsigned short buffer_millis = 10000;

/* 
	connected to WiFi
	avoid conflict with WiFi GotIP callback
*/
volatile bool connected = false;

/* launch WiFi config interrupt flag */
volatile bool config = false;

/* shift/circuit status flags */

/* shift ongoing/over */
volatile bool shift_status = false;

/* JSON instantiations */

/* JSON object to store the checkpoint ID, RFID UID and time of scan */
StaticJsonDocument<128> sentry_scan_info;
/* JSON object to ferry connected info */
StaticJsonDocument<128> connected_to_mqtt;

/* prototyping functions */

/* WiFi and MQTT functions */

void set_broker_credentials(AsyncWiFiManagerParameter, AsyncWiFiManagerParameter, AsyncWiFiManagerParameter, AsyncWiFiManagerParameter);
void connect_to_wifi();
void config_mode_callback(AsyncWiFiManager *);
void setup_wifi_manager();
void connect_to_mqtt();
void wifi_event(WiFiEvent_t);
void on_mqtt_connect(bool);
void on_mqtt_disconnect(AsyncMqttClientDisconnectReason);
void on_mqtt_subscribe(uint16_t, uint8_t);
void on_mqtt_unsubscribe(uint16_t);
void on_mqtt_message(char *, char *, AsyncMqttClientMessageProperties, size_t, size_t, size_t);
void on_mqtt_publish(uint16_t);
void mqtt_setup_once();
void mqtt_setup_repeated();
void launch_wifi_config();

volatile uint8_t alarm_reason;

/* Flag to display success message on the LCD screen */
volatile bool display_success = false;

/**
 * config_mode_callback - callback handler function,
 *                        called before the device enters configuration mode
 * 
 * @my_wifi_manager: active Async WiFi Manager instance (struct)
 * 
 * Return: Nothing
*/
void config_mode_callback(AsyncWiFiManager *my_wifi_manager)
{
	display_AP_mode();
	Serial.println("Entered config mode:");
	Serial.println(WiFi.softAPIP());
	Serial.println(my_wifi_manager->getConfigPortalSSID());
}

/**
 * setup_wifi - sets up WiFi Manager and Config Portal, also allows input of MQTT broker's info
 * 
 * Return: Nothing
*/
void setup_wifi_manager()
{
	/* setting up callback function that runs before the ESP goes into WiFi config mode */
	wifi_manager.setAPCallback(config_mode_callback);

	/* setting up a timeout duration for the ESP to remain in AP (config) mode before restart */
	wifi_manager.setConfigPortalTimeout(120);

	/* setting up a timeout duration for the ESP to connect to previous WiFi before going into AP mode */
	wifi_manager.setConnectTimeout(20);

	/*
		adding WiFi custom parameters to the portal
	*/

	/* domain/IP guiding text */
	wifi_manager.addParameter(&domain_or_ip);
	/* add MQTT broker domain text field */
	wifi_manager.addParameter(&mqtt_host_domain);
	/* add MQTT broker IP text field */
	wifi_manager.addParameter(&mqtt_host_ip);
	/* add MQTT broker username text field */
	wifi_manager.addParameter(&mqtt_user);
	/* add MQTT broker password text field */
	wifi_manager.addParameter(&mqtt_pass);

	/* some MQTT setup code, should run just once */
	mqtt_setup_once();
}

/**
 * connect_to_wifi - connects to the WiFi, called on demand
 *                   starts an access point with name "Checkpoint A"
 *                   goes into a blocking loop awaiting configuration,
 *                   resets all WiFi Manager configs and credentials
 * 
 * Return: Nothing
*/
void connect_to_wifi()
{
	wifi_manager.resetSettings();

	domain = false;
	connected = false;

	Serial.println("Connecting to WiFi...");

	/* 'Checkpoint A' is the displayed name of the ESP access point */
	if (!wifi_manager.startConfigPortal("Checkpoint A"))
	{
		/*
			this code block is run when the config portal timeout is exhausted
		*/

		Serial.println("Failed to connect and hit timeout");
		delay(3000);
		ESP.restart();
		delay(5000);
	}

	/* save broker credentials */
	set_broker_credentials(mqtt_host_domain, mqtt_host_ip, mqtt_user, mqtt_pass);
}

/**
 * set_broker_credentials - saves the received broker credentials into defined variables
 * 
 * @host: MQTT broker's IP Address/domain name
 * @username: MQTT broker's username
 * @password: MQTT broker's password
 * 
 * Return: Nothing
*/
void set_broker_credentials(AsyncWiFiManagerParameter host_domain, AsyncWiFiManagerParameter host_ip, AsyncWiFiManagerParameter username, AsyncWiFiManagerParameter password)
{
	/* save broker's username */
	strncpy(broker_username, username.getValue(), username.getValueLength());
	/* save broker's password */
	strncpy(broker_password, password.getValue(), password.getValueLength());

	/*
		test for broker identity - domain name or IP address
	*/

	/* returns true if a valid broker IP address is given, saves broker's IP */
	bool input_ip = broker_ip.fromString(host_ip.getValue());

	/* if either an invalid broker IP or no broker IP was given */
	if (!input_ip || (broker_ip.toString() == "0.0.0.0"))
	{
		/*
			checks if a domain name was given, no validity checks as long as it's a string
		*/

		/* if no domain name was keyed in */
		if (String(host_domain.getValue()) == "")
		{
			Serial.println("Please enter a valid domain/IP, push reset button.");
			display_mqtt_retry();
			ESP.restart();
		}

		/* if a domain name (any string) was keyed in */
		else
		{
			/* save broker's domain name */
			strncpy(broker_host, host_domain.getValue(), host_domain.getValueLength());

			/* indicate that a domain name was given and not an IP Address */
			domain = true;
		}
	}

	Serial.println(broker_host);
	Serial.println(broker_username);
	Serial.println(broker_password);
	Serial.println(broker_ip);

	/* some MQTT setup code, should be run with every WiFi connection */
	mqtt_setup_repeated();

	/* connect to MQTT */
	connected = true;
	connect_to_mqtt();
}

/**
 * wifi_event - WiFi event-driven handler function
 *              executes logic depending on detected WiFi event
 * 
 * @event: WiFi event (macro)
 * 
 * Return: Nothing
*/
void wifi_event(WiFiEvent_t event)
{
	Serial.printf("[WiFi Event] event: %d\n", event);
	switch(event)
	{
		case ARDUINO_EVENT_WIFI_STA_CONNECTED:
			Serial.println("Connected to WiFi!");
			silence_alarm();

			/* reset the reconnection flag if set */
			if (reconnecting)
				reconnecting = false;

			break;

		case ARDUINO_EVENT_WIFI_STA_GOT_IP:
		/* print IP address */
			Serial.print("IP Address: ");
			Serial.println(WiFi.localIP());

			if (connected)
				connect_to_mqtt();
			break;

		case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
			Serial.println("WiFi connection lost. Reconnecting..");
			display_connecting_to_wifi();

			/* ensure not to attempt MQTT reconnection while WiFi disconnected */
			xTimerStop(mqtt_reconnection_timer, 0);

			/*
				set the reconnection flag if reset,
				capture epoch time at disconnect
			*/
			if (!reconnecting)
			{
				reconnecting = true;
				reconnect_millis = millis();
			}

			/*
				if reconnecting,
				measure time elapsed since disconnect,
				compare with set connection timeout duration
				if exceeded, restart the device
			*/
			else if (reconnecting)
			{
				if ((millis() - reconnect_millis) >= reconnect_timeout)
				{
					delay(3000);
					ESP.restart();
					delay(5000);
				}
			}

			break;
	}
}

/**
 * mqtt_setup_once - MQTT client configs that should only be set once, at device startup
 * 
 * Return: Nothing
*/
void mqtt_setup_once()
{
	/* defining the declared MQTT reconnection timer */
	mqtt_reconnection_timer = xTimerCreate("mqtt_timer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connect_to_mqtt));

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

	if (WiFi.isConnected())
		xTimerStart(mqtt_reconnection_timer, 0);
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

/**
 * launch_wifi_config - sets flag that indicates that device should go into
 *                      on-demand WiFi config mode, triggered by ISR
 * 
 * Return: Nothing
*/
void IRAM_ATTR launch_wifi_config()
{
	config = true;
}

void setup() {
	/* setting up the ESP32 in station mode (WiFi client) */
	WiFi.mode(WIFI_STA);

	Serial.begin(115200);

	/* initialise SPI, I2C, RFID, RTC and LCD comms */

	SPI.begin();
	initialize_rfid();

	Wire.begin();	/* For the LCD display and RTC_DS3231 */
	initialize_display();
	initialize_RTC();


	/* setting up alarmLED and buzzer pins */
	initialize_alarm();

	/* setting up input pin to listen for on-demand trigger (button) */
	pinMode(WIFI_CONFIG_PIN, INPUT_PULLUP);

	/* configure callback function to handle WiFi events */
	WiFi.onEvent(wifi_event);

	/* set up WiFi Manager configs, callbacks, parameters */
	setup_wifi_manager();

	/* set up a hardware interrupt to trigger on-demand WiFi config portal */
	attachInterrupt(digitalPinToInterrupt(WIFI_CONFIG_PIN), launch_wifi_config, FALLING);
}

void loop()
{
	/* if WiFi config mode button pressed */
	if (config)
	{
		connect_to_wifi();

		/* reset interrupt flag */
		config = false;
	}
	
	if (alarm_on_off)
	{
		if (alarm_reason == OVERDUE_SCAN)
			display_scan_time_elapsed();
		else
			display_invalid_scan(alarm_reason);
		return;
	}

	if (display_success)
	{
		display_valid_scan();
		display_success = false;
	}

	/* 
		conditions with return statements:
		running code below them is pointless hence early return, restart loop
	*/

	/* if both WiFi and MQTT connected, display check on both */
	if (WiFi.isConnected() && mqtt_client.connected())
		display_default_text(DISPLAY_SUCCESS, DISPLAY_SUCCESS);

	/* if only WiFi connected: display check on WiFi, X on MQTT */
	else if (WiFi.isConnected())
	{
		display_default_text(DISPLAY_SUCCESS, DISPLAY_FAILURE); return;
	}

	/* if neither connected: display X on both */
	else
	{
		display_default_text(DISPLAY_FAILURE, DISPLAY_FAILURE); return;
	}

	/* Scanning any 'new' RFID card in the vicinity */
	if (!rfid_read_new_card())
		return;

	/* extracting the current epoch time */
	DateTime now = get_time_now();

	display_scanning_verifying();

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
