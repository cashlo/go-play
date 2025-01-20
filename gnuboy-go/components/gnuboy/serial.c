
#include "serial.h"

#include "regs.h"
#include "gnuboy.h"
#include "hw.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"
#include "esp32/rom/gpio.h"

#include "esp_log.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define FREQUENCY     8192
#define ESP_INTR_FLAG_DEFAULT 0
#define TIMER_RESOLUTION_HZ     1000000  // 1MHz resolution
#define TIMER_PERIOD_TICKS      (TIMER_RESOLUTION_HZ / FREQUENCY / 2)


static volatile int clock_level;
static volatile int clock_counter;
static volatile int data_counter;
static volatile int falling_edge_done;
static volatile int edge_counter;
static volatile int handler_id;
static int data = 0;
static int generate_clock = 0;
static int gpio_isr_service_running = 0;

static QueueHandle_t input_queue  = NULL;
static QueueHandle_t output_queue = NULL;

static TaskHandle_t current_handler_task = NULL;

static volatile bool serial_transfer_in_progress = false;

void serial_init() {

	gpio_pad_select_gpio(SERIAL_OUT);
	gpio_pad_select_gpio(SERIAL_IN);
	gpio_pad_select_gpio(SERIAL_CLOCK);

	ESP_ERROR_CHECK(gpio_set_direction(SERIAL_OUT,   GPIO_MODE_OUTPUT));
	ESP_ERROR_CHECK(gpio_set_direction(SERIAL_IN,    GPIO_MODE_INPUT));

	clock_level = 1;
	ESP_ERROR_CHECK(gpio_set_level(SERIAL_CLOCK, clock_level));

    if (input_queue == NULL) {
        input_queue = xQueueCreate(8, sizeof(int));
    }
    if (output_queue == NULL) {
        output_queue = xQueueCreate(8, sizeof(int));
    }

	falling_edge_done = 0;
	data_counter = 8;
	edge_counter = 8;
	data = 0;
}

void serial_clock_low() {
	int output_bit = 1;
	if (edge_counter > 0){
		if (xQueueReceiveFromISR(output_queue, &output_bit, NULL) != pdTRUE) {
			ESP_EARLY_LOGE("SERIAL", "Queue receive failed in ISR");
		}
		ESP_EARLY_LOGE("SERIAL", "Setting output bit: %d, bit %d", output_bit, edge_counter);
		//printf("serial_clock_low() o=%02X sb=%02X\n", output, R_SB);
	}
	ESP_ERROR_CHECK(gpio_set_level(SERIAL_OUT, output_bit));
}

void serial_clock_high() {
	if (edge_counter > 0){
		int input_bit = gpio_get_level(SERIAL_IN);
		if (xQueueSendFromISR(input_queue, &input_bit, (TickType_t) 0) != pdTRUE) {
			ESP_EARLY_LOGE("SERIAL", "Queue send failed in ISR");
		}
		edge_counter--;
		ESP_EARLY_LOGE("SERIAL", "Input bit: %d, %d bits remaining", input_bit, edge_counter);
		//printf("serial_clock_high() i=%02X sb=%02X\n", input, R_SB);
	}
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
	printf("Clean up\n");
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
    xQueueReset(input_queue);
    xQueueReset(output_queue);

	serial_transfer_in_progress = false;
	
    // If there's an existing handler task, delete it
    if (current_handler_task != NULL) {
        vTaskDelete(current_handler_task);
        current_handler_task = NULL;
        // Give the system a moment to clean up
        // vTaskDelay(pdMS_TO_TICKS(10));
    }


	
	// vTaskDelay(100);
	// gpio_set_level(SERIAL_OUT, 1); // This should be done with the last serial tick
}

void input_handler_task() {
	printf("Handler started...\n");
	int internal_counter = 0;
	int this_handler_id = handler_id;
	handler_id++;
	while(1){
		int input_bit;
		BaseType_t queue_result = xQueueReceive(input_queue, &input_bit, portMAX_DELAY);
		        
        if (queue_result != pdTRUE) {
            printf("Queue receive error\n");
            break;  // Exit task if queue error
        }

        if (data_counter <= 0) {
            printf("Data counter error\n");
            break;
        }

		printf("Serial bit received: %01X, data_counter:%02X, internal_counter:%03X, this_handler_id:%03X\n", input_bit, data_counter, internal_counter, this_handler_id);
		data <<= 1;
		data |= input_bit;
		if(data_counter == 1) {
			printf("Complete byte received: %02X\n", data);
			R_SB = data;
			hw_interrupt(IF_SERIAL, IF_SERIAL);
			R_SC &= 0x7f;
			clean_up();
			
			//hw_interrupt(0, IF_SERIAL);
		}
		data_counter--;
		internal_counter++;
	}
	current_handler_task = NULL;  // Clear handle if we break from loop
    vTaskDelete(NULL);
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
		ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT));
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

static void print_memory_info() {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    printf("Free heap: %d, Largest block: %d\n", 
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void serial_exchange(int use_internal_clock)
{
	if (serial_transfer_in_progress) {
        ESP_LOGE("SERIAL", "Serial transfer already in progress");
		clean_up();
		vTaskDelay(pdMS_TO_TICKS(1));  // Give a tiny bit of time for cleanup
        //return;
    }
    
    serial_transfer_in_progress = true;
	//print_memory_info();
	serial_init();
	printf("Send byte: %02X\n", R_SB);
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
	xReturned = xTaskCreatePinnedToCore(input_handler_task, "input_handler_task", 2048, NULL, 23, &current_handler_task, 1);
	if( xReturned != pdPASS ) {
		printf("TASK CREATE FAILED!!\n");
		clean_up();
	}

	return;
}
