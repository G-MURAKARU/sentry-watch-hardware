#include <Arduino.h>
#include "rtc.h"


/* active RTC instance */
RTC_DS3231 my_RTC;

/**
 * initialize_RTC - initializes the RTC and sets the time if power was lost
 *
 * Return: Nothing
*/
void initialize_RTC()
{
	my_RTC.begin();
	
	/*
		set time of the RTC
		adjusts it to time at which the code was compiled
		time reference is time on the device on which the compiler ran
		NB: set timezone of device to GMT+0 to set correct UTC epoch time
				i.e. without timezone offset
	*/
	if (my_RTC.lostPower())
		my_RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
}
