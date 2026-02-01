/** @file disk_d88.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef DISK_D88_H
#define DISK_D88_H

#include <stdint.h>
#include <ff.h>
#include "event.h"

// d88 media type
#define MEDIA_TYPE_2D	0x00
#define MEDIA_TYPE_2DD	0x10
#define MEDIA_TYPE_2HD	0x20
#define MEDIA_TYPE_UNK	0xff

#define DRIVE_TYPE_2D	MEDIA_TYPE_2D
#define DRIVE_TYPE_2DD	MEDIA_TYPE_2DD
#define DRIVE_TYPE_2HD	MEDIA_TYPE_2HD
#define DRIVE_TYPE_UNK	MEDIA_TYPE_UNK

/*======================================================================*/

#define D88_MAX_TRACKS	164

/// d88 header
typedef struct d88_hdr_st {
	char title[17];
	uint8_t rsrv[9];
	uint8_t protect;
	uint8_t media_type;
	uint32_t size;
	uint32_t trkptr[D88_MAX_TRACKS];
} d88_hdr_t;

/// d88 sector
typedef union d88_sct_st {
    struct {
        uint8_t c, h, r, n;
        uint16_t nsec;
        uint8_t dens, del, stat;
        uint8_t rsrv[5];
        uint16_t size;
    };
    uint8_t b[16];
} d88_sct_t;

#define DISK_D88_MAX_BUFFER 2048

typedef struct disk_d88_st {
    FIL fil;    /* file object */
	struct {
		uint32_t mounted : 1;
		uint32_t modified: 1;
        uint32_t need_flush: 1;
	} flags;
	alarm_id_t delay_write_event_id;

	uint8_t drive_number;
    uint8_t sides_per_disk;
    uint8_t tracks_per_side;
    d88_hdr_t header;

    uint8_t curr_track_number;
    uint8_t curr_side_number;
    uint8_t curr_sector_number;

    d88_sct_t curr_sector;
	FSIZE_t curr_sector_offset;
    uint16_t calcd_address_crc;

    int data_pos;
    uint8_t data[DISK_D88_MAX_BUFFER];
    uint16_t calcd_crc;
} DISK_D88;

/*======================================================================*/

void disk_d88_init();
void disk_d88_task();
void disk_d88_info(int drv);
void disk_d88_cat(int drv);
void disk_d88_get_data(int drv);

void disk_d88_disp_lcd(int drv);

int disk_d88_mount(int drv, int side_number, const char *path, int offset);
int disk_d88_unmount(int drv);

void disk_d88_append_header(FIL *fp, int disk_type);

int disk_d88_step(int drv, int track_number, int side_number);
int disk_d88_step_in(int drv, int side_number);
int disk_d88_step_out(int drv, int side_number);
int disk_d88_restore(int drv);

void disk_d88_get_crc(int drv);

int disk_d88_read_sector(int drv, int track_number, int side_number, int sector_number, int density, int deleted, bool calc_crc, int *sector_pos);
int disk_d88_read_sector_id(int drv, int side_number, int sector_pos);

int disk_d88_read_data(int drv, uint8_t *data);
int disk_d88_read_address(int drv, uint8_t *data);

int disk_d88_write_data(int drv, uint8_t data);

int disk_d88_flush(int drv);
int disk_d88_delay_write_event(int drv);

int disk_d88_read_track(int drv, uint8_t *data);
int disk_d88_write_track(int drv, uint8_t data);

int disk_d88_verify_track(int drv, int track_number, int side_number);

bool disk_d88_is_not_ready(int drv);
bool disk_d88_is_write_protected(int drv);
bool disk_d88_is_track0(int drv);
//int disk_d88_is_index_hole(int drv);
//int disk_d88_set_motor(int drv, int onoff);
int disk_d88_set_deleted_mark(int drv, int val);
int disk_d88_get_track_size(int drv, int density);
int disk_d88_get_sector_size(int drv);
int disk_d88_get_sector_nums(int drv);

void disk_d88_set_side_number(int drv, int side_number);
//int disk_d88_set_head_load(int drv, int onoff);

DISK_D88 *disk_d88_get_disk(int drv);

void disk_d88_info_on(DISK_D88 *disk);
void disk_d88_cat_on(DISK_D88 *disk);
void disk_d88_get_data_on(DISK_D88 *disk);

void disk_d88_disp_lcd_on(DISK_D88 *disk);

int disk_d88_mount_on(DISK_D88 *disk, int side_number, const char *path, int offset);
int disk_d88_unmount_on(DISK_D88 *disk);

int disk_d88_step_on(DISK_D88 *disk, int track_number, int side_number);

void disk_d88_get_crc_on(DISK_D88 *disk);

int disk_d88_read_sector_on(DISK_D88 *disk, int track_number, int side_number, int sector_number, int density, int deleted, bool calc_crc, int *sector_pos);
int disk_d88_read_sector_id_on(DISK_D88 *disk, int side_number, int sector_pos);

int disk_d88_read_data_on(DISK_D88 *disk, uint8_t *data);
int disk_d88_read_address_on(DISK_D88 *disk, uint8_t *data);

int disk_d88_write_data_on(DISK_D88 *disk, uint8_t data);

int disk_d88_flush_on(DISK_D88 *disk);
int disk_d88_delay_write_event_on(DISK_D88 *disk);

int disk_d88_write_track_on(DISK_D88 *disk, uint8_t data);

bool disk_d88_is_not_ready_on(DISK_D88 *disk);
bool disk_d88_is_write_protected_on(DISK_D88 *disk);
bool disk_d88_is_track0_on(DISK_D88 *disk);
int disk_d88_set_deleted_mark_on(DISK_D88 *disk, int val);
int disk_d88_get_track_size_on(DISK_D88 *disk, int density);
int disk_d88_get_sector_size_on(DISK_D88 *disk);
int disk_d88_get_sector_nums_on(DISK_D88 *disk);

void disk_d88_set_side_number_on(DISK_D88 *disk, int side_number);
//int disk_d88_set_head_load_on(DISK_D88 *disk, int onoff);

void disk_d88_verbose_mode(bool onoff);
extern bool disk_d88_is_verbose_mode;

#endif /* DISK_D88_H */
