
#include "serial.h"

#include "regs.h"
#include "gnuboy.h"
#include "hw.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/timer.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define TIMER_GROUP TIMER_GROUP_1
#define TIMER_DIVIDER 1024
#define TIMER_SCALE   (TIMER_BASE_CLK / TIMER_DIVIDER)
#define FREQUENCY     800
#define TIMER_INDEX   1
#define ESP_INTR_FLAG_DEFAULT 0


uint8_t test_channel = 1;

int serial_clock_on = 0;

volatile int clock_level = 1;
volatile int clock_counter = 0;
volatile int data_counter = 0;
volatile int falling_edge_done = 0;
static int data = 0;
static int generate_clock = 0;

static xQueueHandle input_queue  = NULL;
static xQueueHandle output_queue = NULL;

void serial_init() {

	gpio_pad_select_gpio(SERIAL_OUT);
	gpio_pad_select_gpio(SERIAL_IN);
	gpio_pad_select_gpio(SERIAL_CLOCK);

	gpio_set_direction(SERIAL_OUT,   GPIO_MODE_OUTPUT);
	gpio_set_direction(SERIAL_IN,    GPIO_MODE_INPUT);

	clock_level = 1;
	gpio_set_level(SERIAL_CLOCK, clock_level);

	input_queue = xQueueCreate(8, sizeof(int));
	output_queue = xQueueCreate(8, sizeof(int));

	falling_edge_done = 0;
	data_counter = 8;
	data = 0;
}

void serial_clock_low() {
	int output_bit;
	xQueueReceiveFromISR(output_queue, &output_bit, NULL);
	//printf("serial_clock_low() o=%02X sb=%02X\n", output, R_SB);
	gpio_set_level(SERIAL_OUT, output_bit);
}

void serial_clock_high() {
	int input_bit = gpio_get_level(SERIAL_IN);
	xQueueSendFromISR(input_queue, &input_bit, NULL);
	//printf("serial_clock_high() i=%02X sb=%02X\n", input, R_SB);
}
	
void IRAM_ATTR timer_isr(void *arg) {
	int internal_clock = (int) arg; 
	clock_level = !clock_level;
	if (clock_level) {
		if (falling_edge_done) serial_clock_high();
	} else {
		falling_edge_done = 1;
		serial_clock_low();
	}
	if(internal_clock){
		gpio_set_level(SERIAL_CLOCK, clock_level);
		TIMERG1.int_clr_timers.t1 = 1;
		if(clock_counter > 1) {
			TIMERG1.hw_timer[TIMER_INDEX].config.alarm_en = 1;
		}
		clock_counter--;	
	}
	
}

void start_serial_timer() {
	timer_config_t config;
	config.divider = TIMER_DIVIDER;
	config.counter_dir = TIMER_COUNT_UP;
	config.counter_en = TIMER_PAUSE;
	config.alarm_en = TIMER_ALARM_EN;
	config.intr_type = TIMER_INTR_LEVEL;
	config.auto_reload = 1;
	timer_init(TIMER_GROUP, TIMER_INDEX, &config);
	timer_set_counter_value(TIMER_GROUP, TIMER_INDEX, 0x00000000ULL);
	timer_set_alarm_value(TIMER_GROUP, TIMER_INDEX, TIMER_SCALE / FREQUENCY / 2);
	timer_enable_intr(TIMER_GROUP, TIMER_INDEX);
	timer_isr_register(TIMER_GROUP, TIMER_INDEX, timer_isr, (void*) 1, ESP_INTR_FLAG_IRAM, NULL);
	
	timer_start(TIMER_GROUP, TIMER_INDEX);
}

void clean_up(){
	if(!generate_clock){
		gpio_isr_handler_remove(SERIAL_CLOCK);
		gpio_uninstall_isr_service();
	}
	R_SC &= 0x7f;
	
	// vTaskDelay(100);
	// gpio_set_level(SERIAL_OUT, 1); // This should be done with the last serial tick
}

void input_handler_task() {
	printf("Handler started...\n");
	while(1){
		int input_bit;
		xQueueReceive(input_queue, &input_bit, portMAX_DELAY);
		printf("Serial bit received: %01X, data_counter:%02X\n", input_bit, data_counter);
		data <<= 1;
		data |= input_bit;
		if(data_counter == 1) {
			printf("Complete byte received: %02X\n", data);
			R_SB = data;
			hw_interrupt(IF_SERIAL, IF_SERIAL);
			hw_interrupt(0, IF_SERIAL);
			clean_up();
			vTaskDelete(NULL);
		}
		data_counter--;
	}
}

void external_interupt_init() {
//	gpio_config_t clock_in_conf;
//	clock_in_conf.intr_type = GPIO_PIN_INTR_ANYEGDE;
//	clock_in_conf.pin_bit_mask = SERIAL_CLOCK;
//	gpio_config(&clock_in_conf);
	printf("Setting up external interrupt...\n");
	gpio_set_intr_type(SERIAL_CLOCK, GPIO_INTR_ANYEDGE);
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(SERIAL_CLOCK, timer_isr, (void*) 0);
}

void fill_output_queue(int data){
	for (int a = 0; a < 8; a++) {
		int bit = (data&0x80)>>7;
		data <<= 1;
		xQueueSend(output_queue, &bit, NULL);
	}
}

void serial_exchange(int use_internal_clock)
{
	serial_init();
	printf("Send byte: %02X\n", R_SB);
	fill_output_queue(R_SB);
	generate_clock = use_internal_clock;
	if(use_internal_clock){
		clock_counter = 8*2;
		gpio_set_direction(SERIAL_CLOCK, GPIO_MODE_OUTPUT);
		start_serial_timer();
	} else {
		gpio_set_direction(SERIAL_CLOCK, GPIO_MODE_INPUT);
		external_interupt_init();
		
	}
	xTaskCreate(input_handler_task, "input_handler_task", 2048, NULL, 10, NULL);
		

	return;
}
