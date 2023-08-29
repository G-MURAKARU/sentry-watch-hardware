#ifndef __INC_DISPLAY_LCD_H
#define __INC_DISPLAY_LCD_H

#include <Arduino.h>

/**
 * enum display_status_e - For displaying connection status
*/
typedef enum display_status_e{
    DISPLAY_SUCCESS = 1,
    DISPLAY_FAILURE = 2
} display_status_t;

/* LCD Screen functions */
void display_default_text(display_status_t, display_status_t);
void display_scanning_verifying();
void display_connecting_to_wifi();
void display_mqtt_retry();
void display_AP_mode();
void display_valid_scan();
void display_invalid_scan(uint8_t);
void display_scan_time_elapsed();
void initialize_display();

#endif		/* ifndef __INC_DISPLAY_LCD_H */
