/** @file i2c_led_btn.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef I2C_BUTTON_H
#define I2C_BUTTON_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "i2c_master.h"
#include "simple_fifo.h"

#define I2C_BUTTON_DOUBLE_BUFFERING 0
#define I2C_BUTTON_INTERVAL_MS 60
#define USE_I2C_BTN_SYNC 1

typedef struct st_i2c_led_btn {
    i2c_slave_t slave;
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
    uint8_t led;
    uint32_t start_ms;
#endif
} i2c_led_btn_t;

#if (I2C_BUTTON_DOUBLE_BUFFERING != 1)
#ifndef USE_I2C_BTN_SYNC
bool i2c_led_btn_received_callback(simple_fifo_t *rxf);
#endif
#endif

void i2c_led_btn_init(i2c_master_t *master, i2c_led_btn_t *obj, int address, uint32_t start_ms);
void i2c_led_btn_task(i2c_led_btn_t *obj);

void i2c_led_btn_set_led(i2c_led_btn_t *obj, uint8_t val);
uint8_t i2c_led_btn_get_btn(i2c_led_btn_t *obj);
void i2c_led_btn_request_btn(i2c_led_btn_t *obj);
bool i2c_led_btn_btn_arrived(i2c_led_btn_t *obj);

#endif /* I2C_BUTTON_H */
