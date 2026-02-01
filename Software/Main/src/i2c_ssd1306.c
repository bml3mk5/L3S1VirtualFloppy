/** @file i2c_ssd1306.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "i2c_ssd1306.h"
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <hardware/irq.h>
#include <hardware/dma.h>
#include <pico/binary_info.h>
#include "i2c_master.h"
#include "l3font.h"
#include "common.h"
#include "main.h"

/* drive a 128x32 OLED panel */
// 0 : Horizontal addressing mode (not supported)
// 1 : Vertical addressing mode (not supported)
// 2 : Page addressing mode
#define ADDRESSING_MODE 2

// commands
enum en_ssd1306_controls {
    LCD_SETLOWERCOLUMN = 0x00,	// 0x00 - 0x0f
    LCD_SETHIGHERCOLUMN = 0x10,	// 0x10 - 0x1f
	LCD_SETMEMORYADDRESSING = 0x20,
	LCD_SETCOLUMNAREAADDRESS = 0x21,
	LCD_SETPAGEAREAADDRESS = 0x22,
	LCD_RIGHTHORIZONTALSCROLL = 0x26,
	LCD_LEFTHORIZONTALSCROLL = 0x27,
	LCD_VERTICALRIGHTHSCROLL = 0x29,
	LCD_VERTICALLEFTHSCROLL = 0x2a,
	LCD_DEACTIVATESCROLL = 0x2e,
	LCD_ACTIVATESCROLL = 0x2f,
	LCD_SETDISPLAYSTARTLINE = 0x40,	// 0x40 - 0x7f
	LCD_SETCONTRAST = 0x81,
	LCD_CHARGEPUMP = 0x8d,
	LCD_SETSEGMENTREMAP0 = 0xa0,
	LCD_SETSEGMENTREMAP1 = 0xa1,
	LCD_VERTICALSCROLLAREA = 0xa3,
	LCD_OUTPUTRAMTODISP = 0xa4,
	LCD_ENTIREDISPLAYON = 0xa5,
	LCD_SETNORMALDISPLAY = 0xa6,
	LCD_SETINVERSEDISPLAY = 0xa7,
	LCD_SETMULTIPLEXRATIO = 0xa8,
	LCD_SETDISPLAYOFF = 0xae,
	LCD_SETDISPLAYON  = 0xaf,
	LCD_SETPAGESTART = 0xb0,	// 0xb0 - 0xb7
	LCD_SCANDIR_NORMAL = 0xc0,
	LCD_SCANDIR_REVERSE = 0xc8,
	LCD_SETDISPLAYOFFSET = 0xd3,
	LCD_SETCLOCKDIVFREQ = 0xd5,
	LCD_SETPRECHARGEPERIOD = 0xd9,
	LCD_COMPINHWCONFIG = 0xda,
	LCD_SETVCOMHDESELECTLEVEL = 0xdb,
	LCD_NOOPERATION = 0xe3,
};

#ifdef USE_IST3613_LCD
enum en_ist3613_controls {
	LCD_POWER_CONTROL_OFF = 0x28,
	LCD_POWER_CONTROL_ON = 0x2f,

	LCD_REGULATION_RATIO = 0x90,  // 0x90 - 0x9f

	LCD_BIAS_SELECT_0 = 0xa8,
	LCD_BIAS_SELECT_1 = 0xa9,
	LCD_BIAS_SELECT_2 = 0xaa,
	LCD_BIAS_SELECT_3 = 0xab,

	LCD_SET_BOOSTER = 0xf8,  // 2bytes command

	LCD_ENTER_IST_TEST_MODE = 0x88,
	LCD_TM_ADJUST_FRAME_RATE_DISABLE = 0x20,
	LCD_TM_ADJUST_FRAME_RATE_ENABLE = 0x28,
	LCD_TM_BROWN_OUT_RESET_OFF = 0x70,
	LCD_TM_BOOSTER_FREQ_DIV = 0x93,
	LCD_TM_OSC_FREQ_DIV = 0x98,  // 0x98 - 0x9f
	LCD_TM_FRAME_CONTROL = 0xb2, // 3bytes command
	LCD_TM_EXIT_IST_TEST_MODE = 0xe3,
};
#endif

// By default these SSD1306 display drivers are on bus address 0x3c
static const int c_addr = 0x3c;

// Modes for lcd_send_byte
enum en_ssd1306_defs {
	LCD_CHARACTER  = 0x40,
	LCD_COMMAND    = 0x00,
};

#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
// updating screen ratio
static const uint32_t update_interval_ms = (100 / I2C_SSD1306_MAX_LINES); // 10fps
// display off time
static const uint32_t dispoff_interval_ms = (1 * 60 * 1000);
#endif

#ifdef USE_IST3613_LCD
#define DEV_TYPE_IST3613_MASK 0x04
#endif

//
static const struct {
    uint8_t scan_dir;
    uint8_t com_pins;
    uint8_t seg_remap;
    uint8_t start_line;
} device_types[] = {
    { LCD_SCANDIR_REVERSE, 0x12, LCD_SETSEGMENTREMAP1, 0x00 },  // 0.96inch OLED (128x64) Upper PINS
    { LCD_SCANDIR_NORMAL,  0x32, LCD_SETSEGMENTREMAP0, 0x00 },  // 0.96inch OLED (128x64) Lower PINS
    { LCD_SCANDIR_REVERSE, 0x22, LCD_SETSEGMENTREMAP1, 0x00 },  // 0.91inch OLED (128x32) Left PINS
    { LCD_SCANDIR_NORMAL,  0x02, LCD_SETSEGMENTREMAP0, 0x00 },  // 0.91inch OLED (128x32) Right PINS
#ifdef USE_IST3613_LCD
    { LCD_SCANDIR_REVERSE, 0x00, LCD_SETSEGMENTREMAP1, 0x32 },  // IST3613 LCD (132x52) Upper PINS
    { LCD_SCANDIR_NORMAL,  0x00, LCD_SETSEGMENTREMAP0, 0x00 },  // IST3613 LCD (132x52) Lower PINS
#endif
};

//--------------------------------------------------------------------+

static void i2c_ssd1306_send_byte_blocking(i2c_slave_t *obj, uint8_t ctrl, uint8_t data)
{
    i2c_master_send_byte_blocking(obj, TRANS_STATE_SEND, ctrl, data);
}

static void i2c_ssd1306_send_command(i2c_slave_t *obj, uint8_t data)
{
    i2c_master_send_byte(obj, TRANS_STATE_SEND, LCD_COMMAND, data);
}

static void i2c_ssd1306_send_command_blocking(i2c_slave_t *obj, uint8_t data)
{
    i2c_master_send_byte_blocking(obj, TRANS_STATE_SEND, LCD_COMMAND, data);
}

static void i2c_ssd1306_send_command_arg(i2c_slave_t *obj, uint8_t data, uint8_t arg)
{
    i2c_master_send_2bytes(obj, TRANS_STATE_SEND, LCD_COMMAND, data, arg);
}

static void i2c_ssd1306_send_command_arg_blocking(i2c_slave_t *obj, uint8_t data, uint8_t arg)
{
    i2c_master_send_2bytes_blocking(obj, TRANS_STATE_SEND, LCD_COMMAND, data, arg);
}

static void ssd1306_clear(i2c_ssd1306_t *obj)
{
    char str[16];
	const size_t len = 16;
	obj->pos_x = 0;
	obj->pos_y = 0;
    memset(str, 0, len);
	i2c_ssd1306_send_command_blocking(&obj->slave, obj->start_line);
	i2c_ssd1306_send_command_arg_blocking(&obj->slave, LCD_SETMEMORYADDRESSING, 0); // horizontal
	for(size_t pos=0; pos<128*8; pos+=len) {
    	i2c_master_send_string(&obj->slave, TRANS_STATE_SEND, LCD_CHARACTER, str, len);
	    i2c_master_wait_idle(obj->slave.master);
	}
	i2c_ssd1306_send_command_arg_blocking(&obj->slave, LCD_SETMEMORYADDRESSING, ADDRESSING_MODE);
	i2c_ssd1306_send_command_blocking(&obj->slave, obj->start_line);
}

#ifdef USE_IST3613_LCD
static void ist3613_clear(i2c_ssd1306_t *obj)
{
	i2c_ssd1306_locate(obj, 0, 0);
	for(size_t pos=0; pos<132*7; pos++) {
    	i2c_ssd1306_send_byte_blocking(&obj->slave, LCD_CHARACTER, 0);
	}
	i2c_ssd1306_locate(obj, 0, 0);
}
#endif

void i2c_ssd1306_clear(i2c_ssd1306_t *obj)
{
	i2c_slave_t *slave = &obj->slave;

#ifdef USE_IST3613_LCD
    if (slave->dev_type & DEV_TYPE_IST3613_MASK) {
        ist3613_clear(obj);
    } else
#endif
    {
        ssd1306_clear(obj);
    }
}

static void ssd1306_home(i2c_ssd1306_t *obj)
{
	obj->pos_x = 0;
	obj->pos_y = 0;
	i2c_ssd1306_send_command(&obj->slave, obj->start_line);
}

#ifdef USE_IST3613_LCD
static void ist3613_home(i2c_ssd1306_t *obj)
{
	i2c_ssd1306_locate(obj, 0, 0);
}
#endif

void i2c_ssd1306_home(i2c_ssd1306_t *obj)
{
	i2c_slave_t *slave = &obj->slave;

#ifdef USE_IST3613_LCD
    if (slave->dev_type & DEV_TYPE_IST3613_MASK) {
        ist3613_home(obj);
    } else
#endif
    {
        ssd1306_home(obj);
    }
}

// go to location on LCD
static void __not_in_flash_func(send_locate)(i2c_ssd1306_t *obj, int x, int y)
{
	x *= 8;
	y *= 2;
	obj->pos_x = x;
	obj->pos_y = y;
#if ADDRESSING_MODE == 2
	i2c_ssd1306_send_command(&obj->slave, obj->start_line);
	i2c_ssd1306_send_command(&obj->slave, (LCD_SETHIGHERCOLUMN | ((x >> 4) & 15)));
	i2c_ssd1306_send_command(&obj->slave, (LCD_SETLOWERCOLUMN | (x & 15)));
	i2c_ssd1306_send_command(&obj->slave, (LCD_SETPAGESTART | (y & 7)));
#endif
}
static void __not_in_flash_func(set_screen_locate)(i2c_ssd1306_t *obj, int x, int y)
{
    obj->screen_y = y;
    obj->screen_x = x;
}
void __not_in_flash_func(i2c_ssd1306_locate)(i2c_ssd1306_t *obj, int x, int y)
{
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
	set_screen_locate(obj, x, y);
#else
	send_locate(obj, x, y);
#endif
}

void i2c_ssd1306_shift(i2c_ssd1306_t *obj, bool shift, bool right)
{
    int val = 0; //LCD_ENTRYMODESET;
    if (shift) val |= 1; //LCD_ENTRYSHIFTINCREMENT;
    if (right) val |= 2; //LCD_ENTRYRIGHT;
    i2c_master_send_byte(&obj->slave, TRANS_STATE_SEND, LCD_COMMAND, (uint8_t)val);
}

static void __not_in_flash_func(send_substring)(i2c_ssd1306_t *obj, const char *s, size_t len)
{
	const uint8_t *d;
	for(size_t i=0; i<len; i++) {
		d = &l3font[(uint8_t)s[i]][0];
		i2c_master_send_string(&obj->slave, TRANS_STATE_SEND, LCD_CHARACTER, d, 8);
	}
#if ADDRESSING_MODE == 2
	i2c_ssd1306_send_command(&obj->slave, (LCD_SETHIGHERCOLUMN | (((obj->pos_x) >> 4) & 15)));
	i2c_ssd1306_send_command(&obj->slave, (LCD_SETLOWERCOLUMN | ((obj->pos_x) & 15)));
	i2c_ssd1306_send_command(&obj->slave, (LCD_SETPAGESTART | ((obj->pos_y + 1) & 7)));
#endif
	for(size_t i=0; i<len; i++) {
		d = &l3font[(uint8_t)s[i]][8];
		i2c_master_send_string(&obj->slave, TRANS_STATE_SEND, LCD_CHARACTER, d, 8);
	}
#if ADDRESSING_MODE == 2
	i2c_ssd1306_send_command(&obj->slave, (LCD_SETPAGESTART | ((obj->pos_y) & 7)));
#endif
}
static void __not_in_flash_func(set_screen_char)(i2c_ssd1306_t *obj, char c)
{
    obj->screen[obj->screen_y][obj->screen_x] = (0x8000 | (uint16_t)c);
    obj->screen_x++;
    if (obj->screen_x >= I2C_SSD1306_MAX_CHARS) {
        obj->screen_y++;
    }
    if (obj->screen_y >= I2C_SSD1306_MAX_LINES) {
        obj->screen_y = 0;
    }
}
static void __not_in_flash_func(set_screen_substring)(i2c_ssd1306_t *obj, const char *s, size_t len)
{
    for(size_t i=0; i<len; i++) {
        set_screen_char(obj, s[i]);
    }
}
void __not_in_flash_func(i2c_ssd1306_char)(i2c_ssd1306_t *obj, char c)
{
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
	set_screen_char(obj, c);
#else
	send_substring(obj, &c, 1);
#endif
}

void __not_in_flash_func(i2c_ssd1306_charset)(i2c_ssd1306_t *obj, char c, size_t len)
{
    char str[16];
	size_t slen = len < 16 ? len : 16;
    memset(str, c, slen);
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
	set_screen_substring(obj, str, slen);
#else
	send_substring(obj, str, slen);
#endif
}

void i2c_ssd1306_send_raw_data(i2c_slave_t *obj, const uint8_t *arr, size_t len)
{
	while((int)len > 0) {
		i2c_master_send_string(obj, TRANS_STATE_SEND, LCD_CHARACTER, arr, (int)len < 16 ? len : 16);
		arr += 16;
		len -= 16;
	}
}

void __not_in_flash_func(i2c_ssd1306_substring)(i2c_ssd1306_t *obj, const char *s, size_t len)
{
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
	set_screen_substring(obj, s, len);
#else
	send_substring(obj, s, len);
#endif
}

void i2c_ssd1306_substring_nowait(i2c_ssd1306_t *obj, const char *s, size_t len)
{
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
	set_screen_substring(obj, s, len);
#else
	if (!i2c_master_tx_buffer_is_not_full(obj->slave.master, len << 3)) return;
	send_substring(obj, s, len);
#endif
}

void __not_in_flash_func(i2c_ssd1306_string)(i2c_ssd1306_t *obj, const char *s)
{
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
	set_screen_substring(obj, s, strlen(s));
#else
	send_substring(obj, s, strlen(s));
#endif
}

void i2c_ssd1306_digit(i2c_ssd1306_t *obj, int val)
{
    char str[16];
    sprintf(str, "%d", val);
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
	set_screen_substring(obj, str, strlen(str));
#else
	send_substring(obj, str, strlen(str));
#endif
}

void i2c_ssd1306_draw_tile(i2c_ssd1306_t *obj, int x, int y, const uint8_t *arr, size_t len)
{
//  x = x_pos;    
	x *= 8;
//  x += x_offset;
	char cmds[3];
	cmds[0] = (LCD_SETHIGHERCOLUMN | (x >> 4));
	cmds[1] = (LCD_SETLOWERCOLUMN | (x & 15));
	cmds[2] = (LCD_SETPAGESTART | (y & 7));

	/* set line offset to 0 */
	i2c_ssd1306_send_command(&obj->slave, obj->start_line);
	i2c_master_send_string(&obj->slave, TRANS_STATE_SEND, LCD_COMMAND, cmds, 3);
	i2c_ssd1306_send_raw_data(&obj->slave, arr, len);
}
//--------------------------------------------------------------------+

static void ssd1306_init_screen(i2c_ssd1306_t *obj)
{
	i2c_slave_t *slave = &obj->slave;

    sleep_ms(200);
	/* display off */
	i2c_ssd1306_send_command_blocking(slave, LCD_SETDISPLAYOFF);
	/* clock divide ratio (0x00=1) and oscillator frequency (0x8) */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETCLOCKDIVFREQ, 0x80);
	/* multiplex ratio */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETMULTIPLEXRATIO, 0x3f);
	/* display offset */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETDISPLAYOFFSET, 0x00);
	/* set display start line to 0 */
    obj->start_line = LCD_SETDISPLAYSTARTLINE | device_types[slave->dev_type].start_line;
	i2c_ssd1306_send_command_blocking(slave, obj->start_line);
	/* [2] charge pump setting (p62): 0x014 enable, 0x010 disable, SSD1306 only, should be removed for SH1106 */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_CHARGEPUMP, 0x14);
	/* addressing mode */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETMEMORYADDRESSING, ADDRESSING_MODE);
  
	/* segment remap a0/a1*/
	i2c_ssd1306_send_command_blocking(slave, device_types[slave->dev_type].seg_remap);
	/* c0: scan dir normal, c8: reverse */
	i2c_ssd1306_send_command_blocking(slave, device_types[slave->dev_type].scan_dir);
  
	/* com pin HW config, sequential com pin config (bit 4), disable left/right remap (bit 5) */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_COMPINHWCONFIG, device_types[slave->dev_type].com_pins);

	/* [2] set contrast control */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETCONTRAST, 0xcf);
	/* [2] pre-charge period 0x022/f1*/
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETPRECHARGEPERIOD, 0xf1);
	/* vcomh deselect level */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETVCOMHDESELECTLEVEL, 0x40);
	// if vcomh is 0, then this will give the biggest range for contrast control issue #98
	// restored the old values for the noname constructor, because vcomh=0 will not work for all OLEDs, #116

	/* Deactivate scroll */ 
	i2c_ssd1306_send_command_blocking(slave, LCD_DEACTIVATESCROLL);
	/* output ram to display */
	i2c_ssd1306_send_command_blocking(slave, LCD_OUTPUTRAMTODISP);
	/* none inverted normal display mode */
	i2c_ssd1306_send_command_blocking(slave, LCD_SETNORMALDISPLAY);

	/* clear screen */
	i2c_ssd1306_clear(obj);
	/* display on */
	i2c_ssd1306_send_command_blocking(slave, LCD_SETDISPLAYON);

    sleep_ms(1);
}

#ifdef USE_IST3613_LCD
static void ist3613_init_screen(i2c_ssd1306_t *obj)
{
	i2c_slave_t *slave = &obj->slave;

    sleep_ms(200);
	/* display off */
	i2c_ssd1306_send_command_blocking(slave, LCD_SETDISPLAYOFF);
#if 0
	/* set display start line to 0 */
	i2c_ssd1306_send_command_blocking(slave, LCD_SETDISPLAYSTARTLINE | 0x00);
	/* c0: scan dir normal */
	i2c_ssd1306_send_command_blocking(slave, LCD_SCANDIR_NORMAL);
	/* segment remap a0 (normal) */
	i2c_ssd1306_send_command_blocking(slave, LCD_SETSEGMENTREMAP0);
#else
	/* set display start line to 0 */
    obj->start_line = LCD_SETDISPLAYSTARTLINE | device_types[slave->dev_type].start_line;
	i2c_ssd1306_send_command_blocking(slave, obj->start_line);
	/* c8: scan dir reverse */
	i2c_ssd1306_send_command_blocking(slave, device_types[slave->dev_type].scan_dir);
	/* segment remap a1 (reverse) */
	i2c_ssd1306_send_command_blocking(slave, device_types[slave->dev_type].seg_remap);
#endif
	/* none inverted normal display mode */
	i2c_ssd1306_send_command_blocking(slave, LCD_SETNORMALDISPLAY);
	/* boost on dc-dc converter */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SET_BOOSTER, 0x03);
	/* regration ration on regrator */
    i2c_ssd1306_send_command_blocking(slave, LCD_REGULATION_RATIO | 0xa);  // 7.0
	/* set contrast control */
	i2c_ssd1306_send_command_arg_blocking(slave, LCD_SETCONTRAST, 0x19);
	/* bias select */
	i2c_ssd1306_send_command_blocking(slave, LCD_BIAS_SELECT_3);
	/* power control all on */
	i2c_ssd1306_send_command_blocking(slave, LCD_POWER_CONTROL_ON);

    sleep_ms(20);

	/* enter test mode */
	i2c_ssd1306_send_command_blocking(slave, LCD_ENTER_IST_TEST_MODE);
	i2c_ssd1306_send_command_blocking(slave, LCD_ENTER_IST_TEST_MODE);
	i2c_ssd1306_send_command_blocking(slave, LCD_ENTER_IST_TEST_MODE);
	i2c_ssd1306_send_command_blocking(slave, LCD_ENTER_IST_TEST_MODE);

    i2c_ssd1306_send_command_blocking(slave, LCD_TM_OSC_FREQ_DIV | 1);
    i2c_ssd1306_send_command_blocking(slave, LCD_TM_ADJUST_FRAME_RATE_ENABLE);
    i2c_ssd1306_send_command_blocking(slave, LCD_TM_FRAME_CONTROL);
    i2c_ssd1306_send_command_blocking(slave, 0x49);
    i2c_ssd1306_send_command_blocking(slave, 0x01);
    i2c_ssd1306_send_command_blocking(slave, LCD_TM_OSC_FREQ_DIV | 1);
    i2c_ssd1306_send_command_blocking(slave, LCD_TM_BOOSTER_FREQ_DIV);
    i2c_ssd1306_send_command_blocking(slave, LCD_TM_BROWN_OUT_RESET_OFF);

    i2c_ssd1306_send_command_blocking(slave, LCD_TM_EXIT_IST_TEST_MODE);

    sleep_ms(50);

	/* clear screen */
	i2c_ssd1306_clear(obj);
	/* display on */
    i2c_ssd1306_send_command_blocking(slave, LCD_SETDISPLAYON);

    sleep_ms(1);
}
#endif

static void display_on(i2c_ssd1306_t *obj)
{
    i2c_slave_t *slave = &obj->slave;
    /* display on */
    i2c_ssd1306_send_command(slave, LCD_SETDISPLAYON);
}

static void display_off(i2c_ssd1306_t *obj)
{
    i2c_slave_t *slave = &obj->slave;
    /* display off */
    i2c_ssd1306_send_command(slave, LCD_SETDISPLAYOFF);
}

void i2c_ssd1306_init_screen(i2c_ssd1306_t *obj)
{
	i2c_slave_t *slave = &obj->slave;

    if (!slave->baud) return;

#ifdef USE_IST3613_LCD
    if (slave->dev_type & DEV_TYPE_IST3613_MASK) {
        ist3613_init_screen(obj);
    } else
#endif
    {
        ssd1306_init_screen(obj);
    }
}

void i2c_ssd1306_init(i2c_master_t *master, i2c_ssd1306_t *obj, int address, uint32_t start_ms)
{
    i2c_slave_init(master, &obj->slave, address == 0 ? c_addr : address, 2, 1);
	obj->pos_x = 0;
	obj->pos_y = 0;
    obj->start_line = 0;
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
    memset(obj->screen, 0, sizeof(obj->screen));
    obj->screen_x = 0;
    obj->screen_y = 0;
    obj->curr_y = 0;
    obj->update_ms = start_ms;
    obj->dispoff_ms = dispoff_interval_ms;
#endif
}

static void update_screen_parts(i2c_ssd1306_t *obj, char *str, int len, int y, int stx)
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

static void update_screen(i2c_ssd1306_t *obj)
{
    char str[I2C_SSD1306_MAX_CHARS];
    int len;
    int stx;

    int y=obj->curr_y;
    obj->curr_y++;
    if (obj->curr_y >= I2C_SSD1306_MAX_LINES) {
        obj->curr_y = 0;
    }
    bool upd = false;
    uint16_t *p = obj->screen[y];
    len = 0;
    stx = 0xffff;
    for(int x=0; x<I2C_SSD1306_MAX_CHARS; x++) {
        if (*p & 0x8000) {
            // need update
            if (stx > x) {
                stx = x;
            }
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

    i2c_ssd1306_display_onoff(obj, upd);
}

void i2c_ssd1306_request_update(i2c_ssd1306_t *obj)
{
    for(int y=0; y<I2C_SSD1306_MAX_LINES; y++) {
        uint16_t *p = obj->screen[y];
        for(int x=0; x<I2C_SSD1306_MAX_CHARS; x++) {
            *p |= 0x8000;
            p++;
        }
    }
}

void i2c_ssd1306_display_onoff(i2c_ssd1306_t *obj, bool on)
{
//    uint32_t curr_ms = to_ms_since_boot(get_absolute_time());
    if (on) {
        if (obj->dispoff_ms == 0) {
            // display on
            display_on(obj);
        }
        obj->dispoff_ms = g_c0_current_time_ms + dispoff_interval_ms;
    } else {
        if (obj->dispoff_ms != 0 && obj->dispoff_ms < g_c0_current_time_ms) {
            // display off
            display_off(obj);
            obj->dispoff_ms = 0;
        }
    }
}

void i2c_ssd1306_task(i2c_ssd1306_t *obj)
{
#if (I2C_SSD1306_DOUBLE_BUFFERING == 1)
//   uint32_t curr_ms = to_ms_since_boot(get_absolute_time());
    if (g_c0_current_time_ms >= obj->update_ms + update_interval_ms) {
        obj->update_ms += update_interval_ms;
        update_screen(obj);
    }
#endif
}
