#include <stdint.h>

/* shift ongoing/over */
extern volatile bool shift_status;

/* alarm triggered/silenced */
extern volatile bool alarm_on_off;
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
