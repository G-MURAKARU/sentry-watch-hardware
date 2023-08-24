#ifndef __INC_ALARM_H
#define __INC_ALARM_H

#define ALARM_TOGGLE_PERIOD     0.5f

/* alarm triggered/silenced */
extern volatile bool alarm_on_off;

/* Alarm functions */
void trigger_alarm();
void silence_alarm();
void initialize_alarm();

#endif		/* ifndef __INC_ALARM_H */
