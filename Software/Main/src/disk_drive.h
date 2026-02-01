/** @file disk_drive.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef DISK_DRIVE_H
#define DISK_DRIVE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum en_disk_drive_status {
    DISK_DRIVE_MOTOR = 1,
    DISK_DRIVE_INDEX = 2,
//    DISK_DRIVE_HEAD_LOAD = 3,
} disk_drive_status_t;

void disk_drive_init();
void disk_drive_change_type(int type);
bool disk_drive_mount(int drv, int sid_num, const char *path, int offset);
void disk_drive_unmount(int drv);
void disk_drive_motor_on(int drv);
void disk_drive_motor_off();
bool disk_drive_is_motor_on();
//void disk_drive_set_head_load(int drv, bool onoff);
uint32_t disk_drive_get_one_round_time();
int disk_drive_get_current_time(int delay);
uint32_t disk_drive_get_index_hole_remain_time(int curr_time);
bool disk_drive_is_index_hole();
int disk_drive_get_current_sector_pos(int sector_nums);
uint32_t disk_drive_get_time_arrival_sector(int sector_pos, int sector_nums, int curr_time);


// callback
typedef void (*disk_drive_status_callback_t)(void);
//typedef void (*disk_drive_set_status_callback_t)(disk_drive_status_t val);
//typedef void (*disk_drive_clr_status_callback_t)(disk_drive_status_t val);

extern disk_drive_status_callback_t disk_drive_motor_on_callback;
extern disk_drive_status_callback_t disk_drive_motor_off_callback;
extern disk_drive_status_callback_t disk_drive_index_on_callback;
extern disk_drive_status_callback_t disk_drive_index_off_callback;

//extern disk_drive_set_status_callback_t disk_drive_set_status_callback;
//extern disk_drive_clr_status_callback_t disk_drive_clr_status_callback;

#endif /* DISK_DRIVE_H */
