#include "mcp9808.h"

#include "twi_master.h"
#include <stdint.h>

#define ADDR 0x3e

#define TEMP_REG 0x05


uint16_t mcp9808_temp(void) {
	uint8_t obuf[1] = { 0 };
	uint8_t ibuf[2] = { 0, 0 };
	obuf[0] = TEMP_REG;
	twi_master_transfer(ADDR, obuf, 1, false);
	twi_master_transfer(ADDR | 0x01, ibuf, 2, true);
	
	ibuf[0] &= 0x1f; // Clear flags
	
	//return ibuf[0] << 8 | ibuf[1];
	if (ibuf[0] & 0x10) {
		return 256 - (((ibuf[0] & 0x0f) * 16) + (ibuf[1] / 16));
	} else {
		return (ibuf[0] * 16) + (ibuf[1] / 16);
	}
}
