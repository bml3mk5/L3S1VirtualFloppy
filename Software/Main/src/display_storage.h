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

void display_storage_init(void);
void display_storage_task(void);

int display_storage_change_directory(int d88_drv, const char *path);

bool display_d88_mount(int d88_drv, const char *file_path);
void display_d88_unmount(int d88_drv);
int display_d88_get_current_drive(void);

void display_filelist_change_phase(void);
void display_filelist_move(int dir);
void display_filelist_comfirm_move(int dir);
void display_filelist_comfirm(void);

void display_storage_lcd_debug_info(void);

#endif /* DISPLAY_STORAGE_H */
