/** @file lcd_1602_i2c.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "i2c_lcd_1602.h"
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <hardware/irq.h>
#include <hardware/dma.h>
#include <pico/binary_info.h>
#include "i2c_master.h"

/* drive a 16x2 LCD panel */

// commands
const int LCD_CLEARDISPLAY = 0x01;
const int LCD_RETURNHOME = 0x02;
const int LCD_ENTRYMODESET = 0x04;
const int LCD_DISPLAYCONTROL = 0x08;
const int LCD_CURSORSHIFT = 0x10;
const int LCD_FUNCTIONSET = 0x20;
const int LCD_SETCGRAMADDR = 0x40;
const int LCD_SETDDRAMADDR = 0x80;

// commands instruction set 1
const int LCD_OSCFREQUENCY = 0x10;
const int LCD_POWERICONCTRL = 0x50;
const int LCD_FOLLOWERCTRL = 0x60;
const int LCD_CONSTRUCTSET = 0x70;

// flags for display entry mode
const int LCD_ENTRYSHIFTINCREMENT = 0x01;
const int LCD_ENTRYRIGHT = 0x02;

// flags for display and cursor control
const int LCD_BLINKON = 0x01;
const int LCD_CURSORON = 0x02;
const int LCD_DISPLAYON = 0x04;

// flags for display and cursor shift
const int LCD_MOVERIGHT = 0x04;
const int LCD_DISPLAYMOVE = 0x08;

// flags for function set
const int LCD_5x10DOTS = 0x04;
const int LCD_2LINE = 0x08;
const int LCD_8BITMODE = 0x10;
const int LCD_INSTRUCTION = 0x01;

const int LCD_IS3V3 = 0x04;

// By default these LCD1602 display drivers are on bus address 0x3e
static const int c_addr = 0x3e;

// Modes for lcd_send_byte
#define LCD_CHARACTER  0x40
#define LCD_COMMAND    0

#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
// updating screen ratio
static const uint32_t update_interval_ms = 100; // 10fps
// display off time
static const uint32_t dispoff_interval_ms = (1 * 60 * 1000);
#endif

//--------------------------------------------------------------------+

static void i2c_lcd_1602_send_byte_blocking(i2c_slave_t *obj, uint8_t ctrl, uint8_t data)
{
    i2c_master_send_byte_blocking(obj, TRANS_STATE_SEND, ctrl, data);
}

void i2c_lcd_1602_clear(i2c_lcd_1602_t *obj)
{
    i2c_lcd_1602_send_byte_blocking(&obj->slave, LCD_COMMAND, LCD_CLEARDISPLAY);
}

void i2c_lcd_1602_home(i2c_lcd_1602_t *obj)
{
    i2c_lcd_1602_send_byte_blocking(&obj->slave, LCD_COMMAND, LCD_RETURNHOME);
}

// go to location on LCD
static void send_locate(i2c_lcd_1602_t *obj, int x, int y)
{
    int val = LCD_SETDDRAMADDR;
    val |= ((y & 1) << 6);
    val |= (x & 0x3f);
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, LCD_COMMAND, (uint8_t)val);
}
static void __not_in_flash_func(set_screen_locate)(i2c_lcd_1602_t *obj, int x, int y)
{
    obj->screen_y = y;
    obj->screen_x = x;
}
void __not_in_flash_func(i2c_lcd_1602_locate)(i2c_lcd_1602_t *obj, int x, int y)
{
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    set_screen_locate(obj, x, y);
#else
    send_locate(obj, x, y);
#endif
}

void i2c_lcd_1602_shift(i2c_lcd_1602_t *obj, bool shift, bool right)
{
    int val = LCD_ENTRYMODESET;
    if (shift) val |= LCD_ENTRYSHIFTINCREMENT;
    if (right) val |= LCD_ENTRYRIGHT;
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, LCD_COMMAND, (uint8_t)val);
}

static void __not_in_flash_func(send_char)(i2c_lcd_1602_t *obj, char c)
{
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, LCD_CHARACTER, (uint8_t)c);
}
static void __not_in_flash_func(set_screen_char)(i2c_lcd_1602_t *obj, char c)
{
    obj->screen[obj->screen_y][obj->screen_x] = (0x8000 | (uint16_t)c);
    obj->screen_x++;
    if (obj->screen_x >= I2C_LCD_1602_MAX_CHARS) {
        obj->screen_y++;
    }
    if (obj->screen_y >= I2C_LCD_1602_MAX_LINES) {
        obj->screen_y = 0;
    }
}
void __not_in_flash_func(i2c_lcd_1602_char)(i2c_lcd_1602_t *obj, char c)
{
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    set_screen_char(obj, c);
#else
    send_char(obj, c);
#endif
}

static void __not_in_flash_func(send_substring)(i2c_lcd_1602_t *obj, const char *s, size_t len)
{
    i2c_master_send_string(&obj->slave, TRANS_STATE_SEND, LCD_CHARACTER, s, len);
}
static void __not_in_flash_func(set_screen_substring)(i2c_lcd_1602_t *obj, const char *s, size_t len)
{
    for(size_t i=0; i<len; i++) {
        set_screen_char(obj, s[i]);
    }
}

void __not_in_flash_func(i2c_lcd_1602_charset)(i2c_lcd_1602_t *obj, char c, size_t len)
{
    char str[16];
    size_t slen = len < 16 ? len : 16;
    memset(str, c, slen);
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    set_screen_substring(obj, str, slen);
#else
    send_substring(obj, str, slen);
#endif
}

void __not_in_flash_func(i2c_lcd_1602_string)(i2c_lcd_1602_t *obj, const char *s)
{
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    set_screen_substring(obj, s, strlen(s));
#else
    send_substring(obj, s, strlen(s));
#endif
}

void __not_in_flash_func(i2c_lcd_1602_substring)(i2c_lcd_1602_t *obj, const char *s, size_t len)
{
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    set_screen_substring(obj, s, len);
#else
    send_substring(obj, s, len);
#endif
}

void i2c_lcd_1602_substring_nowait(i2c_lcd_1602_t *obj, const char *s, size_t len)
{
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    set_screen_substring(obj, s, len);
#else
	if (!i2c_master_tx_buffer_is_not_full(obj->slave.master, len)) return;
    send_substring(obj, s, len);
#endif
}

void i2c_lcd_1602_digit(i2c_lcd_1602_t *obj, int val)
{
    char str[16];
    sprintf(str, "%d", val);
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    set_screen_substring(obj, str, strlen(str));
#else
    send_substring(obj, str, strlen(str));
#endif
}

//--------------------------------------------------------------------+

void i2c_lcd_1602_init_screen(i2c_lcd_1602_t *obj)
{
    i2c_slave_t *slave = &obj->slave;
    if (!slave->baud) return;

    sleep_ms(200);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_FUNCTIONSET | LCD_8BITMODE | LCD_2LINE);
    sleep_us(50);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_FUNCTIONSET | LCD_8BITMODE | LCD_2LINE | LCD_INSTRUCTION);
    sleep_us(50);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_OSCFREQUENCY | 0x04);
    sleep_us(50);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_CONSTRUCTSET | 0x03);
    sleep_us(50);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_POWERICONCTRL | 0x02 | LCD_IS3V3);
    sleep_us(50);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_FOLLOWERCTRL | 0x0c);
    sleep_ms(200);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_FUNCTIONSET | LCD_8BITMODE | LCD_2LINE);
    sleep_us(50);
//    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_BLINKON);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_DISPLAYCONTROL | LCD_DISPLAYON);
    sleep_us(50);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_CLEARDISPLAY);
    sleep_ms(2);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_ENTRYMODESET | LCD_ENTRYSHIFTINCREMENT | LCD_ENTRYRIGHT);
    sleep_us(50);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_RETURNHOME);
    sleep_ms(2);
    i2c_lcd_1602_send_byte_blocking(slave, LCD_COMMAND, LCD_ENTRYMODESET | LCD_ENTRYRIGHT);
    sleep_ms(1);
}

static void display_on(i2c_lcd_1602_t *obj)
{
    /* display on */
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, LCD_COMMAND, LCD_DISPLAYCONTROL | LCD_DISPLAYON);
}

static void display_off(i2c_lcd_1602_t *obj)
{
    /* display off */
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, LCD_COMMAND, LCD_DISPLAYCONTROL);
}

void i2c_lcd_1602_init(i2c_master_t *master, i2c_lcd_1602_t *obj, int address, uint32_t start_ms)
{
    i2c_slave_init(master, &obj->slave, address == 0 ? c_addr : address, 2, 0);
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    memset(obj->screen, 0, sizeof(obj->screen));
    obj->screen_x = 0;
    obj->screen_y = 0;
    obj->update_ms = start_ms;
    obj->dispoff_ms = dispoff_interval_ms;
#endif
}

static void update_screen_parts(i2c_lcd_1602_t *obj, char *str, int len, int y, int stx)
{
    if (len > 0) {
        // buffer is full?
        if (i2c_master_tx_buffer_is_not_full(obj->slave.master, len * 16 + 16)) {
            // set locate
            send_locate(obj, stx, y);
            // send string
            send_substring(obj, str, len);

            uint16_t *pp = &obj->screen[y][stx];
            for(int px=0; px<len; px++) {
                *pp &= ~0x8000;
                pp++;
            }
        }
    }
}

static void update_screen(i2c_lcd_1602_t *obj)
{
    char str[I2C_LCD_1602_MAX_CHARS];
    int len;
    int stx;

	bool upd = false;
	uint16_t *p;
    for(int y=0; y<I2C_LCD_1602_MAX_LINES; y++) {
        p = obj->screen[y];
        len = 0;
        stx = 0xffff;
        for(int x=0; x<I2C_LCD_1602_MAX_CHARS; x++) {
            if (*p & 0x8000) {
                // need update
                if (stx > x) stx = x;
                str[len] = (char)(*p);
                len++;
                upd = true;
            } else {
                update_screen_parts(obj, str, len, y, stx);
                len = 0;
                stx = 0xffff;
            }
            p++;
        }
        update_screen_parts(obj, str, len, y, stx);
    }

    i2c_lcd_1602_display_onoff(obj, upd);
}

void i2c_lcd_1602_request_update(i2c_lcd_1602_t *obj)
{
    for(int y=0; y<I2C_LCD_1602_MAX_LINES; y++) {
        uint16_t *p = obj->screen[y];
        for(int x=0; x<I2C_LCD_1602_MAX_CHARS; x++) {
            *p |= 0x8000;
            p++;
        }
    }
}

void i2c_lcd_1602_display_onoff(i2c_lcd_1602_t *obj, bool on)
{
    uint32_t curr_ms = to_ms_since_boot(get_absolute_time());
    if (on) {
        if (obj->dispoff_ms == 0) {
            // display on
            display_on(obj);
        }
        obj->dispoff_ms = curr_ms + dispoff_interval_ms;
    } else {
        if (obj->dispoff_ms != 0 && obj->dispoff_ms < curr_ms) {
            // display off
            display_off(obj);
            obj->dispoff_ms = 0;
        }
    }
}

void i2c_lcd_1602_task(i2c_lcd_1602_t *obj)
{
#if (I2C_LCD_1602_DOUBLE_BUFFERING == 1)
    uint32_t curr_ms = to_ms_since_boot(get_absolute_time());
    if (curr_ms >= obj->update_ms + update_interval_ms) {
        obj->update_ms += update_interval_ms;
        update_screen(obj);
    }
#endif
}
