#include <Arduino.h>
#include <Wire.h>
#include "main.h"
#include "lcd.h"

/*
	library to interact with the LCD Screen via I2C,
	depends on Wire.h, included
*/
#include <LiquidCrystal_I2C.h>

/*
	Library for performing fixed interval operations in a
	non-blocking manner
*/
#include <Ticker.h>


/* active I2C LCD instance */
static LiquidCrystal_I2C lcd(0x27, 16, 2);

/* custom check-mark symbol to display */
static const byte check[8] = {
	0b00000,
	0b00000,
	0b00001,
	0b00010,
	0b10100,
	0b01000,
	0b00000,
	0b00000
};

/* custom X symbol to display */
static const byte x_mark[8] = {
	0b00000,
	0b00000,
	0b10001,
	0b01010,
	0b00100,
	0b01010,
	0b10001,
	0b00000
};

/* LCD scrolling ticker */
static Ticker lcd_scroll_ticker;

/* Flag to scroll message on the LCD screen */
static volatile bool scroll_screen = false;

/* Variables required for the scroll */
static String scroll_message;
static int scroll_pos;
static int scroll_display_columns;
static int scroll_row;

/**
 * scroll_callback - scrolls the setup scroll_message following the
 *  scroll_ticker only if scroll is activated
 *
 * Return: Nothing
*/
static void scroll_callback()
{
	if (scroll_screen)
	{
		if (scroll_pos < scroll_message.length() - 13)
		{
			lcd.setCursor(0, scroll_row);
			lcd.print(scroll_message.substring(
				scroll_pos, scroll_pos + scroll_display_columns));
			scroll_pos++;
			return;
		}
		scroll_screen = false;
	}
	lcd_scroll_ticker.detach();
}

/**
 * scroll_text - displays a scrolling message on the display,
 *  if the message's length exceeds 16 characters
 *
 * @row: display row on which to display message: 0 or 1
 * @message: message to display
 * @delay_time: time delay before scrolling to the next letter
 * @lcd_columns: number of display's columns = 16
 *
 * Return: Nothing
*/
static void scroll_text(
		int row, String message, int delay_time, int lcd_columns)
{
	/* Disable scrolling before editing message */
	scroll_screen = false;

	/* Adding some padding for the scroll message */
	scroll_message = "   " + message + " ";
	scroll_row = row;
	scroll_display_columns = lcd_columns;
	scroll_pos = 1;

	lcd.setCursor(0, row);
	lcd.print(scroll_message.substring(0, lcd_columns));
	lcd_scroll_ticker.attach((float)delay_time/1000, scroll_callback);
}


/**
 * display_connected - displays WiFi and MQTT check marks when
 *  both are connected on display's top row
 *
 * @symbol_wifi: the symbol to show the WiFi connection
 * @symbol_mqtt: the symbol to show the MQTT connection
 *
 * Return: Nothing
*/
static void display_connected(
		display_status_t symbol_wifi, display_status_t symbol_mqtt)
{
	lcd.setCursor(0, 0);
	lcd.print("WiFi: ");
	lcd.setCursor(6, 0);
	lcd.write((uint8_t)symbol_wifi);
	lcd.setCursor(7, 0);
	lcd.print(" MQTT: ");
	lcd.setCursor(14, 0);
	lcd.write((uint8_t)symbol_mqtt);
	lcd.setCursor(15, 0);
	lcd.print(" ");
}

/**
 * display_default_text - displays "Scan Card" on the bottom
 *  row of the display
 *
 * @symbol_wifi: the symbol to show the WiFi connection
 * @symbol_mqtt: the symbol to show the MQTT connection
 *
 * Return: Nothing
*/
void display_default_text(
	display_status_t wifi_symbol, display_status_t mqtt_symbol)
{
	display_connected(wifi_symbol, mqtt_symbol);

	lcd.setCursor(0, 1);
	lcd.print("   Scan Card    ");
}

/**
 * display_scanning_verifying - displays "Scanning and verifying sentry ID"
 *  when the card is scanned and sent to the sentry platform, and is awaiting
 *  a verification response
 *
 * Return: Nothing
*/
void display_scanning_verifying()
{
	display_connected(DISPLAY_SUCCESS, DISPLAY_SUCCESS);

	String valid = "Scanning and verifying sentry ID..";
	scroll_text(1, valid, 375, 16);
}

/**
 * display_connecting_to_wifi - displays "Connecting to WiFi" on the display
 *
 * Return: Nothing
 *
 * Note: called in WiFi disconnect event handler
*/
void display_connecting_to_wifi()
{
	lcd.setCursor(0, 0);
	lcd.print(" Connecting to  ");
	lcd.setCursor(0, 1);
	lcd.print("    WiFi....    ");
}

/**
 * display_connecting_to_mqtt - displays "Connecting to MQTT" on the display
 *
 * Return: Nothing
 *
 * Note: called in connect_to_mqtt
*/
void display_mqtt_retry()
{
	lcd.setCursor(0, 0);
	lcd.print("Enter valid MQTT");
	lcd.setCursor(0, 1);
	lcd.print(" Domain/IP Addr ");
	delay(3000);
	lcd.setCursor(0, 0);
	lcd.print("   Restarting   ");
	lcd.setCursor(0, 1);
	lcd.print("     Device     ");
	delay(3000);
}

/**
 * display_AP_mode - displays message informing that the device is in
 *  AP/WiFi config mode
 *
 * Return: Nothing
 *
 * Note: called in config_mode_callback
*/
void display_AP_mode()
{
	lcd.setCursor(0, 0);
	lcd.print(" ! A.P. Mode !  ");
	lcd.setCursor(0, 1);
	lcd.print("Set WiFi & MQTT ");
}

/**
 * display_valid_scan - displays a valid scan message on the display,
 *  on response from the sentry monitoring platform
 *
 * Return: Nothing
*/
void display_valid_scan()
{
	String valid = "Valid scan! Continue to next checkpoint..";
	scroll_text(1, valid, 375, 16);
}

/**
 * display_valid_scan - displays an invalid scan message with the
 *  reason (unknown ID, wrong sentry, wrong time) on the display,
 *  on response from the sentry monitoring platform
 *
 * @reason: flag corresponding to reason for invalid scan
 *          1: unknown ID
 *          2: wrong/unexpected sentry
 *          3: wrong time of scan
 *
 * Return: Nothing
*/
void display_invalid_scan(uint8_t reason)
{
	lcd.setCursor(0, 0);
	lcd.print(" INVALID SCAN!  ");

	String unexpected = "WRONG CHECKPOINT!";
	String wrong_time = "WRONG TIME OF SCAN!";

	switch(reason)
	{
		case UNKNOWN_CARD:
			lcd.setCursor(0, 1);
			lcd.print("  UNKNOWN ID!   ");
			break;
		case STOLEN_CARD:
			lcd.setCursor(0, 1);
			lcd.print("  STOLEN CARD!  ");
			break;
		case WRONG_CHECKPOINT:
			scroll_text(1, unexpected, 250, 16);
			break;
		case WRONG_TIME:
			scroll_text(1, wrong_time, 250, 16);
			break;
		case NO_SHIFT_SCAN:
			lcd.setCursor(0, 1);
			lcd.print("NO ONGOING SHIFT");
	}
}

/**
 * display_scan_time_elapsed - displays message indicating that an expected
 *  sentry did not scan within their given check-in window
 *
 * Return: Nothing
*/
void display_scan_time_elapsed()
{
	lcd.setCursor(0, 0);
	lcd.print("SENTRY VERIFYING");
	lcd.setCursor(0, 1);
	lcd.print(" WINDOW PASSED! ");
}

/**
 * initialize_display - sets up the lcd module on the I2C bus
 *
 * Return: Nothing
 *
 * Note: Wire.begin() should be called prior to this
*/
void initialize_display()
{
	lcd.init();
	lcd.backlight();

	/* saving the custom checkmark to the LCD's memory */
	lcd.createChar((uint8_t)DISPLAY_SUCCESS, (byte *)check);
	lcd.createChar((uint8_t)DISPLAY_FAILURE, (byte *)x_mark);
}
