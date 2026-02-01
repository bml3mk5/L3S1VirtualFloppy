/** @file shell_cmd_fdc3.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef SHELL_CMD_FDC3_H
#define SHELL_CMD_FDC3_H

#include <stdint.h>
#include <stdbool.h>
#include "shell_cmd_fdc.h"

/// @brief register number
enum SHELL_CMD_FDC_3INCH_REGS {
    FDC_3INCH_DOR = 0x18,
    FDC_3INCH_DIR = 0x18,
    FDC_3INCH_CTAR = 0x19,
    FDC_3INCH_CMR = 0x1a,
    FDC_3INCH_ISR = 0x1a,
    FDC_3INCH_SUR = 0x1b,
    FDC_3INCH_STRA = 0x1b,
    FDC_3INCH_SAR = 0x1c,
    FDC_3INCH_STRB = 0x1c,
    FDC_3INCH_GCR = 0x1d,
    FDC_3INCH_CCR = 0x1e,
    FDC_3INCH_LTAR = 0x1f,
    FDC_3INCH_UNIT = 0x20,
};

/// @brief interrupt status
enum SHELL_CMD_FDC_3INCH_CMR_MASKS {
	FDC_3INCH_CMR_FWF		= 0x10,	///< free format write flag
	FDC_3INCH_CMR_DMA		= 0x20,	///< DMA flag
	FDC_3INCH_CMR_ISR3MASK	= 0x40,	///< ISR3 interrupt mask
	FDC_3INCH_CMR_FUNCMASK	= 0x80,	///< Function interrupt mask
};

/// @brief interrupt status
enum SHELL_CMD_FDC_3INCH_ISR_MASKS {
	FDC_3INCH_ISR_STRB		= 0x08,	///< STRB
	FDC_3INCH_ISR_STSREQ	= 0x04,	///< status sense request
	FDC_3INCH_ISR_SETCOMP	= 0x02,	///< setting time complete
	FDC_3INCH_ISR_CMDCOMP	= 0x01,	///< macro command complete
};

/// @brief status code A
enum SHELL_CMD_FDC_3INCH_STA_MASKS {
	FDC_3INCH_STA_BUSY		= 0x80,	///< busy
	FDC_3INCH_STA_INDEX		= 0x40,	///< index hole
	FDC_3INCH_STA_TRACKNE	= 0x20,	///< track not equal
	FDC_3INCH_STA_WRITEP	= 0x10,	///< write protect
	FDC_3INCH_STA_TRACK00	= 0x08,	///< track zero
	FDC_3INCH_STA_DREADY	= 0x04,	///< drive ready
	FDC_3INCH_STA_DELETE	= 0x02,	///< delete data mark detected
	FDC_3INCH_STA_DRQ		= 0x01,	///< data transfar request
};

/// @brief status code B
enum SHELL_CMD_FDC_3INCH_STB_MASKS {
	FDC_3INCH_STB_HARDERR	= 0x80,	///< hard error
	FDC_3INCH_STB_WRITEERR	= 0x40,	///< write error
	FDC_3INCH_STB_FILEINO	= 0x20,	///< file inoperable
	FDC_3INCH_STB_SEEKERR	= 0x10,	///< seek error
	FDC_3INCH_STB_SECTNF	= 0x08,	///< sector address undeteced
	FDC_3INCH_STB_DATANF	= 0x04,	///< data mark undetected
	FDC_3INCH_STB_CRCERR	= 0x02,	///< crc error
	FDC_3INCH_STB_DATAERR	= 0x01,	///< data transfar error
};
enum SHELL_CMD_FDC_3INCH_UNIT_MASKS {
    FDC_3INCH_UNIT_MOTOR      = 0x80, ///< motor on/off
    FDC_3INCH_UNIT_DRIVES     = 0x0f,
    FDC_3INCH_UNIT_DRIVE0     = 0x01,
};

void fdc3inch_motor_off(uint8_t drv);
bool fdc3inch_motor_on(uint8_t drv, uint32_t sid_num, bool dden);

void fdc3inch_forceint();

void fdc3inch_step_rate_usage(void);

bool fdc3inch_restore(uint8_t drv, uint32_t step_rate, pftime_t *ptime);
bool fdc3inch_seek(uint8_t drv, uint32_t trk_num, uint32_t step_rate, pftime_t *ptime);
bool fdc3inch_step(uint8_t drv, int dir, uint32_t step_rate, pftime_t *ptime);

bool fdc3inch_read_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);
bool fdc3inch_write_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);

bool fdc3inch_read_track(uint8_t drv, uint32_t sid_num, pftime_t *ptime);

void fdc3inch_after_command(void);

void fdc3inch_read_status();
void fdc3inch_unitsel(uint8_t opts);

void fdc3inch_cmd_files(uint8_t drv);
void fdc3inch_cmd_load(uint8_t drv, const char *file_name);

bool fdc3inch_cmd_boottest(int boot_type);

#endif /* SHELL_CMD_FDC3_H */
