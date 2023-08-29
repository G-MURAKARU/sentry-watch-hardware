#ifndef __INC_ALARM_H
#define __INC_ALARM_H

#define ALARM_TOGGLE_PERIOD     0.5f

/* alarm triggered/silenced */
extern bool alarm_on_off;

/* Alarm functions */
void trigger_alarm(void);
void silence_alarm(void);
void initialize_alarm(void);

#endif		/* ifndef __INC_ALARM_H */
