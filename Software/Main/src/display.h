/**
 * @file display.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-18
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "i2c_lcd_1602.h"
#include "i2c_ssd1306.h"
#include "i2c_led_btn.h"
#include "simple_list.h"

enum en_display_phase {
    PHASE_STORAGE = 0,
    PHASE_SETTING,
    PHASE_RESET,
};

typedef struct st_display_info {
    int phase;
} display_info_t;

typedef struct st_display_pos {
    int16_t c;
    int16_t n;
} display_pos_t;

extern i2c_lcd_1602_t   i2c_lcd;
extern i2c_ssd1306_t    i2c_oled;
extern i2c_led_btn_t    i2c_led_btn;

extern display_info_t   display_info;

void display_init(void);
void display_task(void);

void display_reset_i2c_display(void);

int16_t display_change_choice(int dir, int16_t pos, size_t count);

#define FILE_TITLE_SIZE 40

typedef struct st_text_shift {
    int phase;
    int pos;
    int pos_max;
    uint32_t ms;
    char text[FILE_TITLE_SIZE + 1];
} text_shift_t;

void text_shift_init(text_shift_t *text);
void text_shift_set(text_shift_t *text, const char *n_str, size_t n_len);
void text_shift_task(text_shift_t *text);

void display_config_make_path(int d88_drv, const char *file_name, simple_list_t *tree);
void display_config_set_path(int d88_drv, const char *file_path, uint8_t side_number);
void display_config_clear_path(int d88_drv);
void display_config_set_side_number(int d88_drv, uint8_t side_number);

void display_led_state(int state);
void display_buzzer(void);

void display_lcd_change_phase(void);

// wrapper of i2c_lcd_1602

void lcd_locate(int x, int y);
void lcd_char(char c);
void lcd_charset(char c, size_t len);
void lcd_string(const char *s);
void lcd_locate_char(int x, int y, char c);
void lcd_locate_charset(int x, int y, char c, size_t len);
void lcd_locate_string(int x, int y, const char *s);
void lcd_locate_substring(int x, int y, const char *s, size_t len);
void lcd_digit(int val);

void lcd_disk_motor(int drv, int onoff);
void lcd_disk_drv_number(int drv);
void lcd_disk_trk_sid_sec_number(int drv, int trk, int sid, int sec);
void lcd_disk_trk_sid_number(int drv, int trk, int sid);
void lcd_disk_sid_sec_number(int drv, int sid, int sec);
void lcd_disk_sid_number(int drv, int sid);
void lcd_disk_sec_number(int drv, int sec);

void lcd_d88_status(int drv, int tracks_per_side, int sides_per_disk, bool valid, bool protect);

#endif /* DISPLAY_H */
