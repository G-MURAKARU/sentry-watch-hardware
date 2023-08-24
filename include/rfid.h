#include <Arduino.h>

/* string variable to store stringified RFID UID */
extern String card_id;

/* RFID reader functions */
void initialize_rfid();
bool rfid_read_new_card();