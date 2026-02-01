/** @file fdc_common.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */
/**
  PIN Number | Desc.
 ------------|------------------------------------------
  20         | /RESET (Input)
  21         | /IRQ (Output)
  26         | BUSY (Output)(processing a command)
 */

#ifndef FDC_COMMON_H
#define FDC_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "event.h"

enum en_disk_types {
    DISK_TYPE_5INCH_0 = 0,
    DISK_TYPE_5INCH_1,
    DISK_TYPE_8INCH,
    DISK_TYPE_3INCH
};

#define FDC_COMMON_BUSY_PIN      26
#define FDC_COMMON_IRQ_PIN       21
#define FDC_COMMON_RESET_PIN     20

// for debug
//#define FDC_COMMON_DRQ_PIN       28


void fdc_common_init();
void fdc_common_reset();

void fdc_common_task();

uint8_t fdc_common_get_disk_type();
void fdc_common_set_disk_type(uint8_t type);
uint8_t fdc_common_get_disk_type_and_shift();
void fdc_common_set_disk_type_shift(bool shift_on);

void fdc_common_soft_reset();

// busy
void fdc_common_set_busy();
void fdc_common_clr_busy();

// irq
void fdc_common_set_irq();
void fdc_common_clr_irq();
bool fdc_common_now_irq();
void fdc_common_clr_irq_core1();

// drq
void fdc_common_set_drq();
void fdc_common_clr_drq();
bool fdc_common_now_drq();

//
uint8_t fdc_common_toggle_side_number(uint8_t drv, uint8_t sid);
void fdc_common_set_side_number(uint8_t drv, uint8_t sid);
uint8_t fdc_common_get_side_number(uint8_t drv);

// callback
typedef void (*fdc_common_task_callback_t)(void);
typedef void (*fdc_common_write_io_callback_t)(uint32_t addr, uint8_t data);
typedef uint8_t (*fdc_common_read_io_callback_t)(uint32_t addr);
typedef bool (*fdc_common_post_read_tightly_t)(uint8_t addr);
typedef void (*fdc_common_post_write_tightly_t)(uint8_t addr);
typedef void (*fdc_common_notice_tightly_t)(uint32_t data);

extern fdc_common_task_callback_t fdc_common_task_callback;
extern fdc_common_write_io_callback_t fdc_common_write_io_callback;
extern fdc_common_write_io_callback_t fdc_common_post_read_callback;
extern fdc_common_write_io_callback_t fdc_common_wrote_ack_callback;
extern fdc_common_read_io_callback_t fdc_common_read_io_callback;
extern fdc_common_post_write_tightly_t fdc_common_post_write_tightly_callback;
extern fdc_common_post_read_tightly_t fdc_common_post_read_tightly_callback;
extern fdc_common_notice_tightly_t fdc_common_notice_tightly_callback;

// event
typedef void (*fdc_common_event_callback_t)(uint32_t event_data);
typedef struct st_fdc_common_event {
    alarm_id_t id;
    fdc_common_event_callback_t callback;
} fdc_common_event_t;

#endif /* FDC_COMMON_H */
