/** @file shell_cmd_fdc.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef SHELL_CMD_FDC_H
#define SHELL_CMD_FDC_H

#include <stdint.h>
#include <stdbool.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include "common.h"
#include "main.h"

/// @brief status
enum FDCC_ST_MASKS {
    FDCC_ST_BUSY		= 0x01,	///< busy
    FDCC_ST_SEEKERR     = 0x02,	///< seek error
    FDCC_ST_RECNFND     = 0x04,	///< sector not found
    FDCC_ST_LOSTDATA	= 0x08,	///< data lost
    FDCC_ST_CRCERR		= 0x10,	///< crc error
    FDCC_ST_DELETED     = 0x20,	///< deleted mark
    FDCC_ST_WRITEP		= 0x40,	///< write protectdc
    FDCC_ST_NOTREADY	= 0x80,	///< media not inserted
};

typedef struct st_shell_cmd_fdc_buffer {
    int count;
    uint8_t data[256];
} shell_cmd_fdc_buffer_t;

typedef struct st_fdccommon {
    uint8_t drv;
    uint8_t trk[MAX_DRIVES];
    bool verbose;
    bool verbose_force;
    uint32_t status;
    shell_cmd_fdc_buffer_t buffer;
} shell_cmd_fdc_t;

extern shell_cmd_fdc_t g_shell_cmd_fdc;

typedef struct st_pftime {
    uint64_t start;
    uint32_t count;
    uint32_t data[260];
} pftime_t;

void shell_cmd_fdc_init();
void shell_cmd_fdc_change_disk_type(uint8_t type);
void shell_cmd_fdc_dump_data(shell_cmd_fdc_buffer_t *buff);

bool shell_cmd_fdc_check_status(uint32_t sts, uint32_t mask);

void fdc_cmd_type_main(uint16_t argc, const char *args, int cmd_type);

void fdc_cmd(char *args);

bool split_filename(const char *file_name, char *name, char *ext);

static inline void wait_spin_us(int us)
{
#ifdef IS_HOST_TEST
    absolute_time_t ts = get_absolute_time();
    sleep_us(us);
    absolute_time_t te = get_absolute_time();
    while (absolute_time_diff_us(ts, te) < us) {
        __isb();
        te = get_absolute_time();
    }
#else
    absolute_time_t ts = get_absolute_time();
    absolute_time_t te = ts;
    while (absolute_time_diff_us(ts, te) < us) {
        main_loop_contents_in_shell_cmd();
        te = get_absolute_time();
    }
#endif
//    busy_wait_us_32(us);
}

/// @brief 
/// @return true is last
typedef bool (*directory_entry_cb_t)(int row, void *p_entry, void *user_data);
typedef void (*directory_finish_cb_t)(int row, void *user_data);

void bas_cmd_files(uint32_t drv);
void bas_cmd_load(uint32_t drv, const char *file_name);

extern uint8_t bas_fat_table[256];

typedef struct st_bas_fat_table {
    uint32_t trk_num;
    uint32_t start_sec_num;
    uint32_t end_sec_num;
    uint32_t step_rate;
    bool dden;
} bas_fat_table_t;

bool bas_read_fat_table(uint8_t drv, const bas_fat_table_t *inf, uint8_t *buffer, int buffer_size);

typedef struct st_bas_dir_table {
    uint32_t secs_per_trk;
    uint32_t sector_size;
    uint32_t trk_num;
    uint32_t start_sec_num;
    uint32_t end_sec_num;
    uint32_t secs_per_grp;
    bool dden;
} bas_dir_table_t;

bool bas_read_directory(uint8_t drv, const bas_dir_table_t *inf, directory_entry_cb_t callback_e, directory_finish_cb_t callback_f, void *user_data);

typedef struct st_bas_file_access {
    char name[12];
    char ext[4];
    int8_t  match;
    uint8_t start_group;
    uint16_t end_bytes; 
} bas_file_access_t;

bool fdc_cmd_boottest(int boot_type);

int cli_d88_get_data(int drv, char *data, int size);
uint8_t str_to_uint8(const char *str);

bool cli_d88_check_data(int drv, const uint8_t *dst, uint32_t trk_num, uint32_t sec_num, uint32_t sec_size, uint32_t sts, uint32_t xaddr);

#endif /* SHELL_CMD_FDC_H */
