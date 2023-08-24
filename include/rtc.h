#include <Arduino.h>

/*
	library to interact with the DS3231 real-time clock, includes Wire.h
*/
#include <RTClib.h>

/* active RTC instance */
extern RTC_DS3231 my_RTC;

/* Functions to interact with the RTC */
void initialize_RTC();

/**
 * get_time_now - retrieves the current time from theRTC
 *
 * Return: current date/time
*/
inline DateTime get_time_now()
{
	return my_RTC.now();
}
