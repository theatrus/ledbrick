#ifndef PCA9685_H_
#define PCA9685_H_

#include <stdint.h>
#include <stdbool.h>

void pca9685_init(void);
void pca9685_write_led(uint8_t led, int on, int off);
void pca9685_enable(bool on);

#endif
