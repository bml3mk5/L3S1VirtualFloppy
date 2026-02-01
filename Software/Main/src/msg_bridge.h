/** @file msg_bridge.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef MSG_BRIDGE_H
#define MSG_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

enum en_msg_types {
    MSG_TYPE_MASK                = 0xffff0000,
    MSG_TYPE_INITIALIZE_DONE     = 0x0000ffff,
    MSG_TYPE_PARALLEL_WRITE      = 0x00010000,
    MSG_TYPE_PARALLEL_READ       = 0x00020000,
    MSG_TYPE_PARALLEL_NOTICE     = 0x00030000,
    MSG_TYPE_PARALLEL_WRITE_SYNC = 0x00040000,
    MSG_TYPE_PARALLEL_ACK        = 0x00050000,
};

enum en_msg_notice_types {
    MSG_NOTICE_RESET = 0,
    MSG_NOTICE_IRQ,
    MSG_NOTICE_DISK_TYPE_SHIFT,
    MSG_NOTICE_FDC_COMMON
};

void msg_init(void);
void msg_task(void);
void msg_send_data_to_core0(uint32_t type, uint8_t addr, uint8_t data);
void msg_send_data_to_core1(uint32_t type, uint8_t addr, uint8_t data);
void msg_send_data(uint32_t type, uint8_t addr, uint8_t data);

#ifdef USE_CORE1
void core1_msg_task(void);
#endif

#endif /* MSG_BRIDGE_H */


