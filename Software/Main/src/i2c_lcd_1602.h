/** @file lcd_1602_i2c.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef I2C_LCD_1602_H
#define I2C_LCD_1602_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "i2c_master.h"
#include "simple_fifo.h"

#define I2C_LCD_1602_DOUBLE_BUFFERING 1

#define I2C_LCD_1602_MAX_CHARS 16
#define I2C_LCD_1602_MAX_LINES 2

typedef struct st_i2c_lcd_1602 {
    i2c_slave_t slave;
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    uint16_t screen[I2C_LCD_1602_MAX_LINES][I2C_LCD_1602_MAX_CHARS];   // for double buffering
    uint16_t screen_y;
    uint16_t screen_x;
    uint32_t update_ms;
    uint32_t dispoff_ms;
#endif
} i2c_lcd_1602_t;

void i2c_lcd_1602_init(i2c_master_t *master, i2c_lcd_1602_t *obj, int address, uint32_t start_ms);
void i2c_lcd_1602_init_screen(i2c_lcd_1602_t *obj);
void i2c_lcd_1602_request_update(i2c_lcd_1602_t *obj);
void i2c_lcd_1602_display_onoff(i2c_lcd_1602_t *obj, bool on);
void i2c_lcd_1602_task(i2c_lcd_1602_t *obj);

void i2c_lcd_1602_clear(i2c_lcd_1602_t *obj);
void i2c_lcd_1602_home(i2c_lcd_1602_t *obj);
void i2c_lcd_1602_locate(i2c_lcd_1602_t *obj, int x, int y);
void i2c_lcd_1602_shift(i2c_lcd_1602_t *obj, bool shift, bool right);
void i2c_lcd_1602_char(i2c_lcd_1602_t *obj, char c);
void i2c_lcd_1602_charset(i2c_lcd_1602_t *obj, char c, size_t len);
void i2c_lcd_1602_string(i2c_lcd_1602_t *obj, const char *s);
void i2c_lcd_1602_substring(i2c_lcd_1602_t *obj, const char *s, size_t len);
void i2c_lcd_1602_substring_nowait(i2c_lcd_1602_t *obj, const char *s, size_t len);
void i2c_lcd_1602_digit(i2c_lcd_1602_t *obj, int val);

#endif /* I2C_LCD_1602_H */
