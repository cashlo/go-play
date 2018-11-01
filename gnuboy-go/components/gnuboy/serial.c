#pragma GCC optimize ("O3")

#include "serial.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

uint8_t test_pin = 12;
uint8_t test_channel = 1;

int serial_clock_on = 0;


byte serial_exchange(byte send_byte)
{
	if (!serial_clock_on) {
		printf("starting serial clock\n");
		gpio_set_direction(test_pin, GPIO_MODE_OUTPUT);
		ledc_channel_config_t ledc_channel = {
		    .channel    = LEDC_CHANNEL_1,
		    .duty       = 127,
		    .gpio_num   = test_pin,
		    .speed_mode = LEDC_HIGH_SPEED_MODE,
		    .timer_sel  = LEDC_TIMER_1
		};
		ledc_channel_config(&ledc_channel);
		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = 8000,                      // frequency of PWM signal
			.speed_mode = LEDC_HIGH_SPEED_MODE,           // timer mode
			.timer_num = LEDC_TIMER_1 // timer index			
		};
		ledc_timer_config(&ledc_timer);
		serial_clock_on = 1;
	}
		return 0x55;
}
