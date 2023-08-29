#include <Arduino.h>
#include "main.h"
#include "alarm.h"

/*
	Library for performing fixed interval operations in a
	non-blocking manner
*/
#include <Ticker.h>


/* Ticker object for flashing the onboard LED */
static Ticker alarm_ticker;

/* Global indicator for whether alarm is triggered/silenced */
volatile bool alarm_on_off = false;


/**
 * toggle_alarm - toggles the output state of the alarm LED and alarm buzzer
 *  reads LEDs current state then changes it to the opposite
 *
 * Return: Nothing
*/
static void toggle_alarm()
{
	static volatile bool toggle = false;

	if (toggle)
		tone(ALARM_BUZZER, 500);
	else
		noTone(ALARM_BUZZER);

	digitalWrite(ALARM_LED, toggle);
	toggle = !toggle;
}

/**
 * trigger_alarm - starts the alarm flashing and sound
 *
 * Return: Nothing
*/
void trigger_alarm()
{
	alarm_on_off = true;
	alarm_ticker.attach(ALARM_TOGGLE_PERIOD, toggle_alarm);
}

/**
 * silence_alarm - stops the alarm flashing and sound
 *
 * Return: Nothing
*/
void silence_alarm()
{
	alarm_on_off = false;
	alarm_ticker.detach();
	digitalWrite(ALARM_LED, LOW);
	noTone(ALARM_BUZZER);
}

/**
 * initialize_alarm - initializes the pins used for the alarm LED and
 *  alarm buzzer
 *
 * Return: Nothing
*/
void initialize_alarm()
{
	/* setting up pin connected to alarm LED as an output to flash */
	pinMode(ALARM_LED, OUTPUT);

	/* setting up pin connected to buzzer */
	pinMode(ALARM_BUZZER, OUTPUT);
}

