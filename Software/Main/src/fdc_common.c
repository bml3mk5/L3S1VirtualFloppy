/** @file fdc_common.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include <stdio.h>
#include <string.h>
#include <hardware/gpio.h>
#include <pico/binary_info.h>
#include "common.h"
#include "fdc_common.h"
#include "fdc_5inch.h"
#include "fdc_8inch.h"
#include "fdc_3inch.h"
#include "disk_d88.h"
#include "disk_drive.h"
#include "event.h"
#include "main.h"
#include "msg_bridge.h"
#include "pio_ctrls.h"
#include "config.h"
#include "display_setting.h"
#include "display_reset.h"
#include "shell_cmd_fdc.h"

#ifndef OUT_DEBUG
#define OUT_DEBUG(...)
#endif

enum en_fdc_common_flags {
    FDC_COMMON_FLAG_RESET      = 0x00000001,
    FDC_COMMON_FLAG_SOFT_RESET = 0x00000002
};

typedef struct {
    uint8_t disk_type;
    uint8_t disk_type_shift;
    uint32_t flags;
} FDC_COMMON;

static FDC_COMMON g_fdc_common;
static FDC_COMMON *dev = &g_fdc_common;

fdc_common_task_callback_t fdc_common_task_callback = NULL;
fdc_common_write_io_callback_t fdc_common_write_io_callback = NULL;
fdc_common_write_io_callback_t fdc_common_post_read_callback = NULL;
fdc_common_write_io_callback_t fdc_common_wrote_ack_callback = NULL;
fdc_common_read_io_callback_t fdc_common_read_io_callback = NULL;
fdc_common_post_write_tightly_t MY_CORE1_GROUP(fdc_common_post_write_tightly_callback) = NULL;
fdc_common_post_read_tightly_t MY_CORE1_GROUP(fdc_common_post_read_tightly_callback) = NULL;
fdc_common_notice_tightly_t MY_CORE1_GROUP(fdc_common_notice_tightly_callback) = NULL;

//--------------------------------------------------------------------

static void fdc_common_change_disk_type();

//--------------------------------------------------------------------

/// @brief initialize
void fdc_common_init()
{
    memset(&g_fdc_common, 0, sizeof(g_fdc_common));

    fdc_common_change_disk_type();

    // reset pin
    gpio_init(FDC_COMMON_RESET_PIN);
    gpio_pull_up(FDC_COMMON_RESET_PIN);
    gpio_set_dir(FDC_COMMON_RESET_PIN, false);
    bi_decl(bi_pin_mask_with_name(1u << FDC_COMMON_RESET_PIN, "RESET# (Out)"));

    // irq pin
    gpio_init(FDC_COMMON_IRQ_PIN);
    gpio_pull_up(FDC_COMMON_IRQ_PIN);
    gpio_set_dir(FDC_COMMON_IRQ_PIN, true);
    gpio_put(FDC_COMMON_IRQ_PIN, true);
    bi_decl(bi_pin_mask_with_name(1u << FDC_COMMON_IRQ_PIN, "IRQ# (Out)"));

    // busy pin
    gpio_init(FDC_COMMON_BUSY_PIN);
    gpio_pull_down(FDC_COMMON_BUSY_PIN);
    gpio_set_dir(FDC_COMMON_BUSY_PIN, true);
    gpio_put(FDC_COMMON_BUSY_PIN, false);
    bi_decl(bi_pin_mask_with_name(1u << FDC_COMMON_BUSY_PIN, "BUSY (Out)"));

#ifdef FDC_COMMON_DRQ_PIN
    gpio_init(FDC_COMMON_DRQ_PIN);
    gpio_pull_down(FDC_COMMON_DRQ_PIN);
    gpio_set_dir(FDC_COMMON_DRQ_PIN, true);
    gpio_put(FDC_COMMON_DRQ_PIN, true);
    bi_decl(bi_pin_mask_with_name(1u << FDC_COMMON_DRQ_PIN, "DRQ# (Out)"));
#endif

    fdc_5inch_init();
    fdc_8inch_init();
    fdc_3inch_init();

    fdc_common_reset();
}

void fdc_common_reset()
{
    // change fdc type
    uint8_t old_disk_type = dev->disk_type;
    dev->disk_type = config_get_disk_type();
    pio_ctrls_set_disk_type(dev->disk_type);

    fdc_common_change_disk_type();
    shell_cmd_fdc_change_disk_type(dev->disk_type);

    dev->disk_type_shift = 0;

    switch(dev->disk_type) {
    case DISK_TYPE_3INCH:
        disk_drive_change_type(DISK_DRIVE_TYPE_2D);
        fdc_3inch_reset();
        break;
    case DISK_TYPE_8INCH:
        disk_drive_change_type(DISK_DRIVE_TYPE_2HD);
        fdc_8inch_reset();
        break;
    default:
        disk_drive_change_type(DISK_DRIVE_TYPE_2D);
        if (dev->disk_type == old_disk_type) {
            fdc_5inch_unitsel_reset();
        } else {
            fdc_5inch_reset();
        }
        break;
    }
}

void __no_inline_not_in_flash_func(fdc_common_task)()
{
    uint32_t now_reset = (!gpio_get(FDC_COMMON_RESET_PIN) || (dev->flags & FDC_COMMON_FLAG_SOFT_RESET)) ? FDC_COMMON_FLAG_RESET : 0;
    if ((dev->flags ^ now_reset) & FDC_COMMON_FLAG_RESET) {
        // trigger reset signal
        fdc_common_reset();
        display_reset_change_phase();
    }
    dev->flags = ((dev->flags & ~FDC_COMMON_FLAG_RESET) | now_reset);

    fdc_common_task_callback();
}

uint8_t fdc_common_get_disk_type()
{
    return dev->disk_type;
}

void fdc_common_set_disk_type(uint8_t type)
{
    dev->disk_type = type;
}

uint8_t fdc_common_get_disk_type_and_shift()
{
    return dev->disk_type | dev->disk_type_shift;
}

void __no_inline_not_in_flash_func(fdc_common_set_disk_type_shift)(bool shift_on)
{
    dev->disk_type_shift = (shift_on ? dev->disk_type_shift | 0x04 : dev->disk_type_shift & ~0x04);
}

static int64_t fdc_common_soft_reset_event_callback(alarm_id_t id, void *user_data)
{
    dev->flags &= ~FDC_COMMON_FLAG_SOFT_RESET;
}

void fdc_common_soft_reset()
{
    dev->flags |= FDC_COMMON_FLAG_SOFT_RESET;
    event_register_event(200000, fdc_common_soft_reset_event_callback, 0);
}

//--------------------------------------------------------------------

static void fdc_common_change_disk_type()
{
    switch(dev->disk_type) {
    case DISK_TYPE_3INCH:
        fdc_3inch_set_callback();
        break;
    case DISK_TYPE_8INCH:
        fdc_8inch_set_callback();
        break;
    default:
        fdc_5inch_set_callback();
        break;
    }
}

//--------------------------------------------------------------------

#if 0
void fdc_common_write_io(uint32_t addr, uint8_t data)
{
    switch(dev->disk_type) {
    case DISK_TYPE_3INCH:
        fdc_3inch_write_io(addr, data);
        break;
    case DISK_TYPE_8INCH:
        fdc_8inch_write_io(addr, data);
        break;
    default:
        fdc_5inch_write_io(addr, data);
        break;
    }
}

void fdc_common_post_read(uint32_t addr, uint8_t data)
{
    switch(dev->disk_type) {
    case DISK_TYPE_3INCH:
        fdc_3inch_post_read(addr, data);
        break;
    case DISK_TYPE_8INCH:
        fdc_8inch_post_read(addr, data);
        break;
    default:
        fdc_5inch_post_read(addr, data);
        break;
    }
}
#endif

//--------------------------------------------------------------------
// busy
void fdc_common_set_busy()
{
    gpio_put(FDC_COMMON_BUSY_PIN, true);
}

void fdc_common_clr_busy()
{
    gpio_put(FDC_COMMON_BUSY_PIN, false);
}

//--------------------------------------------------------------------
// irq
void fdc_common_set_irq()
{
    // negative logic
    gpio_put(FDC_COMMON_IRQ_PIN, false);
//    msg_send_data_to_core1(MSG_TYPE_PARALLEL_NOTICE, MSG_NOTICE_IRQ, 0);
}

void fdc_common_clr_irq()
{
    // negative logic
    gpio_put(FDC_COMMON_IRQ_PIN, true);
}

bool fdc_common_now_irq()
{
    // negative logic
    return !gpio_get(FDC_COMMON_IRQ_PIN);
}

void MY_CORE1_FUNC(fdc_common_clr_irq_core1)()
{
    // negative logic
    gpio_put(FDC_COMMON_IRQ_PIN, true);
}

//--------------------------------------------------------------------
// drq
void fdc_common_set_drq()
{
#ifdef FDC_COMMON_DRQ_PIN
    // negative logic
    gpio_put(FDC_COMMON_DRQ_PIN, false);
#endif
}

void fdc_common_clr_drq()
{
#ifdef FDC_COMMON_DRQ_PIN
    // negative logic
    gpio_put(FDC_COMMON_DRQ_PIN, true);
#endif
}

bool fdc_common_now_drq()
{
#ifdef FDC_COMMON_DRQ_PIN
    // negative logic
    return !gpio_get(FDC_COMMON_DRQ_PIN);
#else
    return false;
#endif
}

//--------------------------------------------------------------------

uint8_t fdc_common_toggle_side_number(uint8_t drv, uint8_t sid)
{
    switch(dev->disk_type) {
    case DISK_TYPE_3INCH:
        sid = fdc_3inch_toggle_side_number(drv, sid);
        break;
    default:
        break;
    }
    disk_d88_set_side_number(drv, sid);
    return sid;
}

void fdc_common_set_side_number(uint8_t drv, uint8_t sid)
{
    fdc_3inch_set_side_number(drv, sid);
    disk_d88_set_side_number(drv, sid);
}

uint8_t fdc_common_get_side_number(uint8_t drv)
{
    switch(dev->disk_type) {
    case DISK_TYPE_3INCH:
        return fdc_3inch_get_side_number(drv);
        break;
    case DISK_TYPE_8INCH:
        return fdc_8inch_get_side_number();
        break;
    default:
        return fdc_5inch_get_side_number();
        break;
    }
}
