
#include "serial.h"

#include "regs.h"
#include "driver/gpio.h"
#include "driver/ledc.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint8_t test_channel = 1;

int serial_clock_on = 0;

static int clock_level = 1;


void serial_init() {

	gpio_pad_select_gpio(SERIAL_OUT);

	gpio_set_direction(SERIAL_OUT,   GPIO_MODE_OUTPUT);
	gpio_set_direction(SERIAL_IN,    GPIO_MODE_INPUT);
	gpio_set_direction(SERIAL_CLOCK, GPIO_MODE_INPUT_OUTPUT);

	gpio_set_level(SERIAL_CLOCK, clock_level);
}

void serial_clock_low() {
	int output = (R_SB&0x80)>>7;
	R_SB <<= 1;
	printf("serial_clock_low() o=%02X sb=%02X\n", output, R_SB);
	gpio_set_level(SERIAL_OUT, output);
}

void serial_clock_high() {
	int input = gpio_get_level(SERIAL_IN);
	R_SB |= input;
	printf("serial_clock_high() i=%02X sb=%02X\n", input, R_SB);
}
	
void serial_task() {
	serial_init();
	while(1){
		
	}
}

void serial_exchange()
{
	serial_init();
	int counter = 8;
	while(counter >= 0) {
		gpio_set_level(SERIAL_CLOCK, 0);
		serial_clock_low();
		vTaskDelay(1);
		gpio_set_level(SERIAL_CLOCK, 1);
		serial_clock_high();
		vTaskDelay(1); //(0.125/portTICK_PERIOD_MS);
		counter--;
	}
}
