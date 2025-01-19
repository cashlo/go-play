
#include "serial.h"

#include "regs.h"
#include "gnuboy.h"
#include "hw.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"
#include "esp32/rom/gpio.h"



#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define FREQUENCY     400
#define ESP_INTR_FLAG_DEFAULT 0
#define TIMER_RESOLUTION_HZ     1000000  // 1MHz resolution
#define TIMER_PERIOD_TICKS      (TIMER_RESOLUTION_HZ / FREQUENCY / 2)


volatile int clock_level = 1;
volatile int clock_counter = 0;
volatile int data_counter = 0;
volatile int falling_edge_done = 0;
static int data = 0;
static int generate_clock = 0;
static int gpio_isr_service_running = 0;

static QueueHandle_t input_queue  = NULL;
static QueueHandle_t output_queue = NULL;

void serial_init() {

	gpio_pad_select_gpio(SERIAL_OUT);
	gpio_pad_select_gpio(SERIAL_IN);
	gpio_pad_select_gpio(SERIAL_CLOCK);

	ESP_ERROR_CHECK(gpio_set_direction(SERIAL_OUT,   GPIO_MODE_OUTPUT));
	ESP_ERROR_CHECK(gpio_set_direction(SERIAL_IN,    GPIO_MODE_INPUT));

	clock_level = 1;
	ESP_ERROR_CHECK(gpio_set_level(SERIAL_CLOCK, clock_level));

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
	ESP_ERROR_CHECK(gpio_set_level(SERIAL_OUT, output_bit));
}

void serial_clock_high() {
	int input_bit = gpio_get_level(SERIAL_IN);
	xQueueSendFromISR(input_queue, &input_bit, (TickType_t) 0);
	//printf("serial_clock_high() i=%02X sb=%02X\n", input, R_SB);
}

static gptimer_handle_t gptimer = NULL;

// New timer callback function
static bool IRAM_ATTR timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    int internal_clock = (int)user_ctx;
    clock_level = !clock_level;
    
    if (clock_level) {
        if (falling_edge_done) serial_clock_high();
    } else {
        falling_edge_done = 1;
        serial_clock_low();
    }
    
    if (internal_clock) {
        gpio_set_level(SERIAL_CLOCK, clock_level);
        if (clock_counter > 1) {
            // Timer will continue running
            clock_counter--;
            return true;
        } else {
            // Stop timer
            clock_counter--;
            return false;
        }
    }
    return true;
}

void start_serial_timer() {
    // Timer configuration
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = TIMER_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    // Alarm configuration
    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = TIMER_PERIOD_TICKS,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    // Register timer interrupt handler
    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, (void*)1));

    // Enable and start timer
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
}

void clean_up(){
	if(!generate_clock){
		printf("Stopping external interrupt...\n");
		ESP_ERROR_CHECK(gpio_isr_handler_remove(SERIAL_CLOCK));
		//gpio_uninstall_isr_service();
	} else if (gptimer) {
        gptimer_stop(gptimer);
        gptimer_disable(gptimer);
        gptimer_del_timer(gptimer);
        gptimer = NULL;
    }
	vQueueDelete(input_queue);
	vQueueDelete(output_queue);
	R_SC &= 0x7f;
	
	
	// vTaskDelay(100);
	// gpio_set_level(SERIAL_OUT, 1); // This should be done with the last serial tick
}

void input_handler_task() {
	printf("Handler started...\n");
	while(1){
		int input_bit;
		xQueueReceive(input_queue, &input_bit, portMAX_DELAY);
		//printf("Serial bit received: %01X, data_counter:%02X\n", input_bit, data_counter);
		data <<= 1;
		data |= input_bit;
		if(data_counter == 1) {
			printf("Complete byte received: %02X\n", data);
			R_SB = data;
			clean_up();
			hw_interrupt(IF_SERIAL, IF_SERIAL);
			hw_interrupt(0, IF_SERIAL);
			printf("Destorying handler...\n");
			vTaskDelete(NULL);
		}
		data_counter--;
	}
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int internal_clock = (int) arg;
    clock_level = !clock_level;
    
    if (clock_level) {
        if (falling_edge_done) serial_clock_high();
    } else {
        falling_edge_done = 1;
        serial_clock_low();
    }
}


void external_interupt_init() {
//	gpio_config_t clock_in_conf;
//	clock_in_conf.intr_type = GPIO_PIN_INTR_ANYEGDE;
//	clock_in_conf.pin_bit_mask = SERIAL_CLOCK;
//	gpio_config(&clock_in_conf);
	printf("Setting up external interrupt...\n");
	ESP_ERROR_CHECK(gpio_set_intr_type(SERIAL_CLOCK, GPIO_INTR_ANYEDGE));
	if (!gpio_isr_service_running) {
		gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
		gpio_isr_service_running = 1;
	}
	ESP_ERROR_CHECK(gpio_isr_handler_add(SERIAL_CLOCK, gpio_isr_handler, (void*) 0));
}

void fill_output_queue(int data){
	for (int a = 0; a < 8; a++) {
		int bit = (data&0x80)>>7;
		data <<= 1;
		xQueueSend(output_queue, (void *) &bit, (TickType_t) 0);
	}
}

void serial_exchange(int use_internal_clock)
{
	serial_init();
	//printf("Send byte: %02X\n", R_SB);
	//printf("Serial Starting, RAM left %d\n", esp_get_free_heap_size());
	fill_output_queue(R_SB);
	generate_clock = use_internal_clock;
	if(use_internal_clock){
		gpio_isr_handler_remove(SERIAL_CLOCK);
		//gpio_uninstall_isr_service();
		clock_counter = 8*2;
		gpio_set_direction(SERIAL_CLOCK, GPIO_MODE_OUTPUT);
		start_serial_timer();
	} else {
		gpio_set_direction(SERIAL_CLOCK, GPIO_MODE_INPUT);
		external_interupt_init();
		
	}
	BaseType_t xReturned;
	xReturned = xTaskCreatePinnedToCore(input_handler_task, "input_handler_task", 2048, NULL, 10, NULL, 1);
	if( xReturned != pdPASS ) {
		printf("TASK CREATE FAILED!!\n");
		clean_up();
	}

	return;
}
