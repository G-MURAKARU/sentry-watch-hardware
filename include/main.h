#ifndef __INC_MAIN_H
#define __INC_MAIN_H

#include <stdint.h>

/* LED simulating alarm siren */
#define ALARM_LED 32
/* buzzer simulating alarm siren */
#define ALARM_BUZZER 33

/* connected to MFRC reader reset pin */
#define MFRC_RST_PIN 4
/* MFRC reader SPI chip-select pin */
#define MFRC_SS_PIN 5

/* checkpoint ID to send with a sentry scan */
#define CHECKPOINT_ID "A";

/* Indicator for whether shift is ongoing/over */
extern volatile bool shift_status;

extern volatile uint8_t alarm_reason;

enum alerts_e {
	SUCCESS = 1,
	UNKNOWN_CARD = 2,
	STOLEN_CARD = 3,
	WRONG_CHECKPOINT = 4,
	WRONG_TIME = 5,
	OVERDUE_SCAN = 6,
	NO_SHIFT_SCAN = 7
};

#endif		/* ifndef __INC_MAIN_H */
