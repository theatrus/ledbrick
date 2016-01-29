#include <stdint.h>
#include <twi_master.h>
#include <nrf_gpio.h>
#include "pca9685.h"

#define ADDR 0xFE

#define REG_MODE1 0x00
#define REG_MODE2 0x01
#define REG_LED0 0x06 // Start of LED registers
#define REG_PRESCALE 0xFE

#define PIN_OE 1

static void pca9685_write(uint8_t reg, uint8_t data) {
	uint8_t obuf[2];
	obuf[0] = reg;
	obuf[1] = data;
	twi_master_transfer(ADDR, obuf, 2, true);
}

static void pca9685_bus_reset(void) {
	uint8_t obuf[1];
	obuf[0] = 0x06;
	twi_master_transfer(0, obuf, 1, true);
}

void pca9685_enable(bool on) {
	nrf_gpio_pin_write(PIN_OE, on);
}

void pca9685_write_led(uint8_t led, int on, int off) {
	uint8_t obuf[5];
	
	on += (20 * led);
	off += (20 * led);
	if (off >= 0xFFFE) off = 0xFFFF;
	
	obuf[0] = REG_LED0 + 4*led;
	obuf[1] = on & 0xFF;
	obuf[2] = (on >> 8) & 0xFF;
	obuf[3] = off & 0xFF;
	obuf[4] = (off >> 8) & 0xFF;
	
	twi_master_transfer(ADDR, obuf, 5, true);
}


static uint8_t pca9685_read(uint8_t reg) {
	uint8_t obuf[1];
	obuf[0] = reg;
	twi_master_transfer(ADDR, obuf, 1, true);
	obuf[0] = 0;
	twi_master_transfer(ADDR | 0x01, obuf, 1, true);
	return obuf[0];
}

void pca9685_init(void) {
	// Configure OE
	nrf_gpio_pin_dir_set(PIN_OE, NRF_GPIO_PIN_DIR_OUTPUT);
	nrf_gpio_pin_clear(PIN_OE);
	
	// Do a bus reset
	pca9685_bus_reset();
  pca9685_write(REG_PRESCALE, 0x17);

	pca9685_write(REG_MODE1, (1 << 7) | 1);
	for(;;) {
		if (0x01 == pca9685_read(REG_MODE1)) {
			break;
		}
	}
	pca9685_write(REG_MODE1, (1 << 5) | 1); // Auto-increment + on-call

	
	for(int i = 0; i < 16; i++) {
		pca9685_write_led(i, 0x0, 0x10);
	}
	
}
