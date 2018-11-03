
#include "serial.h"

#include "regs.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/timer.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define TIMER_GROUP TIMER_GROUP_1
#define TIMER_DIVIDER 1024
#define TIMER_SCALE   (TIMER_BASE_CLK / TIMER_DIVIDER)
#define FREQUENCY     8000
#define TIMER_INDEX   1
#define ESP_INTR_FLAG_DEFAULT 0


uint8_t test_channel = 1;

int serial_clock_on = 0;

volatile int clock_level = 1;
volatile int counter = 0;
volatile int data;

xQueueHandle timer_queue;

typedef struct {
    int type;  // the type of timer's event
} timer_event_t;


void serial_init() {

	gpio_pad_select_gpio(SERIAL_OUT);
	gpio_pad_select_gpio(SERIAL_IN);
	gpio_pad_select_gpio(SERIAL_CLOCK);

	gpio_set_direction(SERIAL_OUT,   GPIO_MODE_OUTPUT);
	gpio_set_direction(SERIAL_IN,    GPIO_MODE_INPUT);
	gpio_set_direction(SERIAL_CLOCK, GPIO_MODE_INPUT_OUTPUT);

	gpio_set_level(SERIAL_CLOCK, clock_level);
}

void serial_clock_low() {
	int output = (data&0x80)>>7;
	data <<= 1;
	//printf("serial_clock_low() o=%02X sb=%02X\n", output, R_SB);
	gpio_set_level(SERIAL_OUT, output);
}

void serial_clock_high() {
	int input = gpio_get_level(SERIAL_IN);
	data |= input;
	ets_printf("input bit: %01X\n", input);
	//printf("serial_clock_high() i=%02X sb=%02X\n", input, R_SB);
}

static xQueueHandle data_event_queue = NULL;
	
void IRAM_ATTR timer_isr(void *arg) {
	ets_printf("OMG CLOCK, %02X data: %02X\n", counter, data);
	int internal_clock = (int) arg; 
	if (clock_level) {
		serial_clock_high();
	} else {
		serial_clock_low();
	}
	clock_level = !clock_level;
	if(internal_clock){
		gpio_set_level(SERIAL_CLOCK, clock_level);
		TIMERG1.int_clr_timers.t1 = 1;
		if(counter > 0) {
			TIMERG1.hw_timer[TIMER_INDEX].config.alarm_en = 1;
		}
		counter--;
	}
	if (counter == 0) {
		xQueueSendFromISR(data_event_queue, &internal_clock, NULL);
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

void serial_event_task() {
	while(1){
		int test;
		xQueueReceive(data_event_queue, &test, portMAX_DELAY);
		printf("OMG EVENT!! data: %02X\n", data);
	}
}

void external_interupt_init() {
//	gpio_config_t clock_in_conf;
//	clock_in_conf.intr_type = GPIO_PIN_INTR_ANYEGDE;
//	clock_in_conf.pin_bit_mask = SERIAL_CLOCK;
//	gpio_config(&clock_in_conf);
	printf("Setting up external interrupt...\n");
	data_event_queue = xQueueCreate(10, sizeof(int));
	gpio_set_intr_type(SERIAL_CLOCK, GPIO_INTR_ANYEDGE);
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(SERIAL_CLOCK, timer_isr, (void*) 0);
	xTaskCreate(serial_event_task, "serial_event_task", 2048, NULL, 10, NULL);
}

void serial_exchange(int use_internal_clock)
{
	serial_init();
	counter = 8*2-1;
	if(use_internal_clock){
		data = R_SB;
		start_serial_timer();
		//xTaskCreate(serial_event_task, "serial_event_task", 2048, NULL, 5, NULL);
		//timer_event_t test;
		//xQueueReceive(timer_queue, &test, portMAX_DELAY);
		vTaskDelay(1);
		printf("OMG data:%02X!!\n", data);
		R_SB = data;
	} else {
		external_interupt_init();
		
	}

	return;
}
