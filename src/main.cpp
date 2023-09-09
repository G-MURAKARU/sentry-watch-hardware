#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "main.h"

/* Functions for handling the WiFi connection */
#include "my_wifi.h"

/* Functions for handling the MQTT connection and data */
#include "mqtt.h"

/* Functions to interact with RFID card reader */
#include "rfid.h"

/* Functions to interact with the real-time clock */
#include "rtc.h"

/* Functions to interact with the LCD Screen */
#include "lcd.h"

/* Functions to interact with the alarm LED and buzzer */
#include "alarm.h"

/* checkpoint ID to send with a sentry scan */
uint32_t CHECKPOINT_ID = 0;


/* shift/circuit status flags */

/* shift ongoing/over */
volatile bool shift_status = false;

volatile uint8_t alarm_reason;

/**
 * setup - Single running code before loop
 *
 * Return: nothing
*/
void setup() {
	Serial.begin(115200);

	/* initialise SPI, I2C, RFID, RTC and LCD comms */

	SPI.begin();
	initialize_rfid();

	Wire.begin();	/* For the LCD display and RTC_DS3231 */
	initialize_display();
	initialize_RTC();


	/* setting up alarmLED and buzzer pins */
	initialize_alarm();

	/* Setting Up wifi connection */
	initialize_wifi();

	/* some MQTT setup code, should run just once */
	mqtt_setup_once();
}

/**
 * loop - Repeatedly running code of the program
 *
 * Return: nothing
*/
void loop()
{
	/* if WiFi config mode button pressed */
	check_wifi_config_requested();

	if (alarm_on_off)
	{
		if (alarm_reason == OVERDUE_SCAN)
			display_scan_time_elapsed();
		else
			display_invalid_scan(alarm_reason);
		return;
	}

	/*
		conditions with return statements:
		running code below them is pointless hence early return, restart loop
	*/

	/* if both WiFi and MQTT connected, display check on both */
	if (wifi_isConnected() && mqtt_isConnected())
		display_default_text(DISPLAY_SUCCESS, DISPLAY_SUCCESS);

	/* if only WiFi connected: display check on WiFi, X on MQTT */
	else if (wifi_isConnected())
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

	display_scanning_verifying();

	mqtt_send_scanned_card();
}
