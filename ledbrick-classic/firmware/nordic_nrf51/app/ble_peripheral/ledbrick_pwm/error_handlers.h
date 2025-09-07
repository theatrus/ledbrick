#ifndef _ERROR_HANDLERS_H_
#define _ERROR_HANDLERS_H_

#include <stdbool.h>

typedef enum {
	ERROR_FAN = 0,
	ERROR_TEMP = 1
} error_e;

void error_raise(error_e error);
bool error_present(error_e error);
bool error_any(void);
void error_init(void);


#endif
