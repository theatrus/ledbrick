#include <stdint.h>
#include <stdbool.h>
#include "nrf.h"
#include "boards.h"
#include <nrf_gpio.h>
#include <nrf_drv_gpiote.h>
#include <nrf_drv_ppi.h>
#include <nrf_drv_timer.h>
#include "error_handlers.h"

#define PIN_FANTACH 8

const nrf_drv_timer_t timer2 = NRF_DRV_TIMER_INSTANCE(2);
nrf_ppi_channel_t ppi_channel;
nrf_ppi_channel_t ppi_channel2;

nrf_drv_gpiote_in_config_t gpio_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(true);

static void pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
 // Not used
}

void timer_dummy_handler(nrf_timer_event_t event_type, void * p_context){
	if (event_type == NRF_TIMER_EVENT_COMPARE1)
		error_raise(ERROR_FAN);
}

uint16_t fantach_rpm(void) {
	uint32_t ticks = nrf_drv_timer_capture_get(&timer2, NRF_TIMER_CC_CHANNEL0);
	if (error_present(ERROR_FAN)) return 0;
	uint16_t rpm = (uint16_t)(( (31250.0f / 2.0f) / (float)ticks ) * 60.0f);
	return rpm;
}

void fantach_init(void) {
	
	nrf_drv_timer_init(&timer2, NULL, timer_dummy_handler);
  nrf_drv_timer_enable(&timer2);
	
	// Compare at max to check for stalled fans
	nrf_drv_timer_extended_compare(&timer2, NRF_TIMER_CC_CHANNEL1, 0xFFFFUL, NRF_TIMER_SHORT_COMPARE1_CLEAR_MASK, true);

	nrf_gpio_pin_dir_set(PIN_FANTACH, NRF_GPIO_PIN_DIR_INPUT);

	nrf_drv_ppi_init();
	nrf_drv_ppi_channel_alloc(&ppi_channel);
	nrf_drv_ppi_channel_alloc(&ppi_channel2);
	
	nrf_drv_gpiote_in_init(PIN_FANTACH, &gpio_config, pin_handler);
	nrf_drv_gpiote_in_event_enable(PIN_FANTACH, true);
	
	uint32_t ppi_addr = nrf_drv_gpiote_in_event_addr_get(PIN_FANTACH);
  uint32_t capture_addr = nrf_drv_timer_capture_task_address_get(&timer2, 0);
	uint32_t clear_addr = nrf_drv_timer_task_address_get(&timer2, NRF_TIMER_TASK_CLEAR);
	
	nrf_drv_ppi_channel_assign(ppi_channel, ppi_addr, capture_addr);
	nrf_drv_ppi_channel_enable(ppi_channel);
	
	nrf_drv_ppi_channel_assign(ppi_channel2, ppi_addr, clear_addr);
	nrf_drv_ppi_channel_enable(ppi_channel2);
	
}
