/**
 * @file display_storage.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-24
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#ifndef DISPLAY_STORAGE_H
#define DISPLAY_STORAGE_H

#include "simple_list.h"
#include "common.h"
#include <ff.h>
#include "display.h"

enum en_storage_states {
    ST_STATE_IDLE = 0,
    ST_STATE_UNMOUNT,
    ST_STATE_UNMOUNT_WAIT,
    ST_STATE_UNMOUNT_DONE,
    ST_STATE_MOUNT,
    ST_STATE_MOUNT_WAIT,
    ST_STATE_MOUNT_DONE,
    ST_STATE_MOUNTING,
};

enum en_storage_flags {
    ST_FLG_FILE_NOT_FOUND = 0x0001,
    ST_FLG_DIR_NOT_FOUND = 0x0002,
    ST_FLG_NOT_FOUND_MASK = 0x000f,
    ST_FLG_NEED_MOUNT = 0x0010,
    ST_FLG_DISK_IMAGE = 0x0200,
};

struct st_file_info {
    uint8_t drv;
    uint8_t sid;        // side number for 3inch
    uint16_t flags;
    display_pos_t pos;
    simple_list_t list; // file list in current directory
    simple_list_t tree; // parent - children dir tree
    text_shift_t shift;
};

struct st_display_storage {
    uint16_t state;
//    uint16_t flags;
    alarm_id_t id;
    display_pos_t curr_drv; // current drive on file info
    struct st_file_info file_info[MAX_DRIVES];
};
extern struct st_display_storage storage_info;

typedef void (*display_storage_append_header_t)(FIL *fp, const char *path, size_t len);
extern display_storage_append_header_t display_storage_append_header_cb;
typedef void (*display_storage_progress_t)(void);
extern display_storage_progress_t display_storage_progress_cb;

void display_storage_init(void);
void display_storage_task(void);
void display_storage_change_phase(void);

void display_lcd_drive_number(int img_drv);

int display_storage_change_directory(int img_drv, const char *path);

bool display_storage_create_file_in_current_dir(const char *name, simple_list_t *tree, simple_list_t *list);

/// @brief 
/// @param  
/// @return 
static inline int display_filelist_get_current_drive(void)
{
    return storage_info.curr_drv.c;
}

void display_lcd_disp_file(struct st_file_info *file_info, simple_list_data_t *data);

void display_filelist_move(int dir);
void display_filelist_confirm_move(int dir);
void display_filelist_confirm(void);
void display_filelist_confirm_long(void);

int display_filelist_find_by_name(const char *name);
int display_filelist_find_by_subname(const char *name, size_t len);

void display_storage_lcd_debug_info(void);

#endif /* DISPLAY_STORAGE_H */
