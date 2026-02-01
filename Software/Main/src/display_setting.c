/**
 * @file display_setting.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-23
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "display_setting.h"
#include <stdint.h>
#include <string.h>
#include "common.h"
#include "display.h"
#include "display_storage.h"
//#include "display_menu.h"
#include "display_message.h"
#include "config.h"
//#include "event.h"

enum en_setting_lists {
    SETTING_DISK_TYPE = 0,
    SETTING_SEEK_TRACK,
    SETTING_SEARCH_SECTOR,
    SETTING_DATA_REQUSET,
    SETTING_I2C_SSD1306,
    SETTING_RESET_I2C_DISP,
    SETTING_SAVE,
    SETTING_EXIT,
    SETTING_LIST_MAX
};

const char *setting_list_disk_type[] = {
    "5inch 2D      ",
    "\0", // "2D ($FF10)",
#ifdef _MBS1
    "5inch 2HD     ",
#else
    "8inch 2D      ",
#endif
    "3inch 1S      ",
    NULL
};

static const char *setting_list_calc_time[] = {
    "Calculate time",
    "Certain time  ",
    NULL
};

static const char *setting_list_data_reqtime[] = {
    "Calculate time",
    "As fast as    ",
    NULL
};

static const char *setting_list_i2c_ssd1306[] = {
    "OLED 128x64 D ",
    "OLED 128x64 U ",
    "OLED 128x32 R ",
    "OLED 128x32 L ",
#ifdef USE_IST3613_LCD
    "LCD IST3613 D ",
    "LCD IST3613 U ",
#else
    "\0",
    "\0",
#endif
    NULL
};

typedef struct st_setting_lists {
    const char *title;
    const char **list;
    int16_t      lcount;
    display_pos_t lpos;
} setting_lists_t;

static setting_lists_t setting_lists[] = {
    // 0123456789012345
    { "Disk type:", setting_list_disk_type, 4, {0, 0} },
    { "Seek track:", setting_list_calc_time, 2, {0, 0} },
    { "Search sector:", setting_list_calc_time, 2, {0, 0} },
    { "Data request:", setting_list_data_reqtime, 2, {0, 0} },
    { "Display type 1:", setting_list_i2c_ssd1306, 6, {0, 0} },
    { "Reset display", NULL, 0, {0, 0} },
    { "Save and Exit", NULL, 0, {0, 0} },
    { "Exit", NULL, 0, {0, 0} },
    { NULL, NULL, 0, {0, 0} }
};

static struct st_setting_info {
    display_pos_t pos;
    int16_t row;
    int prev_phase;
} setting_info;

void display_setting_init(void)
{
    setting_info.pos.c = -1;
    setting_info.pos.n = 0;
    setting_info.row = 0;
    setting_info.prev_phase = -1;
}

void display_setting_change_phase(void)
{
    if (display_info.phase != PHASE_SETTING) {
        setting_info.prev_phase = display_info.phase;
    }
    display_info.phase = PHASE_SETTING;
    setting_info.pos.c = -1;
    setting_info.pos.n = 0;
    setting_info.row = 0;

    setting_lists[SETTING_DISK_TYPE].lpos.c = config_get_disk_type();
    setting_lists[SETTING_SEEK_TRACK].lpos.c = config_get_seek_track();
    setting_lists[SETTING_SEARCH_SECTOR].lpos.c = config_get_search_sector();
    setting_lists[SETTING_DATA_REQUSET].lpos.c = config_get_data_request();
    setting_lists[SETTING_I2C_SSD1306].lpos.c = config_get_i2c_ssd1306_type();
}

void display_setting_move(int dir)
{
    setting_lists_t *item;

    switch(setting_info.row) {
    case 1:
        item = &setting_lists[setting_info.pos.c];
        if (item->list) {
            do {
                item->lpos.n = display_change_choice(dir, item->lpos.n, item->lcount);
            } while(item->list[item->lpos.n][0] == '\0');
        } else {
            item->lpos.n = display_change_choice(dir, item->lpos.n, item->lcount);
        }
        break;
    default:
        setting_info.pos.n = display_change_choice(dir, setting_info.pos.n, sizeof(setting_lists)/sizeof(setting_lists[0]) - 1);
        break;
    }
}

static void display_setting_save_and_exit(void)
{
    display_info.phase = setting_info.prev_phase;
    setting_info.prev_phase = -1;

    setting_lists_t *item;

    // save parameter
    item = &setting_lists[SETTING_DISK_TYPE];
    config_set_disk_type(item->lpos.c);

    item = &setting_lists[SETTING_SEEK_TRACK];
    config_set_seek_track(item->lpos.c);

    item = &setting_lists[SETTING_SEARCH_SECTOR];
    config_set_search_sector(item->lpos.c);

    item = &setting_lists[SETTING_DATA_REQUSET];
    config_set_data_request(item->lpos.c);

    item = &setting_lists[SETTING_I2C_SSD1306];
    config_set_i2c_ssd1306_type(item->lpos.c);

    if (config_flash_save()) {
        display_storage_change_phase();
    } else {
        display_error_message(ERR_CANNOT_SAVE_FLASH);
    }
}

static void display_setting_exit(void)
{
    display_info.phase = setting_info.prev_phase;
    setting_info.prev_phase = -1;
    display_storage_change_phase();
}

void display_setting_confirm(void)
{
    setting_lists_t *item;

    switch (setting_info.pos.c) {
    case SETTING_SAVE:
        display_setting_save_and_exit();
        break;
    case SETTING_EXIT:
        display_setting_exit();
        break;
    case SETTING_RESET_I2C_DISP:
        item = &setting_lists[SETTING_I2C_SSD1306];
        config_set_i2c_ssd1306_type(item->lpos.c);
        display_reset_i2c_display();
        break;
    default:
        setting_info.row = 1 - setting_info.row;
        // need update display
        setting_info.pos.n = setting_info.pos.c;
        setting_info.pos.c = -1;
        break;
    }
}

void display_setting_confirm_long(void)
{
    display_setting_exit();
}

void display_setting_task(void)
{
    char str[20];
    size_t len;
    setting_lists_t *item = NULL;

    if (setting_info.pos.c != setting_info.pos.n) {
        setting_info.pos.c = setting_info.pos.n;

        item = &setting_lists[setting_info.pos.c];

        str[0] = setting_info.row == 0 ? RCURSOR : 0x20;
        strcpy(&str[1], item->title);
        lcd_padding(str, strlen(str), 16);
        lcd_locate_substring(0, 0, str, 16);
        // display choices forcely
        item->lpos.n = item->lpos.c;
        item->lpos.c = -1;
    } else {
        item = &setting_lists[setting_info.pos.c];
    }

    // choices
    if (item->lpos.c != item->lpos.n) {
        item->lpos.c = item->lpos.n;

        if (item->list) {
            str[0] = setting_info.row == 1 ? RCURSOR : 0x20;
            str[1] = 0x20;
            strcpy(&str[2], item->list[item->lpos.c]);
        } else {
            str[0]='\0';
        }
        lcd_padding(str, strlen(str), 16);
        lcd_locate_substring(0, 1, str, 16);
    }
}
