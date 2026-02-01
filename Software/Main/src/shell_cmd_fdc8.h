/** @file shell_cmd_fdc8.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-01-01
 * 
 * @copyright Copyright (c) Sasaji 2025
 */

#ifndef SHELL_CMD_FDC8_H
#define SHELL_CMD_FDC8_H

#include <stdint.h>
#include <stdbool.h>
#include "shell_cmd_fdc.h"

/// @brief register number
enum SHELL_CMD_FDC_8INCH_REGS {
    FDC_8INCH_STR = 0,
    FDC_8INCH_CR = 0,
    FDC_8INCH_TR = 1,
    FDC_8INCH_SCR = 2,
    FDC_8INCH_DR = 3,
    FDC_8INCH_UNIT = 4,
    FDC_8INCH_HALT = 8,
    FDC_8INCH_TYPE = 12,
    FDC_8INCH_DRQ = 2,
};
/// @brief status
enum SHELL_CMD_FDC_8INCH_STATUS_MASKS {
    FDC_8INCH_ST_BUSY		= 0x01,	///< busy
    FDC_8INCH_ST_INDEX		= 0x02,	///< index hole
    FDC_8INCH_ST_DRQ		= 0x02,	///< data request
    FDC_8INCH_ST_TRACK00	= 0x04,	///< track0
    FDC_8INCH_ST_LOSTDATA	= 0x04,	///< data lost
    FDC_8INCH_ST_CRCERR		= 0x08,	///< crc error
    FDC_8INCH_ST_SEEKERR	= 0x10,	///< seek error
    FDC_8INCH_ST_RECNFND	= 0x10,	///< sector not found
    FDC_8INCH_ST_HEADENG	= 0x20,	///< head engage
    FDC_8INCH_ST_RECTYPE	= 0x20,	///< record type
    FDC_8INCH_ST_WRITEFAULT	= 0x20,	///< write fault
    FDC_8INCH_ST_WRITEP		= 0x40,	///< write protectdc
    FDC_8INCH_ST_NOTREADY	= 0x80,	///< media not inserted
};
enum SHELL_CMD_FDC_8INCH_UNIT_MASKS {
    FDC_8INCH_UNIT_MOTOR      = 0x08, ///< motor on/off
    FDC_8INCH_UNIT_SIDE1      = 0x10, ///< side select
    FDC_8INCH_UNIT_DDEN       = 0x20, ///< double density
    FDC_8INCH_UNIT_FAULT_RES  = 0x40, ///< fault reset
    FDC_8INCH_UNIT_FDC_MASK   = 0x80, ///< fdc mask
};

enum SHELL_CMD_FDC_8INCH_SOME_MASKS {
    FDC_8INCH_UNIT_DRQ        = 0x80,
    FDC_8INCH_UNIT_IRQ        = 0x01,
    FDC_8INCH_ACCESS_NG       = 0x01,
    FDC_8INCH_NMI_MASK        = 0x01,
};

void fdc8inch_motor_off(uint8_t drv);
bool fdc8inch_motor_on(uint8_t drv, uint32_t sid_num, bool dden);

void fdc8inch_forceint();

void fdc8inch_step_rate_usage(void);

bool fdc8inch_restore(uint8_t drv, uint32_t step_rate, pftime_t *ptime);
bool fdc8inch_seek(uint8_t drv, uint32_t trk_num, uint32_t step_rate, pftime_t *ptime);
bool fdc8inch_step(uint8_t drv, int dir, uint32_t step_rate, pftime_t *ptime);

bool fdc8inch_read_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);
bool fdc8inch_read_data_2(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);
bool fdc8inch_read_data_3(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);
bool fdc8inch_write_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);
bool fdc8inch_write_data_2(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);

bool fdc8inch_read_track(uint8_t drv, uint32_t sid_num, pftime_t *ptime);

bool fdc8inch_read_addr(uint8_t drv, pftime_t *ptime);

void fdc8inch_after_command(void);

void fdc8inch_read_status();
void fdc8inch_unitsel(uint8_t opts);

void fdc8inch_cmd_files(uint8_t drv);
void fdc8inch_cmd_load(uint8_t drv, const char *file_name);

bool fdc8inch_cmd_boottest(int boot_type);

#endif /* SHELL_CMD_FDC8_H */
