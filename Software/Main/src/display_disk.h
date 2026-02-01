/**
 * @file display_disk.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-10-18
 * 
 * @copyright Copyright (c) Sasaji 2025
 * 
 */

#ifndef DISPLAY_DISK_H
#define DISPLAY_DISK_H

#include "simple_list.h"
#include "display_storage.h"

typedef struct st_disk_image_exts {
    const char *ext;
    uint8_t len;
    const char *comment;
} disk_image_exts_t;

extern const disk_image_exts_t disk_image_exts[2];

void display_disk_init(void);
void display_disk_task(void);

int display_disk_file_set_path(int img_drv, simple_list_t *tree, simple_list_t *list);

void display_disk_file_unmount(int img_drv);
bool display_disk_file_mount_by_path(int img_drv, const char *file_path);
bool display_disk_file_mount_on(int img_drv, uint8_t sid, const simple_list_data_t *data, simple_list_t *tree);
void display_disk_file_mount_by_info(struct st_file_info *file_info, simple_list_data_t *data);
void display_lcd_disp_file_and_disk_file_mount(struct st_file_info *file_info, simple_list_data_t *data);
void display_lcd_disp_file_and_disk_file_info(struct st_file_info *file_info, simple_list_data_t *data);
void display_disk_file_toggle_side_number(struct st_file_info *file_info);

void lcd_disk_motor(int drv, int onoff);

void lcd_disk_trk_sid_sec_number(int drv, int trk, int sid, int sec);
void lcd_disk_trk_sid_number(int drv, int trk, int sid);
void lcd_disk_sid_sec_number(int drv, int sid, int sec);
void lcd_disk_sid_number(int drv, int sid);
void lcd_disk_sec_number(int drv, int sec);
void lcd_disk_status(int drv, int tracks_per_side, int sides_per_disk, bool valid, bool protect);

#endif /* DISPLAY_DISK_H */
