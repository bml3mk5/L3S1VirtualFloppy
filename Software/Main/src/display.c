/**
 * @file display.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-18
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "display.h"
#include <stdio.h>
#include <string.h>
#include <hardware/gpio.h>
#include <pico/binary_info.h>
#include <tusb.h>
#include "common.h"
#include "i2c_master.h"
#include "display_btn.h"
#include "display_storage.h"
#include "display_setting.h"
#include "msc_app.h"
#include "disk_drive.h"
#include "disk_d88.h"
#include "shell_cmd.h"
//#include "event.h"
#include "config.h"
#include "utils.h"
#include <ff.h>

//--------------------------------------------------------------------
// instance

i2c_master_t     i2c_master;
i2c_lcd_1602_t   i2c_lcd;
i2c_ssd1306_t    i2c_oled;
i2c_led_btn_t    i2c_led_btn;

display_info_t   display_info;

//--------------------------------------------------------------------

static void display_lcd_init(void);
static void display_lcd_task(void);

static void display_lcd_disp_file_and_d88_mount(simple_list_data_t *data);

static void display_led_init(void);
//static void display_led_task(void);

static void lcd_disk_init(void);

static i2c_slave_t *slave_list[] = {
    &i2c_lcd.slave,
    &i2c_oled.slave,
    &i2c_led_btn.slave,
    NULL
};

//--------------------------------------------------------------------

void display_init(void)
{
    // i2c master
    i2c_master_init(&i2c_master, PICO_DEFAULT_I2C_SCL_PIN, PICO_DEFAULT_I2C_SDA_PIN, i2c0);
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SCL_PIN, PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C));
    // init lcd instance
    i2c_lcd_1602_init(&i2c_master, &i2c_lcd, 0 /* default */, 10);
    // init oled instance
    i2c_ssd1306_init(&i2c_master, &i2c_oled, 0 /* default */, 20);
    // init button instance
    i2c_led_btn_init(&i2c_master, &i2c_led_btn, 0 /* default */, 0);

#if 0
    i2c_lcd.slave.gpio_num = 6;
    i2c_oled.slave.gpio_num = 7;
    i2c_led_btn.slave.gpio_num = 8;
    for(int i=0; slave_list[i]; i++) {
        gpio_init(slave_list[i]->gpio_num);
        gpio_set_dir(slave_list[i]->gpio_num, true);
        gpio_put(slave_list[i]->gpio_num, false);
    }
#endif

    // check devices
    printf("I2C Check Devices ...\n");
    i2c_master_check_device_flexible(&i2c_master, slave_list);

    // init lcd screen
    i2c_lcd_1602_init_screen(&i2c_lcd);
    // init oled screen
    i2c_slave_set_device_type(&i2c_oled.slave, config_get_i2c_ssd1306_type());
    i2c_ssd1306_init_screen(&i2c_oled);

    display_info.phase = PHASE_STORAGE;

    display_led_init();
    display_btn_init();
    display_lcd_init();
 //    display_storage_init();
    lcd_disk_init();

    i2c_master_wait_idle(&i2c_master);
}

void __no_inline_not_in_flash_func(display_task)(void)
{
//    display_led_task();
    display_btn_task();
    display_lcd_task();
    i2c_lcd_1602_task(&i2c_lcd);
    i2c_ssd1306_task(&i2c_oled);
    i2c_led_btn_task(&i2c_led_btn);
}

//--------------------------------------------------------------------

void display_reset_i2c_display(void)
{
    // init lcd screen
    i2c_lcd_1602_init_screen(&i2c_lcd);
    i2c_lcd_1602_request_update(&i2c_lcd);
    // init oled screen
    i2c_slave_set_device_type(&i2c_oled.slave, config_get_i2c_ssd1306_type());
    i2c_ssd1306_init_screen(&i2c_oled);
    i2c_ssd1306_request_update(&i2c_oled);
}

//--------------------------------------------------------------------

int16_t display_change_choice(int dir, int16_t pos, size_t count)
{
    if (dir > 0) {
        pos++;
        if (pos >= count) pos = 0;
    } else if (dir < 0) {
        pos--;
        if (pos < 0) pos = count - 1;
    }
    return pos;
}

//--------------------------------------------------------------------

/// @brief Initialize scrolling a text on the I2C LCD
/// @param text 
void text_shift_init(text_shift_t *text)
{
    text->phase = 0;
    text->pos = 0;
    text->pos_max = 0;
    text->ms = 0;
    text->text[0] = 0;
}

/// @brief Set scrolling a text on the I2C LCD
/// @param text 
void text_shift_set(text_shift_t *text, const char *n_str, size_t n_len)
{
    text->pos = 0;
    text->pos_max = 0;
    text->ms = 0;
    memcpy(text->text, n_str, n_len < FILE_TITLE_SIZE ? n_len : FILE_TITLE_SIZE);

    text->phase = (n_len > 16 ? 1 : 0);

    if (text->phase) {
        text->ms = to_ms_since_boot(get_absolute_time());
        text->pos_max = n_len - 16 + 1;
    }
}

/// @brief Scroll the long text on the I2C LCD
/// @param text : text structure
void text_shift_task(text_shift_t *text)
{
    const uint32_t interval_ms = 500;

    if (text->phase == 0) {
        return;
    }

    // Blink every interval ms
    uint32_t curr_ms = to_ms_since_boot(get_absolute_time());
    if (curr_ms - text->ms < interval_ms) return; // not enough time
    text->ms += interval_ms;

    switch(text->phase) {
    case 1:
        text->pos = 0;
        text->phase++;
        break;
    case 2:
    case 4:
    case 6:
        text->pos++;
        if (text->pos >= text->pos_max) {
            text->phase++;
        } else {
            lcd_locate_substring(2, 0, &text->text[text->pos], 14);
        }
        break;
    case 3:
    case 5:
    case 7:
        text->pos--;
        if (text->pos < 0) {
            text->phase++;
        } else {
            lcd_locate_substring(2, 0, &text->text[text->pos], 14);
        }
        break;
    default:
        break;
    }
}

//--------------------------------------------------------------------

/// @brief Make an absolute file path (Slash separated) 
/// @param d88_drv : drive number
/// @param file_name : path
/// @param tree : directory tree
/// @note file path is stored in d88_config.file_path
void display_config_make_path(int d88_drv, const char *file_name, simple_list_t *tree)
{
    // make absolute path
    char *path = config_get_path_ptr(d88_drv);
    uint32_t size = (uint32_t)config_get_path_size();
    uint32_t pos = 0;
    uint32_t len;
    simple_list_item_t *item = tree->item_list;
    while(item) {
        len = (uint32_t)strlen(item->data.name);
        if (pos + len + 1 >= size) {
            break;
        }
        path[pos] = '/';
        pos++;
        strcpy(&path[pos], item->data.name);
        pos += len;
        item = item->next;
    }
    len = (uint32_t)strlen(file_name);
    if (pos + len + 1 < size) {
        path[pos] = '/';
        pos++;
        strcpy(&path[pos], file_name);
        config_save();
    }
//    printf("%s\n", d88_config.file_path);
}

/// @brief 
/// @param d88_drv 
/// @param file_path
/// @param side_number
void display_config_set_path(int d88_drv, const char *file_path, uint8_t side_number)
{
    config_set_path(d88_drv, file_path);
    config_set_side_number(d88_drv, side_number);
    config_save();
}

/// @brief 
/// @param d88_drv 
void display_config_clear_path(int d88_drv)
{
    config_set_path(d88_drv, "");
    config_save();
}

/// @brief 
/// @param d88_drv 
/// @param side_number
void display_config_set_side_number(int d88_drv, uint8_t side_number)
{
    config_set_side_number(d88_drv, side_number);
    config_save();
}

#if 0
/// @brief Trace sub directories from the file path
/// @param file_path : path such as "/foo/bar/baz/file.txt" (slash separator)
/// @param list : file list in current directory
/// @return Position the file in the directory / -1 means file not found
int display_file_path_trace(const char *file_path, simple_list_t *list)
{
    int index = -1;
    const char *s = file_path;
    const char *p;

//    printf("Trace: %s\n", file_path);

    while(1) {
        p = strchr(s, '/');
        if (!p) {
            // file
            simple_list_data_t *data = simple_list_get_data_by_name(list, s);
            if (data) {
                // file found
//            display_lcd_disp_file_and_d88_mount(data);
                index = data->index;
            }
            break;
        } else {
            // directory
            if (s < p) {
                // goto sub directory
                simple_list_data_t *data = simple_list_get_data_by_subname(list, s, p - s);
                if (!data) {
                    // directory not found
                    break;
                }
                display_directory_change(data);
            }
            s = p + 1;
        }
    }
    return index;
}
#endif

//--------------------------------------------------------------------

/// @brief Initialize for I2C LCD
void display_lcd_init(void)
{
    display_storage_init();
    display_setting_init();
    display_reset_init();
}

void display_lcd_change_phase(void)
{
    switch(display_info.phase) {
    case PHASE_SETTING:
        display_setting_change_phase();
        break;
    case PHASE_RESET:
        display_reset_change_phase();
        break;
    default:
        display_filelist_change_phase();
        break;
    }
}

void __no_inline_not_in_flash_func(display_lcd_task)(void)
{
    switch(display_info.phase) {
    case PHASE_SETTING:
        display_setting_task();
        break;
    case PHASE_RESET:
        display_reset_task();
        break;
    default:
        display_storage_task();
        break;
    }
}

//--------------------------------------------------------------------

/// @brief Wrapper function to set the cursor position on I2C LCD
/// @param x : x axis (0 - 39)
/// @param y : y axis (0 - 1)
void lcd_locate(int x, int y)
{
    i2c_lcd_1602_locate(&i2c_lcd, x, y);
    i2c_ssd1306_locate(&i2c_oled, x, y);
}

/// @brief Wrapper function to show the charactor on I2C LCD
/// @param c : a charactor
void lcd_char(char c)
{
    i2c_lcd_1602_char(&i2c_lcd, c);
    i2c_ssd1306_char(&i2c_oled, c);
}

/// @brief Wrapper function to show the charactor repeatly on I2C LCD
/// @param c : a charactor
/// @param len : string length
void lcd_charset(char c, size_t len)
{
    i2c_lcd_1602_charset(&i2c_lcd, c, len);
    i2c_ssd1306_charset(&i2c_oled, c, len);
}

/// @brief Wrapper function to show the string on I2C LCD
/// @param s : string to show
void lcd_string(const char *s)
{
    i2c_lcd_1602_string(&i2c_lcd, s);
    i2c_ssd1306_string(&i2c_oled, s);
}

/// @brief Wrapper function to show the charactor on I2C LCD
/// @param x : x axis (0 - 39)
/// @param y : y axis (0 - 1)
/// @param c : a charactor
void lcd_locate_char(int x, int y, char c)
{
    i2c_lcd_1602_locate(&i2c_lcd, x, y);
    i2c_lcd_1602_char(&i2c_lcd, c);
    i2c_ssd1306_locate(&i2c_oled, x, y);
    i2c_ssd1306_char(&i2c_oled, c);
}

/// @brief Wrapper function to show the charactor repeatly on I2C LCD
/// @param x : x axis (0 - 39)
/// @param y : y axis (0 - 1)
/// @param c : a charactor
/// @param len : string length
void lcd_locate_charset(int x, int y, char c, size_t len)
{
    i2c_lcd_1602_locate(&i2c_lcd, x, y);
    i2c_lcd_1602_charset(&i2c_lcd, c, len);
    i2c_ssd1306_locate(&i2c_oled, x, y);
    i2c_ssd1306_charset(&i2c_oled, c, len);
}

/// @brief Wrapper function to show the string on I2C LCD
/// @param x : x axis (0 - 39)
/// @param y : y axis (0 - 1)
/// @param s : string to show
void lcd_locate_string(int x, int y, const char *s)
{
    i2c_lcd_1602_locate(&i2c_lcd, x, y);
    i2c_lcd_1602_string(&i2c_lcd, s);
    i2c_ssd1306_locate(&i2c_oled, x, y);
    i2c_ssd1306_string(&i2c_oled, s);
}

/// @brief Wrapper function to show the string on I2C LCD
/// @param x : x axis (0 - 39)
/// @param y : y axis (0 - 1)
/// @param s : string to show
/// @param len : string length
void lcd_locate_substring(int x, int y, const char *s, size_t len)
{
    i2c_lcd_1602_locate(&i2c_lcd, x, y);
    i2c_lcd_1602_substring(&i2c_lcd, s, len);
    i2c_ssd1306_locate(&i2c_oled, x, y);
    i2c_ssd1306_substring(&i2c_oled, s, len);
}

/// @brief Wrapper function to show the digit data on I2C LCD
/// @param val : integer value
void lcd_digit(int val)
{
    i2c_lcd_1602_digit(&i2c_lcd, val);
    i2c_ssd1306_digit(&i2c_oled, val);
}

//--------------------------------------------------------------------

static uint8_t led_motor;
#ifdef USE_LCD_DISK_PARAMS
struct st_lcd_disk {
    int trk;
    int sid;
    int sec;
} lcd_disk[MAX_DRIVES];
#endif

/// @brief Initialize data to display the track, side and sector number on I2C LCD
/// @param  
void lcd_disk_init(void)
{
    led_motor = 0;
#ifdef USE_LCD_DISK_PARAMS
    for(int i=0; i<MAX_DRIVES; i++) {
        lcd_disk[i].trk = -1;
        lcd_disk[i].sid = -1;
        lcd_disk[i].sec = -1;
    }
#endif
}

/// @brief Turn on/off the LED lamp on the I2C device
/// (Simulate access lamp on a FDD)
/// @param drv : drive number 
/// @param onoff : 0:OFF !=0:ON
void lcd_disk_motor(int drv, int onoff)
{
    uint8_t new_motor;
    if (drv >= 0) {
        new_motor = (onoff ? (1 << (7-drv)) : 0);
    } else {
        new_motor = (onoff ? ~((1 << (8-MAX_DRIVES)) - 1) : 0);
    }
    if (led_motor != new_motor) {
        led_motor = new_motor;
        i2c_led_btn_set_led(&i2c_led_btn, led_motor);
    }
}

/// @brief Show the drive number on the I2C LCD
/// @param drv : drive number 
void lcd_disk_drv_number(int drv)
{
    char str[4];
    str[0] = (drv & 3) + '0';
    str[1] = ':';
    str[2] = 0;
    i2c_lcd_1602_locate(&i2c_lcd, 0, 0);
    i2c_lcd_1602_substring(&i2c_lcd, str, 2);
    i2c_ssd1306_locate(&i2c_oled, 0, 0);
    i2c_ssd1306_substring(&i2c_oled, str, 2);
}

/// @brief Show the track, side and sector number on the I2C LCD
/// @param drv : drive number 
/// @param trk : track number
/// @param sid : side number
/// @param sec : sector number
void __not_in_flash_func(lcd_disk_trk_sid_sec_number)(int drv, int trk, int sid, int sec)
{
    char str[8];
    if (drv == display_d88_get_current_drive()) {
        dec_str_2(trk, &str[0]);
        str[2] = ':';
        str[3] = (sid & 1) + '0';
        str[4] = ':';
        dec_str_2(sec, &str[5]);
        str[7] = 0;
        i2c_lcd_1602_locate(&i2c_lcd, 2, 1);
        i2c_lcd_1602_substring(&i2c_lcd, str, 7);
        i2c_ssd1306_locate(&i2c_oled, 2, 1);
        i2c_ssd1306_substring(&i2c_oled, str, 7);
    }
#ifdef USE_LCD_DISK_PARAMS
    lcd_disk[drv].trk = trk;
    lcd_disk[drv].sid = sid;
    lcd_disk[drv].sec = sec;
#endif
}

/// @brief Show the track, side and sector number on the I2C LCD
/// @param drv : drive number 
/// @param trk : track number
/// @param sid : side number
void lcd_disk_trk_sid_number(int drv, int trk, int sid)
{
    char str[8];
    if (drv == display_d88_get_current_drive()) {
        dec_str_2(trk, &str[0]);
        str[2] = ':';
        str[3] = (sid & 1) + '0';
        str[4] = 0;
        i2c_lcd_1602_locate(&i2c_lcd, 2, 1);
        i2c_lcd_1602_substring(&i2c_lcd, str, 4);
        i2c_ssd1306_locate(&i2c_oled, 2, 1);
        i2c_ssd1306_substring(&i2c_oled, str, 4);
    }
#ifdef USE_LCD_DISK_PARAMS
    lcd_disk[drv].trk = trk;
    lcd_disk[drv].sid = sid;
#endif
}

/// @brief Show the side and sector number on the I2C LCD
/// @param drv : drive number 
/// @param sid : side number
/// @param sec : sector number
void lcd_disk_sid_sec_number(int drv, int sid, int sec)
{
    char str[8];
    if (drv == display_d88_get_current_drive()) {
        str[0] = (sid & 1) + '0';
        str[1] = ':';
        dec_str_2(sec, &str[2]);
        str[4] = 0;
        i2c_lcd_1602_locate(&i2c_lcd, 5, 1);
        i2c_lcd_1602_substring(&i2c_lcd, str, 4);
        i2c_ssd1306_locate(&i2c_oled, 5, 1);
        i2c_ssd1306_substring(&i2c_oled, str, 4);
    }
#ifdef USE_LCD_DISK_PARAMS
    lcd_disk[drv].sid = sid;
    lcd_disk[drv].sec = sec;
#endif
}

/// @brief Show the side number on the I2C LCD
/// @param drv : drive number 
/// @param sid : side number
void lcd_disk_sid_number(int drv, int sid)
{
    char str[4];
    if (drv == display_d88_get_current_drive()) {
        str[0] = (sid & 1) + '0';
        str[1] = 0;
        i2c_lcd_1602_locate(&i2c_lcd, 5, 1);
        i2c_lcd_1602_substring(&i2c_lcd, str, 1);
        i2c_ssd1306_locate(&i2c_oled, 5, 1);
        i2c_ssd1306_substring(&i2c_oled, str, 1);
    }
#ifdef USE_LCD_DISK_PARAMS
    lcd_disk[drv].sid = sid;
#endif
}

/// @brief Show the sector number on the I2C LCD
/// @param drv : drive number 
/// @param sec : sector number
void lcd_disk_sec_number(int drv, int sec)
{
    char str[4];
    if (drv == display_d88_get_current_drive()) {
        dec_str_2(sec, &str[0]);
        str[2] = 0;
        i2c_lcd_1602_locate(&i2c_lcd, 7, 1);
        i2c_lcd_1602_substring_nowait(&i2c_lcd, str, 2);
        i2c_ssd1306_locate(&i2c_oled, 7, 1);
        i2c_ssd1306_substring_nowait(&i2c_oled, str, 2);
    }
#ifdef USE_LCD_DISK_PARAMS
    lcd_disk[drv].sec = sec;
#endif
}

//--------------------------------------------------------------------

/// @brief 
/// @param drv 
/// @param tracks_per_side 
/// @param sides_per_disk 
/// @param valid 
/// @param protect 
void __not_in_flash_func(lcd_d88_status)(int drv, int tracks_per_side, int sides_per_disk, bool valid, bool protect)
{
    char str[8];
    if (drv == display_d88_get_current_drive()) {
//        sprintf(str, " %02d:%d  ", tracks_per_side, sides_per_disk);
        str[0] = ' ';
        dec_str_2(tracks_per_side, &str[1]);
        str[3] = ':';
        str[4] = (sides_per_disk & 3) + '0';
        str[5] = ' ';
        if (valid) {
            if (!protect) {
                str[6] = 'N';
            } else {
                str[6] = 'P';
            }
        } else {
            str[6] = 'E';
        }
        lcd_locate_string(9, 1, str);
    }
}

//--------------------------------------------------------------------

const uint8_t led_flag[4] = { 0, 0x80, 0, 0x40 };
int led_state;

void display_led_init(void)
{
    led_state = 0;
}

/// @brief Modify status of the LED lamp on the I2C device
/// @param state : 0:OFF 1:ON
void __no_inline_not_in_flash_func(display_led_state)(int state)
{
    led_state = (state & 3);
    i2c_led_btn_set_led(&i2c_led_btn, led_flag[led_state] | led_motor);
}

/// @brief Sound the buzzer on the I2C device  
void display_buzzer(void)
{
    i2c_led_btn_set_led(&i2c_led_btn, led_flag[led_state] | led_motor | 0x20);
}

