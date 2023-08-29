#ifndef __INC_RFID_READER_H
#define __INC_RFID_READER_H

#include <Arduino.h>

/* string variable to store stringified RFID UID */
extern String card_id;

/* RFID reader functions */
void initialize_rfid(void);
bool rfid_read_new_card(void);

#endif		/* ifndef __INC_RFID_READER_H */
