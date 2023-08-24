#include <Arduino.h>
#include <Wire.h>
#include "main.h"
#include "lcd.h"

/*
	library to interact with the LCD Screen via I2C,
	depends on Wire.h, included
*/
#include <LiquidCrystal_I2C.h>


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


/**
 * display_connected - displays WiFi and MQTT check marks when both are connected
 *                     on display's top row
 *
 * Return: Nothing
*/
static void display_connected(display_status_t symbol_wifi, display_status_t symbol_mqtt)
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
 * display_default_text - displays "Scan Card" on the bottom row of the display
 *
 * Return: Nothing
*/
void display_default_text(display_status_t wifi_symbol, display_status_t mqtt_symbol)
{
	display_connected(wifi_symbol, mqtt_symbol);

	lcd.setCursor(0, 1);
	lcd.print("   Scan Card    ");
}

/**
 * display_scanning_verifying - displays "Scanning and verifying sentry ID" when the card is scanned
 *                              and sent to the sentry platform, and awaiting a verification response
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
 * display_connected_to_wifi - displays "Connecting to WiFi" on the display
 *                             called in WiFi disconnect event handler
 *
 * Return: Nothing
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
 *                             called in connect_to_mqtt
 *
 * Return: Nothing
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
 * display_AP_mode - displays message informing that the device is in AP/WiFi config mode
 *                   called in config_mode_callback
 *
 * Return: Nothing
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
 *                      on response from the sentry monitoring platform
 *
 * Return: Nothing
*/
void display_valid_scan()
{
	String valid = "Valid scan! Continue to next checkpoint..";
	scroll_text(1, valid, 375, 16);
}

/**
 * display_valid_scan - displays an invalid scan message on the display,
 *                      on response from the sentry monitoring platform
 *                      displays with the reason: unknown ID, wrong sentry, wrong time
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
 * display_scan_time_elapsed - displays message indicating that an expected sentry
 *                             did not scan within their given check-in window
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
 * scroll_text - displays a scrolling message on the display,
 *               if the message's length exceeds 16 characters
 *
 * @row: display row on which to display message: 0 or 1
 * @message: message to display
 * @delay_time: time delay before scrolling to the next letter
 * @lcd_columns: number of display's columns = 16
 *
 * Return: Nothing
*/
void scroll_text(int row, String message, int delay_time, int lcd_columns)
{
	for (int i = 0; i < 3; i++)
	{
		message = " " + message;
	}
	message += " ";

	for (int pos = 0; pos < message.length() - 13; pos++)
	{
		lcd.setCursor(0, row);
		lcd.print(message.substring(pos, pos + lcd_columns));
		delay(delay_time);
	}
}

/**
 * initialize_display - sets up the lcd module and the I2C bus
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
