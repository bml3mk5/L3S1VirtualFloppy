/** @file disk_drive.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "disk_drive.h"
#include <stdio.h>
#include <string.h>
#include "disk_d88.h"
#include "event.h"
#include "common.h"
#include "display.h"
#include "display_disk.h"
#include "fdc_common.h"
#include "config.h"
#include "utils.h"

// time per round (usec.) 300rpm
#define DISK_DRIVE_ONE_ROUND_TIME_2D    200000
// time per round (usec.) 360rpm
#define DISK_DRIVE_ONE_ROUND_TIME_2HD   166666
//
#define DISK_DRIVE_INDEX_TIME             5000
// delay time until motor off (usec.) 
#define DISK_DRIVE_MOTOR_OFF_DELAY     3000000
#define DISK_DRIVE_HEAD_LOADED_TIME	     60000

typedef struct st_disk_drives {
    uint8_t     motor_on;
    uint8_t     index_hole_on;
    uint8_t     drive_type;
    alarm_id_t  motor_off_id;
    uint32_t    motor_time;   ///< motor start/stop time;
    alarm_id_t  index_hole_id;
    uint32_t    one_round_time;
} DISK_DRIVES;
static DISK_DRIVES g_drvs;

#define DISK_DRIVE_MAX_SECTOR_POS   64
typedef struct st_disk_drive {
    uint32_t head_load : 1;
} DISK_DRIVE;
static DISK_DRIVE g_drv[MAX_DRIVES];

//disk_drive_set_status_callback_t disk_drive_set_status_callback = NULL;
//disk_drive_clr_status_callback_t disk_drive_clr_status_callback = NULL;
disk_drive_status_callback_t disk_drive_motor_on_callback = NULL;
disk_drive_status_callback_t disk_drive_motor_off_callback = NULL;
disk_drive_status_callback_t disk_drive_index_on_callback = NULL;
disk_drive_status_callback_t disk_drive_index_off_callback = NULL;

/*======================================================================*/

static int64_t motor_off_cb(alarm_id_t id, void *user_data);
static int64_t index_hole_cb(alarm_id_t id, void *user_data);

/*======================================================================*/

void disk_drive_init()
{
    memset(&g_drvs, 0, sizeof(g_drvs));
    g_drvs.motor_off_id = -1;
    g_drvs.index_hole_id = -1;
    disk_drive_change_type(0);

    memset(&g_drv, 0, sizeof(g_drv));
}

void disk_drive_change_type(int type)
{
    switch(type) {
    case DISK_DRIVE_TYPE_2HD:
        g_drvs.drive_type = DISK_DRIVE_TYPE_2HD;
        g_drvs.one_round_time = DISK_DRIVE_ONE_ROUND_TIME_2HD;
        break;
    default:
        g_drvs.drive_type = DISK_DRIVE_TYPE_2D;
        g_drvs.one_round_time = DISK_DRIVE_ONE_ROUND_TIME_2D;
        break;
    }
}

int disk_drive_get_type()
{
    return g_drvs.drive_type;
}

/*======================================================================*/

bool disk_drive_mount(int drv, int sid_num, const char *path, int offset)
{
    if (disk_d88_mount(drv, sid_num, path, offset) != FR_OK) {
//        disk_drive_clr_status_callback(DISK_DRIVE_MOTOR);
        disk_drive_motor_on_callback();
        return false;
    }
    return true;
}

void disk_drive_unmount(int drv)
{
    disk_d88_unmount(drv);
//    disk_drive_clr_status_callback(DISK_DRIVE_MOTOR);
    disk_drive_motor_off_callback();
}

void disk_drive_append_header(FIL *fp, int image_type)
{
    disk_d88_append_header(fp, config_get_disk_type());
}

/*======================================================================*/

void __not_in_flash_func(disk_drive_motor_on)(int drv)
{
    if (g_drvs.motor_on == 0) {
        if (g_drvs.motor_time < DISK_DRIVE_INDEX_TIME) {
            g_drvs.index_hole_id = event_register_event(DISK_DRIVE_INDEX_TIME - g_drvs.motor_time, index_hole_cb, 0);
        } else {
            g_drvs.index_hole_id = event_register_event(g_drvs.one_round_time - g_drvs.motor_time, index_hole_cb, 1);
        }
        {
            uint32_t t = mod_u32((uint32_t)to_us_since_boot(get_absolute_time()) - g_drvs.motor_time, g_drvs.one_round_time);
            if (t) {
                g_drvs.motor_time = g_drvs.one_round_time - t;
            } else {
                g_drvs.motor_time = t;
            }
        }
    }
    if (g_drvs.motor_on == 1) {
        event_cancel_event(&g_drvs.motor_off_id);
    }
    g_drvs.motor_on = 3;
//    disk_drive_set_status_callback(DISK_DRIVE_MOTOR);
    disk_drive_motor_on_callback();
    lcd_disk_motor(drv, 1);
}

/// @brief Motor off request
void __not_in_flash_func(disk_drive_motor_off)()
{
    if (g_drvs.motor_on == 3) {
        g_drvs.motor_off_id = event_register_event(DISK_DRIVE_MOTOR_OFF_DELAY, motor_off_cb, 0);
        g_drvs.motor_on = 1;
    }
}

bool __not_in_flash_func(disk_drive_is_motor_on)()
{
    return (g_drvs.motor_on != 0);
}

static int64_t __no_inline_not_in_flash_func(motor_off_cb)(alarm_id_t id, void *user_data)
{
    event_cancel_event(&g_drvs.index_hole_id);
    g_drvs.motor_time = mod_u32((uint32_t)to_us_since_boot(get_absolute_time()) + g_drvs.motor_time, g_drvs.one_round_time);
    g_drvs.motor_on = 0;
    g_drvs.motor_off_id = -1;
//    disk_drive_clr_status_callback(DISK_DRIVE_MOTOR);
    disk_drive_motor_off_callback();
    lcd_disk_motor(-1, 0);
    return 0;
}

//void disk_drive_set_head_load(int drv, bool onoff)
//{
//    g_drv[drv].head_load = onoff ? 1 : 0;
//}

static int64_t __no_inline_not_in_flash_func(index_hole_cb)(alarm_id_t id, void *user_data)
{
    uint32_t t = mod_u32((uint32_t)to_us_since_boot(get_absolute_time()) + g_drvs.motor_time, g_drvs.one_round_time);
    if (t < DISK_DRIVE_INDEX_TIME) {
        g_drvs.index_hole_on = 1;
//        disk_drive_set_status_callback(DISK_DRIVE_INDEX);
        disk_drive_index_on_callback();
        g_drvs.index_hole_id = event_register_event(DISK_DRIVE_INDEX_TIME - t, index_hole_cb, 0);
    } else {
        g_drvs.index_hole_on = 0;
//        disk_drive_clr_status_callback(DISK_DRIVE_INDEX);
        disk_drive_index_off_callback();
        g_drvs.index_hole_id = event_register_event(g_drvs.one_round_time - t, index_hole_cb, 1);
    }
    return 0;
}

/*======================================================================*/

uint32_t __not_in_flash_func(disk_drive_get_one_round_time)()
{
    return g_drvs.one_round_time;
}

int __not_in_flash_func(disk_drive_get_current_time)(int delay)
{
    if (delay < 0) delay += g_drvs.one_round_time;
    uint32_t cur_time = delay;
    if (g_drvs.motor_on) {
        // In motor on
        cur_time += (uint32_t)to_us_since_boot(get_absolute_time());
    }
    cur_time += g_drvs.motor_time;
    return (int)mod_u32(cur_time, g_drvs.one_round_time);
}

uint32_t __not_in_flash_func(disk_drive_get_index_hole_remain_time)(int curr_time)
{
    return g_drvs.one_round_time - curr_time;
}

bool __not_in_flash_func(disk_drive_is_index_hole)()
{
    return (g_drvs.index_hole_on != 0);
} 

int __not_in_flash_func(disk_drive_get_current_sector_pos)(int sector_nums)
{
    int div_time = (int)div_u32(g_drvs.one_round_time - DISK_DRIVE_INDEX_TIME, sector_nums);
    int curr_time = disk_drive_get_current_time(0);
    int sector_pos;
    if (curr_time < DISK_DRIVE_INDEX_TIME) {
        sector_pos = 0;
    } else {
        sector_pos = (int)div_u32(curr_time - DISK_DRIVE_INDEX_TIME, div_time);
    }
    return sector_pos; 
}

uint32_t __not_in_flash_func(disk_drive_get_time_arrival_sector)(int sector_pos, int sector_nums, int curr_time)
{
	if (sector_nums <= 0) sector_nums = 16;
    sector_pos++;
    if (sector_pos >= sector_nums) sector_pos -= sector_nums;

    int div_time = (int)div_u32(g_drvs.one_round_time - DISK_DRIVE_INDEX_TIME, sector_nums);
    int sect_time = sector_pos * div_time + DISK_DRIVE_INDEX_TIME;

    uint32_t sum;
    if (curr_time < sect_time) {
        sum = (uint32_t)sect_time - (uint32_t)curr_time;
    } else {
        sum = g_drvs.one_round_time + (uint32_t)sect_time - (uint32_t)curr_time;
    }
    if (sum >= 65568) {
        sum -= 65536;
    } else {
        sum = 32;
    }
//    printf("TC:%d TS:%d SM:%d\n", curr_time, sect_time, sum);
	return sum;
}
