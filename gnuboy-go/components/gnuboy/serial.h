#include "defs.h"

#define SERIAL_OUT   GPIO_NUM_12
#define SERIAL_IN    GPIO_NUM_15
#define SERIAL_CLOCK GPIO_NUM_4

void serial_init();
void serial_exchange(int use_internal_clock);
