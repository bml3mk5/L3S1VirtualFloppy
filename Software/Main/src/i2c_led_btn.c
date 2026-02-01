/** @file i2c_led_btn.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "i2c_led_btn.h"
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <hardware/irq.h>
#include <hardware/dma.h>
#include <pico/binary_info.h>
#include "i2c_master.h"

// slave address
static int c_addr = 0x41;

static uint8_t recv_data = 0;

#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
static bool data_recved = false;
static const uint32_t interval_ms = I2C_BUTTON_INTERVAL_MS;
#endif

//--------------------------------------------------------------------+

void __no_inline_not_in_flash_func(i2c_led_btn_set_led)(i2c_led_btn_t *obj, uint8_t val)
{
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
    obj->led = val;
#else
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, 0, val);
#endif
}

uint8_t __no_inline_not_in_flash_func(i2c_led_btn_get_btn)(i2c_led_btn_t *obj)
{
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
    data_recved = false;
#else
#ifdef USE_I2C_BTN_SYNC
    recv_data = i2c_master_recv_byte(&obj->slave);
#endif
#endif
    return recv_data;
}

void __no_inline_not_in_flash_func(i2c_led_btn_request_btn)(i2c_led_btn_t *obj)
{
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
#else
    i2c_master_recv_request_byte(&obj->slave, TRANS_STATE_RECV, 0);
#endif
}

bool __no_inline_not_in_flash_func(i2c_led_btn_btn_arrived)(i2c_led_btn_t *obj)
{
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
    return data_recved;
#else
    return i2c_master_recv_data_arrived(&obj->slave);
#endif
}

//--------------------------------------------------------------------+

void i2c_led_btn_init(i2c_master_t *master, i2c_led_btn_t *obj, int address, uint32_t start_ms)
{
    i2c_slave_init(master, &obj->slave, address == 0 ? c_addr : address, 1, 1);
#if (I2C_BUTTON_DOUBLE_BUFFERING != 1)
#ifndef USE_I2C_BTN_SYNC
    obj->slave.recvd = i2c_led_btn_received_callback;
#endif
#endif
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
    obj->led = 0;
    obj->start_ms = start_ms;
#endif
}

#if (I2C_BUTTON_DOUBLE_BUFFERING != 1)
#ifndef USE_I2C_BTN_SYNC
bool i2c_led_btn_received_callback(simple_fifo_t *rxf)
{
    if (fifo_is_not_empty(rxf)) {
        recv_data = fifo_pop16(rxf);
//      printf("RECV: %02x\n", recv_data);
        return true;
    }
    return false;
}
#endif
#endif

#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
static void update_indicator(i2c_led_btn_t *obj)
{
    if (i2c_master_recv_data_arrived(&obj->slave)) {
        recv_data = i2c_master_recv_byte(&obj->slave);
        data_recved = true;
    }
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, 0, obj->led);
    i2c_master_recv_request_byte(&obj->slave, TRANS_STATE_RECV, 0);
}
#endif

void i2c_led_btn_task(i2c_led_btn_t *obj)
{
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
    uint32_t curr_ms = to_ms_since_boot(get_absolute_time());
    if (curr_ms - obj->start_ms < interval_ms) return; // not enough time
    obj->start_ms += interval_ms;
    update_indicator(obj);
#endif
}
