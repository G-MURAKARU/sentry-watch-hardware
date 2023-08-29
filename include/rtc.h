#ifndef __INC_RTC_DS3231_H
#define __INC_RTC_DS3231_H

#include <Arduino.h>

/*
 *	library to interact with the DS3231 real-time clock, includes Wire.h
*/
#include <RTClib.h>

/* active RTC instance */
extern RTC_DS3231 my_RTC;

/* Functions to interact with the RTC */
void initialize_RTC(void);

/**
 * get_time_now - retrieves the current time from theRTC
 *
 * Return: current date/time
*/
inline DateTime get_time_now(void)
{
	return (my_RTC.now());
}

#endif		/* ifndef __INC_RTC_DS3231_H */
