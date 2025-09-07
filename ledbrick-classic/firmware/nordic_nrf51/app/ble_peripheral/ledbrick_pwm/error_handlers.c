#include <stdint.h>
#include <stdbool.h>
#include <nrf_gpio.h>
#include "error_handlers.h"
#include <app_timer.h>

#define PIN_ERRORLED 12

static int errors = 0;
static int errors_last = 0;
static app_timer_id_t timer;

static void on_update(void) {
	if (errors) {
		nrf_gpio_pin_clear(PIN_ERRORLED);
	}
}

static void on_timer(void* ctx) {
	if (errors == 0 && errors_last == 0)
		nrf_gpio_pin_set(PIN_ERRORLED);
	errors_last = errors;
	errors = 0;
}


void error_raise(error_e error) {
	errors |= (1 << error);
	on_update();
}

bool error_present(error_e error) {
	return (errors & (1 << error) || errors_last & (1 << error));
}

bool error_any(void) {
	return errors != 0 || errors_last != 0;
}

void error_init(void) {
	nrf_gpio_pin_dir_set(PIN_ERRORLED, NRF_GPIO_PIN_DIR_OUTPUT);
	app_timer_create(&timer, APP_TIMER_MODE_REPEATED, on_timer);
	app_timer_start(timer, APP_TIMER_TICKS(5000, 0), NULL);
}
