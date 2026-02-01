/** @file disk_ctl.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef DISK_CTL_H
#define DISK_CTL_H

#include <stdbool.h>

#include <tusb.h>
#include "tusb_config.h"

#include <ff.h>
#include <diskio.h>

extern FATFS fatfs[CFG_TUH_DEVICE_MAX]; // for simplicity only support 1 LUN per device
extern volatile bool _disk_busy[CFG_TUH_DEVICE_MAX];
extern scsi_inquiry_resp_t inquiry_resp;

#endif /* DISK_CTL_H */
