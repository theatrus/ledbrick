#ifndef _FAN_MONITOR_H_
#define _FAN_MONITOR_H_

#include <stdint.h>
#include <stdbool.h>

void fantach_init(void);
uint16_t fantach_rpm(void);
void fantach_enable(void);
void fantach_disable(void);
bool fantach_enabled(void);

#endif
