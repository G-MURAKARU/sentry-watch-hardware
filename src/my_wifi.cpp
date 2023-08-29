#include <Arduino.h>

#include "main.h"
#include "mqtt.h"
#include "lcd.h"
#include "alarm.h"

/* necessary WiFi library */
#include <WiFi.h>

/*
 *  WiFi Manager to help with setting WiFi credentials at runtime
 *   and asynchronous connection handling
*/
#include <ESPAsyncWiFiManager.h>

/* object instantiation for WiFi Manager setup */

static AsyncWebServer server(80);
static DNSServer dns;
static AsyncWiFiManager wifi_manager(&server, &dns);

/* custom WiFi Manager parameters to capture MQTT broker info */

/* message instructing user to key in either MQTT broker domain name
 *  or MQTT broker IP Address
 */
static AsyncWiFiManagerParameter domain_or_ip(
	"<p>Enter either the broker's IP Address or domain name(URL).</p>"
	"<p>Leave the other box blank.</p>");
/* broker domain name form field */
static AsyncWiFiManagerParameter mqtt_host_domain(
	"broker-host-domain", "MQTT Domain", NULL, MQTT_HOST_DOMAIN_MAX_LEN);
/* broker IP address form field */
static AsyncWiFiManagerParameter mqtt_host_ip(
	"broker-host-ip", "MQTT IP Address", NULL, MQTT_HOST_IP_MAX_LEN);
/* broker username form field */
static AsyncWiFiManagerParameter mqtt_user(
	"broker-user", "MQTT Username", NULL, MQTT_BROKER_USER_MAX_LEN);
/* broker password form field */
static AsyncWiFiManagerParameter mqtt_pass(
	"broker-pass", "MQTT Password", NULL, MQTT_BROKER_PASS_MAX_LEN);

/* initialising variables to handle WiFi disconnection */

/* WiFi reconnection timeout duration [ms] */
const unsigned int reconnect_timeout = 120000;
/* if device cannot reconnect to WiFi after 2 minutes, it resets */

/* epoch time at disconnect, used to measure elapsed time since disconnect */
volatile unsigned long reconnect_millis = 0;

/* device's WiFi reconnection state flag */
volatile bool reconnecting = false;

/*
	configured WiFi
	avoid conflict with WiFi GotIP callback
*/
volatile bool configured = false;

/* launch WiFi config interrupt flag */
volatile bool config = false;

/**
 * set_broker_credentials - saves the received broker credentials into
 *  defined global variables
 *
 * Return: Nothing
*/
static void set_broker_credentials()
{
	/* save broker's username */
	strncpy(broker_username, mqtt_user.getValue(), mqtt_user.getValueLength());
	/* save broker's password */
	strncpy(broker_password, mqtt_pass.getValue(), mqtt_pass.getValueLength());

	/*
		test for broker identity - domain name or IP address
	*/

	/* returns true if a valid broker IP address is given, saves broker's IP */
	bool input_ip = broker_ip.fromString(mqtt_host_ip.getValue());

	/* if either an invalid broker IP or no broker IP was given */
	if (!input_ip || (broker_ip.toString() == "0.0.0.0"))
	{
		/*
			checks if a domain name was given, no validity checks as long as it's a string
		*/

		/* if no domain name was keyed in */
		if (String(mqtt_host_domain.getValue()) == "")
		{
			Serial.println("Please enter a valid domain/IP, push reset button.");
			display_mqtt_retry();
			ESP.restart();
		}

		/* if a domain name (any string) was keyed in */
		else
		{
			/* save broker's domain name */
			strncpy(broker_host, mqtt_host_domain.getValue(), mqtt_host_domain.getValueLength());

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
	configured = true;
	connect_to_mqtt();
}

/**
 * configure_wifi - configures the WiFi, called on demand
 *                   starts an access point with name "Checkpoint A"
 *                   goes into a blocking loop awaiting configuration,
 *                   resets all WiFi Manager configs and credentials
 *
 * Return: Nothing
*/
static void configure_wifi()
{
	wifi_manager.resetSettings();

	domain = false;
	configured = false;

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
	set_broker_credentials();
}

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
 * setup_wifi_manager - sets up WiFi Manager and Config Portal, also allows
 *  input of MQTT broker's info
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
}

/**
 * wifi_event - WiFi event-driven handler function
 *              executes logic depending on detected WiFi event
 *
 * @event: WiFi event (macro)
 *
 * Return: Nothing
*/
static void wifi_event(WiFiEvent_t event)
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

			if (configured)
				connect_to_mqtt();
			break;

		case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
			Serial.println("WiFi connection lost. Reconnecting..");
			display_connecting_to_wifi();

			/* ensure not to attempt MQTT reconnection while WiFi disconnected */
			mqtt_stop_reconnect();

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
		default:
			Serial.printf("Wifi Event occurred: %d\n", (int)event);
	}
}

/**
 * launch_wifi_config - sets flag that indicates that device should go into
 *  on-demand WiFi config mode, triggered by ISR
 *
 * Return: Nothing
*/
void IRAM_ATTR launch_wifi_config()
{
	config = true;
}

/**
 * check_wifi_config_requested - checks the global config variable to
 *  know if wifi config is requested and enters config mode if requested
 *
 * Return: Nothing
*/
void check_wifi_config_requested()
{
    if (config)
	{
		configure_wifi();

		/* reset interrupt flag */
		config = false;
	}
}

/**
 * initialize_wifi - initializes the WiFi and WiFiManager for configuring
 *  required system properties
 *
 * Return: Nothing
*/
void initialize_wifi()
{
	/* setting up the ESP32 in station mode (WiFi client) */
	WiFi.mode(WIFI_STA);

	/* setting up input pin to listen for on-demand trigger (button) */
	pinMode(WIFI_CONFIG_PIN, INPUT_PULLUP);

	/* set up a hardware interrupt to trigger on-demand WiFi config portal */
	attachInterrupt(digitalPinToInterrupt(WIFI_CONFIG_PIN),
        launch_wifi_config, FALLING);

	/* configure callback function to handle WiFi events */
	WiFi.onEvent(wifi_event);

	/* set up WiFi Manager configs, callbacks, parameters */
	setup_wifi_manager();
}

/**
 * wifi_isConnected - checks if WiFi is connected
 *
 * Return: true if connected, false otherwise
*/
bool wifi_isConnected()
{
    return WiFi.isConnected();
}