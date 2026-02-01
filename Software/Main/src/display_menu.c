/**
 * @file display_menu.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2026-01-12
 * 
 * @copyright Copyright (c) Sasaji 2026
 * 
 */

#include "display_menu.h"
#include <string.h>
#include "common.h"
#include "main.h"
#include "display.h"
#include "display_storage.h"
#include "display_disk.h"
#include "display_setting.h"
//#include "config.h"
#include "event.h"
//#include "msc_app.h"

enum en_filemenu_lists {
    FILEMENU_CREATE_FILE = 0,
//    FILEMENU_DELETE_FILE,
    FILEMENU_SETTINGS,
    FILEMENU_EXIT,
    FILEMENU_LIST_MAX
};

typedef struct st_filemenu_lists {
    const char *title;
    display_pos_t lpos;
    int16_t args[2];
} filemenu_lists_t;

static filemenu_lists_t filemenu_lists[] = {
    // 012345678901234
    { "Create file", {0, 0}, {0, 0} },
//    { "Delete file", {0, 0}, {0, 0} },
    { "Settings", {0, 0}, {0, 0} },
    { "Exit", {0, 0}, {0, 0} },
    { NULL, {0, 0}, {0, 0} }
};

enum en_filemenu_info_state {
    FILEMENU_STATE_INITIAL = 0,
    FILEMENU_STATE_TYPE,
    FILEMENU_STATE_CONFIRM,
    FILEMENU_STATE_DESIDE,
    FILEMENU_STATE_RESULT,
    FILEMENU_STATE_MAX
};

static struct st_filemenu_info {
    display_pos_t pos;
    display_pos_t state;
    int prev_phase;
} filemenu_info;

static alarm_id_t event_id;
static char filemenu_name[20];

static char buffer_string[20];

//--------------------------------------------------------------------

void display_menu_init(void)
{
    filemenu_info.pos.c = -1;
    filemenu_info.pos.n = 0;
    filemenu_info.state.c = 0;
    filemenu_info.state.n = 0;
    filemenu_info.prev_phase = -1;
    event_id = -1;
}

void display_menu_change_phase(void)
{
    if (display_info.phase != PHASE_MENU) {
        filemenu_info.prev_phase = display_info.phase;
    }
    display_info.phase = PHASE_MENU;
    filemenu_info.pos.c = -1;
    filemenu_info.pos.n = 0;
    filemenu_info.state.c = 0;
    filemenu_info.state.n = 0;

    filemenu_lists[FILEMENU_CREATE_FILE].lpos.c = 0;
//    filemenu_lists[FILEMENU_DELETE_FILE].lpos.c = 0;
    filemenu_lists[FILEMENU_SETTINGS].lpos.c = 0;
}

//--------------------------------------------------------------------

/// @brief 
/// @param msg
/// @param yesno 0:yes 1:no 
void display_menu_yesno_message(const char *msg, int16_t yesno)
{
    char str[20];

    strncpy(str, msg, 9);
    lcd_padding(str, strlen(str), 10);
    memcpy(&str[10], "Yes No", 6);
    str[9] = (yesno == 0) ? RCURSOR : ' ';
    str[13] = (yesno == 1) ? RCURSOR : ' ';
    str[16] = '\0';
    lcd_locate_substring(0, 1, str, 16);
}

//--------------------------------------------------------------------

static void display_menu_go_state_initial(void)
{
    // 
    event_cancel_event(&event_id);
    // initial state
    filemenu_info.state.n = 0;
    // need update display
    filemenu_info.pos.n = filemenu_info.pos.c;
    filemenu_info.pos.c = -1;
}

static void display_menu_state_initial(filemenu_lists_t *item)
{
    //                          0123456789012345
    lcd_locate_substring(0, 0, "  *** MENU ***  ", 16);

    buffer_string[0] = ' ';
    strcpy(&buffer_string[1], item->title);
    lcd_padding(buffer_string, strlen(buffer_string), 16);
    lcd_locate_substring(0, 1, buffer_string, 16);

    // display choices forcely
    item->lpos.n = item->lpos.c;
    item->lpos.c = -1;
}

static int64_t display_menu_post_state_result_cb(alarm_id_t id, void *user_data)
{
    display_menu_go_state_initial();
    return 0;
}

static void display_menu_go_state_result(char *msg, size_t len)
{
    filemenu_info.state.n = filemenu_info.state.c + 1;
    event_cancel_event(&event_id);
    lcd_padding(msg, len, 16);
    lcd_locate_substring(0, 1, msg, 16);
    event_id = event_register_event(3000000, display_menu_post_state_result_cb, 0);
}

//--------------------------------------------------------------------

static void display_menu_create_file_state_select_type(filemenu_lists_t *item)
{
    if (filemenu_info.state.c != filemenu_info.state.n) {
        //                          0123456789012345
        lcd_locate_substring(0, 0, "Select type:    ", 16);
    }
    buffer_string[0]=' ';
    strcpy(&buffer_string[1], disk_image_exts[item->lpos.c].comment);
    lcd_padding(buffer_string, strlen(buffer_string), 16);
    lcd_locate_substring(0, 1, buffer_string, 16);
}

static void display_menu_create_file_state_confirm(filemenu_lists_t *item)
{
    if (filemenu_info.state.c != filemenu_info.state.n) {
        // create new file name
        int match = 0;
        for(int i=0; i<10000 && match >= 0; i++) {
            sprintf(buffer_string, "disk%04d", i);
            match = display_filelist_find_by_subname(buffer_string, 8);
        }
        strcat(buffer_string, disk_image_exts[item->args[0]].ext);
        strcpy(filemenu_name, buffer_string);
        lcd_padding(buffer_string, strlen(buffer_string), 16);
        lcd_locate_substring(0, 0, buffer_string, 16);

        item->lpos.c = item->lpos.n = 1; // select NO by default
    }
    display_menu_yesno_message(" Create?", item->lpos.c);
}

static int progress_invert = 0;
static uint32_t progress_start_ms = 0;

static void display_menu_create_progress(void)
{
    const uint32_t interval_ms = 500;

    if (g_c0_current_time_ms < progress_start_ms + interval_ms) return; // not enough time
    progress_start_ms += interval_ms;
    progress_invert = 1 - progress_invert;

    if (progress_invert) {
        buffer_string[0] = '\0';
    } else {
        strcpy(buffer_string, " Creating...");
    }
    lcd_padding(buffer_string, strlen(buffer_string), 16);
    lcd_locate_substring(0, 1, buffer_string, 16);
}

static void display_menu_create_progress_start(void)
{
    progress_invert = 1;
    progress_start_ms = g_c0_current_time_ms;
    display_storage_progress_cb = display_menu_create_progress;
}

static void display_menu_create_progress_stop(void)
{
    display_storage_progress_cb = NULL;
}

static void display_menu_create_file_state_decide(filemenu_lists_t *item)
{
    display_menu_create_progress_start();

    if (item->args[1] == 0) {
        // create new file
        struct st_display_storage *si = &storage_info;
        struct st_file_info *fi = &si->file_info[si->curr_drv.c];
        if (display_storage_create_file_in_current_dir(filemenu_name, &fi->tree, &fi->list)) {
            // success
            //           0123456789012345
            strcpy(buffer_string, " Created.");
        } else {
            // fail
            //           0123456789012345
            strcpy(buffer_string, " Error.");
        }
    } else {
        // cancel
        //           0123456789012345
        strcpy(buffer_string, " Canceled.");
    }

    display_menu_create_progress_stop();

    display_menu_go_state_result(buffer_string, strlen(buffer_string));
}

//--------------------------------------------------------------------

void display_menu_move(int dir)
{
    filemenu_lists_t *item;

    if (filemenu_info.state.c == FILEMENU_STATE_INITIAL) {
        filemenu_info.pos.n = display_change_choice(dir, filemenu_info.pos.n, sizeof(filemenu_lists)/sizeof(filemenu_lists[0]) - 1);
    } else {
        item = &filemenu_lists[filemenu_info.pos.c];
        switch(filemenu_info.pos.c) {
        case FILEMENU_CREATE_FILE:
            switch(filemenu_info.state.c) {
            case FILEMENU_STATE_TYPE:
                item->lpos.n = display_change_choice(dir, item->lpos.n, sizeof(disk_image_exts)/sizeof(disk_image_exts[0]) - 1);
                break;
            case FILEMENU_STATE_CONFIRM:
                item->lpos.n = display_change_choice(dir, item->lpos.n, 2);
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

void display_menu_exit(void)
{
    display_info.phase = filemenu_info.prev_phase;
    filemenu_info.prev_phase = -1;
    display_lcd_change_phase();
}

void display_menu_confirm(void)
{
    filemenu_lists_t *item;

    switch (filemenu_info.pos.c) {
    case FILEMENU_SETTINGS:
        display_setting_change_phase();
        break;
    case FILEMENU_EXIT:
        display_menu_exit();
        break;
    default:
        item = &filemenu_lists[filemenu_info.pos.c];
        if (storage_info.state != ST_STATE_MOUNTING) {
            filemenu_info.state.c = -1; // go initial state
        }
        switch(filemenu_info.state.c) {
        case FILEMENU_STATE_CONFIRM:
        case FILEMENU_STATE_TYPE:
            item->args[filemenu_info.state.c - 1] = item->lpos.c;
            filemenu_info.state.n = filemenu_info.state.c + 1;
            // need update display
            item->lpos.n = item->lpos.c;
            item->lpos.c = -1;
            break;
        case FILEMENU_STATE_INITIAL:
            filemenu_info.state.n = FILEMENU_STATE_CONFIRM;
            // need update display
            item->lpos.n = item->lpos.c;
            item->lpos.c = -1;
            break;
        default:
            display_menu_go_state_initial();
            break;
        }
        break;
    }
}

void display_menu_confirm_long(void)
{
    display_menu_exit();
}

//--------------------------------------------------------------------

void display_menu_task(void)
{
    filemenu_lists_t *item = NULL;

    // select category on the menu
    if (filemenu_info.pos.c != filemenu_info.pos.n) {
        filemenu_info.pos.c = filemenu_info.pos.n;

        item = &filemenu_lists[filemenu_info.pos.c];

        if (filemenu_info.state.n == FILEMENU_STATE_INITIAL) {
            display_menu_state_initial(item);
        }
    } else {
        item = &filemenu_lists[filemenu_info.pos.c];
    }

    // choices in category
    if (item->lpos.c != item->lpos.n) {
        item->lpos.c = item->lpos.n;

        switch(filemenu_info.pos.c) {
        case FILEMENU_CREATE_FILE:
            switch(filemenu_info.state.n) {
            case FILEMENU_STATE_TYPE:
                display_menu_create_file_state_select_type(item);
                break;
            case FILEMENU_STATE_CONFIRM:
                display_menu_create_file_state_confirm(item);
                break;
            case FILEMENU_STATE_DESIDE:
                display_menu_create_file_state_decide(item);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    filemenu_info.state.c = filemenu_info.state.n;
}
