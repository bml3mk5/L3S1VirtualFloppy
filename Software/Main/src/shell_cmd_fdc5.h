/** @file shell_cmd_fdc5.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef SHELL_CMD_FDC5_H
#define SHELL_CMD_FDC5_H

#include <stdint.h>
#include <stdbool.h>
#include "shell_cmd_fdc.h"

/// @brief register number
enum SHELL_CMD_FDC_5INCH_REGS {
    FDC_5INCH_STR = 0,
    FDC_5INCH_CR = 0,
    FDC_5INCH_TR = 1,
    FDC_5INCH_SCR = 2,
    FDC_5INCH_DR = 3,
    FDC_5INCH_UNIT = 4,
};
/// @brief status
enum SHELL_CMD_FDC_5INCH_STATUS_MASKS {
    FDC_5INCH_ST_BUSY		= 0x01,	///< busy
    FDC_5INCH_ST_INDEX		= 0x02,	///< index hole
    FDC_5INCH_ST_DRQ		= 0x02,	///< data request
    FDC_5INCH_ST_TRACK00	= 0x04,	///< track0
    FDC_5INCH_ST_LOSTDATA	= 0x04,	///< data lost
    FDC_5INCH_ST_CRCERR		= 0x08,	///< crc error
    FDC_5INCH_ST_SEEKERR	= 0x10,	///< seek error
    FDC_5INCH_ST_RECNFND	= 0x10,	///< sector not found
    FDC_5INCH_ST_HEADENG	= 0x20,	///< head engage
    FDC_5INCH_ST_RECTYPE	= 0x20,	///< record type
    FDC_5INCH_ST_WRITEFAULT	= 0x20,	///< write fault
    FDC_5INCH_ST_WRITEP		= 0x40,	///< write protectdc
    FDC_5INCH_ST_NOTREADY	= 0x80,	///< media not inserted
};
enum SHELL_CMD_FDC_5INCH_UNIT_MASKS {
    FDC_5INCH_UNIT_MOTOR      = 0x08, ///< motor on/off
    FDC_5INCH_UNIT_SIDE1      = 0x10, ///< side select
    FDC_5INCH_UNIT_DDEN       = 0x20, ///< double density
    FDC_5INCH_UNIT_NMI_MASK   = 0x40, ///< nmi mask
    FDC_5INCH_UNIT_IRQ        = 0x01,
    FDC_5INCH_UNIT_DRQ        = 0x80,
};

void fdc5inch_motor_off(uint8_t drv);
bool fdc5inch_motor_on(uint8_t drv, uint32_t sid_num, bool dden);

void fdc5inch_forceint();

void fdc5inch_step_rate_usage(void);

bool fdc5inch_restore(uint8_t drv, uint32_t step_rate, pftime_t *ptime);
bool fdc5inch_seek(uint8_t drv, uint32_t trk_num, uint32_t step_rate, pftime_t *ptime);
bool fdc5inch_step(uint8_t drv, int dir, uint32_t step_rate, pftime_t *ptime);

bool fdc5inch_read_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);
bool fdc5inch_write_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);

bool fdc5inch_read_track(uint8_t drv, uint32_t sid_num, pftime_t *ptime);

bool fdc5inch_read_addr(uint8_t drv, pftime_t *ptime);

void fdc5inch_after_command(void);

void fdc5inch_read_status();
void fdc5inch_unitsel(uint8_t opts);

typedef struct st_directory_l3_2d {
	uint8_t  name[8];
	uint8_t  ext[3];
	uint8_t  type;
	uint8_t  type2;
	uint8_t  start_group;
	uint16_t end_bytes;	///< used size of end cluster (big endien)
	char reserved[16];
} directory_l3_2d_t;

uint8_t fdc5inch_next_group(uint8_t cur_group);
bool fdc5inch_count_groups_and_size(const bas_dir_table_t *inf, uint8_t start_group, int *group_count, int *sector_count);
bool fdc5inch_files_entry_cb(int row, void *p_entry, void *user_data);
void fdc5inch_files_finish_cb(int row, void *user_data);
bool fdc5inch_load_entry_cb(int row, void *p_entry, void *user_data);

void fdc5inch_cmd_files(uint8_t drv);
void fdc5inch_cmd_load(uint8_t drv, const char *file_name);

bool fdc5inch_cmd_boottest(int boot_type);

#endif /* SHELL_CMD_FDC5_H */
