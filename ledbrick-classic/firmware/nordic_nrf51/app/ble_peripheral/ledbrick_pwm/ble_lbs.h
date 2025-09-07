#ifndef BLE_LBS_H__
#define BLE_LBS_H__

#include <stdint.h>
#include <stdbool.h>
#include "ble.h"
#include "ble_srv_common.h"

#define LBS_UUID_BASE {0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15, 0xDE, 0xEF, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00}
#define LBS_UUID_SERVICE 0x1523
#define LBS_UUID_LED_CHAR 0x1525
#define LBS_UUID_FAN_CHAR 0x1524
#define LBS_UUID_TEMP_CHAR 0x1526

// Forward declaration of the ble_lbs_t type. 
typedef struct ble_lbs_s ble_lbs_t;

typedef void (*ble_lbs_led_write_handler_t) (ble_lbs_t * p_lbs, uint8_t led, uint8_t power);

typedef struct
{
    ble_lbs_led_write_handler_t led_write_handler;                    /**< Event handler to be called when LED characteristic is written. */
} ble_lbs_init_t;

typedef struct ble_lbs_s
{
    uint16_t                    service_handle;
    ble_gatts_char_handles_t    led_char_handles;
    ble_gatts_char_handles_t    fan_char_handles;
	  ble_gatts_char_handles_t    temp_char_handles;
    uint8_t                     uuid_type;
    uint16_t                    conn_handle;
    ble_lbs_led_write_handler_t led_write_handler;
} ble_lbs_t;

uint32_t ble_lbs_init(ble_lbs_t * p_lbs, const ble_lbs_init_t * p_lbs_init);

void ble_lbs_on_ble_evt(ble_lbs_t * p_lbs, ble_evt_t * p_ble_evt);

uint32_t ble_lbs_update_fan(ble_lbs_t* p_lbs, uint8_t* rpm);
uint32_t ble_lbs_update_temp(ble_lbs_t* p_lbs, uint8_t* temp);


#endif // BLE_LBS_H__

/** @} */
