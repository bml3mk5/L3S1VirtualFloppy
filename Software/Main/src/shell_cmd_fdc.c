/** @file shell_cmd_fdc.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "shell_cmd_fdc.h"
#include "shell_cmd_fdc5.h"
#include "shell_cmd_fdc8.h"
#include "shell_cmd_fdc3.h"
#include "embedded_cli.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pico/stdio.h>
#include "fdc_common.h"

#ifndef IS_HOST_TEST
#include "config.h"
#include "fdc_5inch.h"
#include "fdc_8inch.h"
#include "fdc_3inch.h"
#include "disk_d88.h"
#else
#include "dumb_uart.h"
#endif
#include "shell_cmd.h"
#include "shell_cmd_cfg.h"
#include "simple_fifo.h"

shell_cmd_fdc_t g_shell_cmd_fdc;

static const char *c_disk_types[] = {
    "5inch 2D $FF00",
    "5inch 2D $FF10",
#ifdef _MBS1
    "5inch2HD $FFx0",
#else
    "8inch 2D $FFx0",
#endif
    "3inch 1S $FF18",
    NULL
};

typedef bool (*shell_cmd_fdc_motor_on_cb_t)(uint8_t drv, uint32_t sid_num, bool dden);
typedef void (*shell_cmd_fdc_motor_off_cb_t)(uint8_t drv);
typedef bool (*shell_cmd_fdc_restore_cb_t)(uint8_t drv, uint32_t step_rate, pftime_t *ptime);
typedef bool (*shell_cmd_fdc_seek_cb_t)(uint8_t drv, uint32_t trk_num, uint32_t step_rate, pftime_t *ptime);
typedef bool (*shell_cmd_fdc_step_cb_t)(uint8_t drv, int dir, uint32_t step_rate, pftime_t *ptime);
typedef bool (*shell_cmd_fdc_data_cb_t)(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime);
typedef bool (*shell_cmd_fdc_track_cb_t)(uint8_t drv, uint32_t sid_num, pftime_t *ptime);

typedef void (*shell_cmd_fdc_unitsel_cb_t)(uint8_t opts);
typedef void (*shell_cmd_fdc_command_cb_t)(void);

#if 0
extern shell_cmd_fdc_motor_on_cb_t fdc_motor_on;
extern shell_cmd_fdc_motor_off_cb_t fdc_motor_off;
extern shell_cmd_fdc_restore_cb_t fdc_restore;
extern shell_cmd_fdc_seek_cb_t fdc_seek;
extern shell_cmd_fdc_step_cb_t fdc_step;
extern shell_cmd_fdc_data_cb_t fdc_read_data;
extern shell_cmd_fdc_data_cb_t fdc_write_data;
extern shell_cmd_fdc_command_cb_t fdc_forceint;
extern shell_cmd_fdc_command_cb_t fdc_read_status;
extern shell_cmd_fdc_unitsel_cb_t fdc_unitsel;
extern shell_cmd_fdc_command_cb_t fdc_after_command;
#endif

shell_cmd_fdc_motor_on_cb_t fdc_motor_on = NULL;
shell_cmd_fdc_motor_off_cb_t fdc_motor_off = NULL;
shell_cmd_fdc_restore_cb_t fdc_restore = NULL;
shell_cmd_fdc_seek_cb_t fdc_seek = NULL;
shell_cmd_fdc_step_cb_t fdc_step = NULL;
shell_cmd_fdc_data_cb_t fdc_read_data = NULL;
shell_cmd_fdc_data_cb_t fdc_read_data_2 = NULL;
shell_cmd_fdc_data_cb_t fdc_read_data_3 = NULL;
shell_cmd_fdc_data_cb_t fdc_write_data = NULL;
shell_cmd_fdc_data_cb_t fdc_write_data_2 = NULL;
shell_cmd_fdc_track_cb_t fdc_read_track = NULL;
shell_cmd_fdc_command_cb_t fdc_forceint = NULL;
shell_cmd_fdc_command_cb_t fdc_read_status = NULL;
shell_cmd_fdc_unitsel_cb_t fdc_unitsel = NULL;
shell_cmd_fdc_command_cb_t fdc_after_command = NULL;

void shell_cmd_fdc_init()
{
    g_shell_cmd_fdc.drv = 0;
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        g_shell_cmd_fdc.trk[drv] = 0;
    }
    g_shell_cmd_fdc.verbose = false;
    g_shell_cmd_fdc.verbose_force = false;
    g_shell_cmd_fdc.buffer.count = 0;

#ifndef IS_HOST_TEST
    shell_cmd_fdc_change_disk_type(config_get_disk_type());
#else
    shell_cmd_fdc_change_disk_type(DISK_TYPE_5INCH_0);
#endif
}

void shell_cmd_fdc_change_disk_type(uint8_t type)
{
    switch(type) {
    case DISK_TYPE_3INCH:
        fdc_motor_on = &fdc3inch_motor_on;
        fdc_motor_off = &fdc3inch_motor_off;
        fdc_restore = &fdc3inch_restore;
        fdc_seek = &fdc3inch_seek;
        fdc_step = &fdc3inch_step;
        fdc_read_data = &fdc3inch_read_data;
        fdc_read_data_2 = &fdc3inch_read_data;
        fdc_read_data_3 = &fdc3inch_read_data;
        fdc_write_data = &fdc3inch_write_data;
        fdc_write_data_2 = &fdc3inch_write_data;
        fdc_read_track = &fdc3inch_read_track;
        fdc_forceint = &fdc3inch_forceint;
        fdc_read_status = &fdc3inch_read_status;
        fdc_unitsel = &fdc3inch_unitsel;
        fdc_after_command = &fdc3inch_after_command;
        break;
    case DISK_TYPE_8INCH:
        fdc_motor_on = &fdc8inch_motor_on;
        fdc_motor_off = &fdc8inch_motor_off;
        fdc_restore = &fdc8inch_restore;
        fdc_seek = &fdc8inch_seek;
        fdc_step = &fdc8inch_step;
        fdc_read_data = &fdc8inch_read_data;
        fdc_read_data_2 = &fdc8inch_read_data_2;
        fdc_read_data_3 = &fdc8inch_read_data_3;
        fdc_write_data = &fdc8inch_write_data;
        fdc_write_data_2 = &fdc8inch_write_data_2;
        fdc_read_track = &fdc8inch_read_track;
        fdc_forceint = &fdc8inch_forceint;
        fdc_read_status = &fdc8inch_read_status;
        fdc_unitsel = &fdc8inch_unitsel;
        fdc_after_command = &fdc8inch_after_command;
        break;
    default:
        fdc_motor_on = &fdc5inch_motor_on;
        fdc_motor_off = &fdc5inch_motor_off;
        fdc_restore = &fdc5inch_restore;
        fdc_seek = &fdc5inch_seek;
        fdc_step = &fdc5inch_step;
        fdc_read_data = &fdc5inch_read_data;
        fdc_read_data_2 = &fdc5inch_read_data;
        fdc_read_data_3 = &fdc5inch_read_data;
        fdc_write_data = &fdc5inch_write_data;
        fdc_write_data_2 = &fdc5inch_write_data;
        fdc_read_track = &fdc5inch_read_track;
        fdc_forceint = &fdc5inch_forceint;
        fdc_read_status = &fdc5inch_read_status;
        fdc_unitsel = &fdc5inch_unitsel;
        fdc_after_command = &fdc5inch_after_command;
        break;
    }
    fdc_common_set_disk_type(type);
}

void shell_cmd_fdc_dump_data(shell_cmd_fdc_buffer_t *buff)
{
    uint32_t pos = 0;
    uint32_t n;
    uint32_t count = buff->count;
    if (count >= sizeof(buff->data)) count = sizeof(buff->data);
    for(; pos<count; pos+=16) {
        printf("0x%04x:", pos);
        for(n=0; n<16; n++) {
            printf(" %02x", buff->data[pos+n]);
        }
        printf(" ");
        for(n=0; n<16; n++) {
            uint8_t c = buff->data[pos+n];
            printf("%c", c >= 0x20 && c < 0x80 ? c : '.');
        }
        printf("\n");
    }
}

//======================================================================

static const char *errcodes[] = {
    "BUSY",
    "SEEKERR",
    "RECNFND",
    "LOSTDATA",
    "CRCERR",
    "DELETED",
    "WRITEP",
    "NOTRDY",
    NULL
};

void shell_cmd_fdc_after_command(uint32_t sts, uint32_t count, pftime_t *ptime)
{
    printf("CMD_FDC: Done Sts:%08x Count:%d\n", sts, count);
    int i;
    uint32_t mask = 1;
    if (sts) {
        for(i=0; errcodes[i] != NULL; i++) {
            if (sts & mask) {
                printf(" %s", errcodes[i]);
            }
            mask <<= 1;
        }
        printf("\n");
    }
    if (ptime) {
        printf("Time:\n");
        for(i=0; i<(int)ptime->count; i++) {
            if (i == 0) {
                printf(" %3d:%2uus", i, ptime->data[i]);
            } else {
                printf(" %3d:%2uus", i, ptime->data[i] - ptime->data[i-1]);
            }
            if ((i & 7) == 7) printf("\n");
        }
        if ((i & 7) != 7) printf("\n");
    }
    fdc_after_command();
}

bool shell_cmd_fdc_check_status(uint32_t sts, uint32_t mask)
{
    int i;
    sts &= mask;
    if (sts) {
        printf("Error sts:%08x :", sts);
        mask = 1;
        for(i=0; errcodes[i] != NULL; i++) {
            if (sts & mask) {
                printf(" %s", errcodes[i]);
            }
            mask <<= 1;
        }
        printf("\n");
    }
    return (sts != 0);
}

//======================================================================

static pftime_t processing_time;

static const char *c_fdc_commands[] = {
    "type",
    "drive",
    "restore",
    "seek",
    "stepin",
    "stepout",
    "read",
    "read2",
    "read3",
    "write",
    "write2",
    "readtrk",
    "writetrk",
    "readaddr",
    "forceint",
    "readstat",
    "unitsel",
    "info",
    "verbose",
    NULL
};
enum en_fdc_commands {
    FDC_COMMAND_TYPE = 0,
    FDC_COMMAND_DRIVE,
    FDC_COMMAND_RESTORE,
    FDC_COMMAND_SEEK,
    FDC_COMMAND_STEPIN,
    FDC_COMMAND_STEPOUT,
    FDC_COMMAND_READDATA,
    FDC_COMMAND_READDATA2,
    FDC_COMMAND_READDATA3,
    FDC_COMMAND_WRITEDATA,
    FDC_COMMAND_WRITEDATA2,
    FDC_COMMAND_READTRACK,
    FDC_COMMAND_WRITETRACK,
    FDC_COMMAND_READADDR,
    FDC_COMMAND_FORCEINT,
    FDC_COMMAND_READSTAT,
    FDC_COMMAND_UNITSEL,
    FDC_COMMAND_INFO,
    FDC_COMMAND_VERBOSE,
};

static void fdc_cmd_usage_header(const char *cmd, const char *msg)
{
    shell_cmd_usage_header("fdc", cmd, msg);
}

static void fdc_cmd_usage()
{
    fdc_cmd_usage_header("<command>", "[<arg> ...]\n  Supported Commands:\n");
    for(int i=0; c_fdc_commands[i]; i++) {
        printf("    %s\n", c_fdc_commands[i]);
    }
}

static void fdc_cmd_type_usage(int cmd_type)
{
    if (cmd_type == 1) {
        cfg_cmd_disk_type_usage();
    } else {
        fdc_cmd_usage_header(c_fdc_commands[FDC_COMMAND_TYPE], "<fdc type>\n");
    }
    printf(" fdc type:\n");
    for(int i=0; i<4; i++) {
        if (i == DISK_TYPE_5INCH_1) continue;
        printf("  %d (%s)\n",i, c_disk_types[i]);
    }
}

void fdc_cmd_type_main(uint16_t argc, const char *args, int cmd_type)
{
    uint32_t disk_type = fdc_common_get_disk_type();
    if (argc > 2 || (argc == 2 && shell_cmd_is_help_option(2, args))) {
        fdc_cmd_type_usage(cmd_type);
        return;
    } else if (argc < 2) {
#ifndef IS_HOST_TEST
        printf(" Current type is %u (%s) on the board.\n",
            disk_type,
            c_disk_types[disk_type]);
#else
        cli_put_args(cmd_type == 1 ? "cfg" : "fdc", args, argc);
        printf(" Current type is %u (%s) on the host test.\n",
            disk_type,
            c_disk_types[disk_type]);
#endif
        return;
    }
    bool rc = shell_cmd_get_uint32(args, 2, &disk_type);
    if (!(rc && (0 <= disk_type && disk_type < 4 && disk_type != DISK_TYPE_5INCH_1))) {
        fdc_cmd_type_usage(cmd_type);
        return;
    }
#ifndef IS_HOST_TEST
    config_set_disk_type(disk_type);
    config_flash_save();
    printf("Set to %u (%s) on the board.\n", disk_type, c_disk_types[disk_type]);
#else
    shell_cmd_fdc_change_disk_type(disk_type);
    cli_put_args("fdc", args, argc);
    printf("Set to %u (%s) on the host test.\n", disk_type, c_disk_types[disk_type]);
#endif
}

static void fdc_cmd_type(uint16_t argc, char *args)
{
    fdc_cmd_type_main(argc, args, 0);
}

static void fdc_cmd_drive_usage()
{
    fdc_cmd_usage_header(c_fdc_commands[FDC_COMMAND_DRIVE], "<drive number>\n");
}

static void fdc_cmd_drive(uint16_t argc, char *args)
{
    uint32_t drv_num = 0;

    if ( argc < 2 || shell_cmd_is_help_option(2, args)) {
        fdc_cmd_drive_usage();
        return;
    }
    if (!shell_cmd_get_uint32(args, 2, &drv_num)) {
        fdc_cmd_drive_usage();
        return;
    }
    g_shell_cmd_fdc.drv = (uint8_t)drv_num;
}

static void fdc_cmd_step_rate_usage()
{
    switch(fdc_common_get_disk_type()) {
    case 3:
        fdc3inch_step_rate_usage();
        break;
    default:
        fdc5inch_step_rate_usage();
        break;
    }
}

static void fdc_cmd_restore_usage()
{
    fdc_cmd_usage_header(c_fdc_commands[FDC_COMMAND_RESTORE], "[<step_rate>]\n");
    fdc_cmd_step_rate_usage();
}

static void fdc_cmd_restore(uint16_t argc, char *args)
{
    uint32_t step_rate = 0x03;
    char *err = NULL;

    if ( argc >= 2 ) {
        if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &step_rate)) {
            fdc_cmd_restore_usage();
            return;
        }
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, 0, false)) {
        return;
    }
    // restore
    fdc_restore(g_shell_cmd_fdc.drv, step_rate, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, 0, &processing_time);
}

static void fdc_cmd_seek_usage()
{
    fdc_cmd_usage_header(c_fdc_commands[FDC_COMMAND_SEEK], "<track number> [<step_rate>]\n");
    printf(" Track number: 0 - 39\n");
    fdc_cmd_step_rate_usage();
}

static void fdc_cmd_seek(uint16_t argc, char *args)
{
    uint32_t trk_num;
    uint32_t step_rate = 0x03;

    if ( argc < 2 ) {
        fdc_cmd_seek_usage();
        return;
    }
    if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &trk_num)) {
        fdc_cmd_seek_usage();
        return;
    }
    if ( argc >= 3 ) {
        if (!shell_cmd_get_uint32(args, 3, &step_rate)) {
            fdc_cmd_seek_usage();
            return;
        }
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, 0, false)) {
        return;
    }
    // seek
    fdc_seek(g_shell_cmd_fdc.drv, trk_num, step_rate, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, 0, &processing_time);
}

static void fdc_cmd_step_usage(int cmd)
{
    fdc_cmd_usage_header(c_fdc_commands[cmd], "[<step_rate>]\n");
    fdc_cmd_step_rate_usage();
}

static void fdc_cmd_step(uint16_t argc, char *args, int cmd)
{
    int dir = cmd == FDC_COMMAND_STEPIN ? 1 : -1;
    uint32_t step_rate = 0x03;

    if ( argc >= 2 ) {
        if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &step_rate)) {
            fdc_cmd_step_usage(cmd);
            return;
        }
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, 0, false)) {
        return;
    }
    // step in/out
    fdc_step(g_shell_cmd_fdc.drv, dir, step_rate, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, 0, &processing_time);
}

static void fdc_cmd_read_write_data_usage(int cmd)
{
    fdc_cmd_usage_header(c_fdc_commands[cmd], "<sector number> [<side number> [<single density>]]\n");
    printf(" sector number: 1 - 16\n");
    printf(" side number: 0 or 1\n");
    printf(" single density: set 0 when single density\n");
}

static void fdc_cmd_read_data(uint16_t argc, char *args)
{
    bool rc;
    int cmd = FDC_COMMAND_READDATA;
    uint32_t sec_num = 1;
    uint32_t sid_num = 0;
    bool dden = (fdc_common_get_disk_type() != DISK_TYPE_3INCH);

    if ( argc < 2 ) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &sec_num)) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if ( argc >= 3 ) {
        if (!shell_cmd_get_uint32(args, 3, &sid_num)) {
            fdc_cmd_read_write_data_usage(cmd);
            return;
        }
    }
    if ( argc >= 4 ) {
        dden = shell_cmd_get_bool(args, 4);
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, sid_num, dden)) {
        return;
    }
    // read data
    rc = fdc_read_data(g_shell_cmd_fdc.drv, sid_num, sec_num, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, g_shell_cmd_fdc.buffer.count, &processing_time);
    shell_cmd_fdc_dump_data(&g_shell_cmd_fdc.buffer);
}

static void fdc_cmd_read_data_2(uint16_t argc, char *args)
{
    bool rc;
    int cmd = FDC_COMMAND_READDATA;
    uint32_t sec_num = 1;
    uint32_t sid_num = 0;
    bool dden = (fdc_common_get_disk_type() != DISK_TYPE_3INCH);

    if ( argc < 2 ) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &sec_num)) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if ( argc >= 3 ) {
        if (!shell_cmd_get_uint32(args, 3, &sid_num)) {
            fdc_cmd_read_write_data_usage(cmd);
            return;
        }
    }
    if ( argc >= 4 ) {
        dden = shell_cmd_get_bool(args, 4);
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, sid_num, dden)) {
        return;
    }
    // read data
    rc = fdc_read_data_2(g_shell_cmd_fdc.drv, sid_num, sec_num, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, g_shell_cmd_fdc.buffer.count, &processing_time);
    shell_cmd_fdc_dump_data(&g_shell_cmd_fdc.buffer);
}

static void fdc_cmd_read_data_3(uint16_t argc, char *args)
{
    bool rc;
    int cmd = FDC_COMMAND_READDATA;
    uint32_t sec_num = 1;
    uint32_t sid_num = 0;
    bool dden = (fdc_common_get_disk_type() != DISK_TYPE_3INCH);

    if ( argc < 2 ) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &sec_num)) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if ( argc >= 3 ) {
        if (!shell_cmd_get_uint32(args, 3, &sid_num)) {
            fdc_cmd_read_write_data_usage(cmd);
            return;
        }
    }
    if ( argc >= 4 ) {
        dden = shell_cmd_get_bool(args, 4);
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, sid_num, dden)) {
        return;
    }
    // read data
    rc = fdc_read_data_3(g_shell_cmd_fdc.drv, sid_num, sec_num, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, g_shell_cmd_fdc.buffer.count, &processing_time);
    shell_cmd_fdc_dump_data(&g_shell_cmd_fdc.buffer);
}

static void fdc_cmd_write_data(uint16_t argc, char *args)
{
    bool rc;
    int cmd = FDC_COMMAND_WRITEDATA;
    uint32_t sec_num = 1;
    uint32_t sid_num = 0;
    bool dden = (fdc_common_get_disk_type() != DISK_TYPE_3INCH);
    char *err = NULL;

    if ( argc < 2 ) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &sec_num)) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if ( argc >= 3 ) {
        if (!shell_cmd_get_uint32(args, 3, &sid_num)) {
            fdc_cmd_read_write_data_usage(cmd);
            return;
        }
    }
    if ( argc >= 4 ) {
        dden = shell_cmd_get_bool(args, 4);
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, sid_num, dden)) {
        return;
    }
    // disp buffer data
    shell_cmd_fdc_dump_data(&g_shell_cmd_fdc.buffer);
    // write data
    rc = fdc_write_data(g_shell_cmd_fdc.drv, sid_num, sec_num, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, g_shell_cmd_fdc.buffer.count, &processing_time);
}

static void fdc_cmd_write_data_2(uint16_t argc, char *args)
{
    bool rc;
    int cmd = FDC_COMMAND_WRITEDATA;
    uint32_t sec_num = 1;
    uint32_t sid_num = 0;
    bool dden = (fdc_common_get_disk_type() != DISK_TYPE_3INCH);
    char *err = NULL;

    if ( argc < 2 ) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &sec_num)) {
        fdc_cmd_read_write_data_usage(cmd);
        return;
    }
    if ( argc >= 3 ) {
        if (!shell_cmd_get_uint32(args, 3, &sid_num)) {
            fdc_cmd_read_write_data_usage(cmd);
            return;
        }
    }
    if ( argc >= 4 ) {
        dden = shell_cmd_get_bool(args, 4);
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, sid_num, dden)) {
        return;
    }
    // disp buffer data
    shell_cmd_fdc_dump_data(&g_shell_cmd_fdc.buffer);
    // write data
    rc = fdc_write_data_2(g_shell_cmd_fdc.drv, sid_num, sec_num, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, g_shell_cmd_fdc.buffer.count, &processing_time);
}

static void fdc_cmd_read_write_track_usage(int cmd)
{
    fdc_cmd_usage_header(c_fdc_commands[cmd], "Usage: fdc %s <side number> [<single density>]\n");
    printf(" side number: 0 or 1\n");
    printf(" single density: set 0 when single density\n");
}

static void fdc_cmd_read_track(uint16_t argc, char *args)
{
    bool rc;
    int cmd = FDC_COMMAND_READTRACK;
    uint32_t sid_num = 0;
    bool dden = (fdc_common_get_disk_type() != DISK_TYPE_3INCH);

    if ( argc < 2 ) {
        fdc_cmd_read_write_track_usage(cmd);
        return;
    }
    if (shell_cmd_is_help_option(2, args) || !shell_cmd_get_uint32(args, 2, &sid_num)) {
        fdc_cmd_read_write_track_usage(cmd);
        return;
    }
    if ( argc >= 3 ) {
        dden = shell_cmd_get_bool(args, 3);
    }

    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, sid_num, dden)) {
        return;
    }
    // read data
    rc = fdc_read_track(g_shell_cmd_fdc.drv, sid_num, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, g_shell_cmd_fdc.buffer.count, &processing_time);
    shell_cmd_fdc_dump_data(&g_shell_cmd_fdc.buffer);
}

static void fdc_cmd_read_address_usage(int cmd)
{
    printf("NOTE: This command is able to execute on 5inch 2D only.\n");
    fdc_cmd_usage_header(c_fdc_commands[cmd], "[<single density>]\n");
    printf(" single density: set 0 when single density\n");
}

static void fdc_cmd_read_address(uint16_t argc, char *args)
{
    bool rc;
    int cmd = FDC_COMMAND_READADDR;
    uint32_t sid_num = 0;
    bool dden = (fdc_common_get_disk_type() != DISK_TYPE_3INCH);
    if (fdc_common_get_disk_type() != DISK_TYPE_5INCH_0) {
        fdc_cmd_read_address_usage(cmd);
        return;
    }
    if ( argc >= 2 ) {
        if (shell_cmd_is_help_option(2, args)) {
            fdc_cmd_read_address_usage(cmd);
            return;
        }
        dden = shell_cmd_get_bool(args, 2);
    }
    g_shell_cmd_fdc.verbose = true;
    // motor on
    if (!fdc_motor_on(g_shell_cmd_fdc.drv, sid_num, dden)) {
        return;
    }
    // read data
    rc = fdc5inch_read_addr(g_shell_cmd_fdc.drv, &processing_time);

    // motor off
    fdc_motor_off(g_shell_cmd_fdc.drv);
    shell_cmd_fdc_after_command(g_shell_cmd_fdc.status, g_shell_cmd_fdc.buffer.count, &processing_time);
    shell_cmd_fdc_dump_data(&g_shell_cmd_fdc.buffer);
}

static void fdc_cmd_forceint()
{
    fdc_forceint();
}

static void fdc_cmd_readstat()
{
    fdc_read_status();
}

static void fdc_cmd_unitsel_usage()
{
    fdc_cmd_usage_header(c_fdc_commands[FDC_COMMAND_UNITSEL], "<hexa value>\n");
}

static void fdc_cmd_unitsel(uint16_t argc, char *args)
{
    char *err = NULL;

    if ( argc < 2 || shell_cmd_is_help_option(2, args)) {
        fdc_cmd_unitsel_usage();
        return;
    }
    uint8_t opts = (uint8_t)strtol(embeddedCliGetToken(args, 2), &err, 16);
    if (err && *err != 0x20 && *err != 0) {
        fdc_cmd_unitsel_usage();
        return;
    }
    fdc_unitsel(opts);
}

static void fdc_cmd_info(uint16_t argc, char *args)
{
#ifndef IS_HOST_TEST
    fdc_5inch_get_info();
    fdc_8inch_get_info();
    fdc_3inch_get_info();
#else
    cli_put_args("fdc", args, argc);
#endif
}

static void fdc_cmd_verbose_usage()
{
    fdc_cmd_usage_header(c_fdc_commands[FDC_COMMAND_VERBOSE], "<on 1/off 0>\n");
}

static void fdc_cmd_verbose(uint16_t argc, char *args)
{
    if ( argc < 2 || shell_cmd_is_help_option(2, args)) {
        fdc_cmd_verbose_usage();
        return;
    }
    g_shell_cmd_fdc.verbose_force = shell_cmd_get_bool(args, 2);
}

void fdc_cmd(char *args)
{
    int rc = 0;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // need at least 1 argument
    if ( argc == 0 ) {
        fdc_cmd_usage();
        return;
    }

    const char *ccmd = embeddedCliGetToken(args, 1);

    int match = -1;
    for(int i=0; c_fdc_commands[i]; i++) {
        if (strcasecmp(ccmd, c_fdc_commands[i]) != 0) {
        continue;
        }
        match = i;
        switch(i) {
        case FDC_COMMAND_TYPE:
            // change fdc type
            fdc_cmd_type(argc, args);
            break;

        case FDC_COMMAND_DRIVE:
            // drive
            fdc_cmd_drive(argc, args);
            break;

        case FDC_COMMAND_RESTORE:
            // restore
            fdc_cmd_restore(argc, args);
            break;

        case FDC_COMMAND_SEEK:
            // seek
            fdc_cmd_seek(argc, args);
            break;

        case FDC_COMMAND_STEPIN:
        case FDC_COMMAND_STEPOUT:
            // step in
            // step out
            fdc_cmd_step(argc, args, i);
            break;

        case FDC_COMMAND_READDATA:
            // read data
            fdc_cmd_read_data(argc, args);
            break;

        case FDC_COMMAND_READDATA2:
            // read data (pattern 2)
            fdc_cmd_read_data_2(argc, args);
            break;

        case FDC_COMMAND_READDATA3:
            // read data (pattern 3)
            fdc_cmd_read_data_3(argc, args);
            break;

        case FDC_COMMAND_WRITEDATA:
            // write data
            fdc_cmd_write_data(argc, args);
            break;

        case FDC_COMMAND_WRITEDATA2:
            // write data (pattern 2)
            fdc_cmd_write_data_2(argc, args);
            break;

        case FDC_COMMAND_READTRACK:
            // read track
            fdc_cmd_read_track(argc, args);
            break;

        case FDC_COMMAND_READADDR:
            // read address
            fdc_cmd_read_address(argc, args);
            break;

        case FDC_COMMAND_FORCEINT:
            // forceint
            fdc_cmd_forceint();
            break;

        case FDC_COMMAND_READSTAT:
            // read status
            fdc_cmd_readstat();
            break;

        case FDC_COMMAND_UNITSEL:
            // write to unitsel
            fdc_cmd_unitsel(argc, args);
            break;

        case FDC_COMMAND_INFO:
            // read status
            fdc_cmd_info(argc, args);
            break;

        case FDC_COMMAND_VERBOSE:
            // verbose
            fdc_cmd_verbose(argc, args);
            break;

        default:
            break;
        }
    }
    if (match < 0) {
        fdc_cmd_usage();
    }
}

//======================================================================

bool split_filename(const char *file_name, char *name, char *ext)
{
    int len;
    if (!file_name) return false;
    char *p = strchr(file_name, '.');
    if (p) {
        len = (int)strlen(p+1);
        if (len > 3) len = 3;
        memcpy(ext, p+1, len);
        len = (int)(p - file_name);
    } else {
        len = (int)strlen(file_name);
    }
    if (len > 8) len = 8;
    memcpy(name, file_name, len);
    return true;
}

//======================================================================

uint8_t bas_fat_table[256];

void bas_cmd_files(uint32_t drv)
{
    switch(fdc_common_get_disk_type()) {
    case DISK_TYPE_8INCH:
        fdc8inch_cmd_files(drv);
        break;
    case DISK_TYPE_3INCH:
        fdc3inch_cmd_files(drv);
        break;
    default:
        fdc5inch_cmd_files(drv);
        break;
    }
}

void bas_cmd_load(uint32_t drv, const char *file_name)
{
    switch(fdc_common_get_disk_type()) {
    case DISK_TYPE_8INCH:
        fdc8inch_cmd_load(drv, file_name);
        break;
    case DISK_TYPE_3INCH:
        fdc3inch_cmd_load(drv, file_name);
        break;
    default:
        fdc5inch_cmd_load(drv, file_name);
        break;
    }
}

bool bas_read_fat_table(uint8_t drv, const bas_fat_table_t *inf, uint8_t *buffer, int buffer_size)
{
    bool rc = true;
    do {
        // motor on
        rc = fdc_motor_on(drv, 0, inf->dden);
        if (!rc) {
            break;
        }
        // seek
        rc = fdc_seek(drv, inf->trk_num, inf->step_rate, NULL);
        if (!rc) {
            printf("BAS_FDC: read_fat_table seek failed.\n");
            break;
        }
        // read FAT table
        uint8_t *buff_pos = buffer;
        int buff_siz = 0;
        for(uint32_t sec_num=inf->start_sec_num; sec_num<=inf->end_sec_num; sec_num++) {
            rc = fdc_read_data(drv, 0, sec_num, NULL);
            if (!rc) {
                printf("BAS_FDC: read_fat_table read failed.\n");
                break;
            } else {
                int siz = g_shell_cmd_fdc.buffer.count;
                if (buff_siz + siz >= buffer_size) {
                    siz = buffer_size - buff_siz;
                }
                memcpy(buff_pos, g_shell_cmd_fdc.buffer.data, siz);
                buff_pos += siz;
            }
        }
    } while(0);
    return rc;
}

bool bas_read_directory(uint8_t drv, const bas_dir_table_t *inf, directory_entry_cb_t callback_e, directory_finish_cb_t callback_f, void *user_data)
{
    bool rc = true;
    int row;
    bool last = false;

    row = 0;
    for(int sec = inf->start_sec_num; sec <= inf->end_sec_num && !last; sec++) {
        fdc_motor_on(drv, sec / inf->secs_per_trk, inf->dden);
        rc = fdc_read_data(drv, sec / inf->secs_per_trk, (sec % inf->secs_per_trk) + 1, NULL);
        if (!rc) {
            printf("BAS_DIR: Record Not Found.\n");
            break;
        }
        for(int pos=0; pos<g_shell_cmd_fdc.buffer.count && !last; pos+=32) {
            if (g_shell_cmd_fdc.buffer.data[pos] == 0xff) {
                // unused entry
                last = true;
                break;
            }
            else if (g_shell_cmd_fdc.buffer.data[pos] == 0x00) {
                // deleted entry
                continue;
            }
            if (callback_e) {
                last = callback_e(row, &g_shell_cmd_fdc.buffer.data[pos], user_data);
            }
            row++;
        }
    }
    if (callback_f) {
        callback_f(row, user_data);
    }
    return rc;
}

//======================================================================

bool fdc_cmd_boottest(int boot_type)
{
    bool rc = false;
    switch(fdc_common_get_disk_type()) {
    case DISK_TYPE_3INCH:
        rc = fdc3inch_cmd_boottest(boot_type);
        break;
    case DISK_TYPE_8INCH:
        rc = fdc8inch_cmd_boottest(boot_type);
        break;
    default:
        rc = fdc5inch_cmd_boottest(boot_type);
        break;
    }
    return rc;
}

//======================================================================

int cli_d88_get_data(int drv, char *data, int size)
{
    char args[16];
    int argc = 2;
    int pos = 0;
    int ret = -1;
    bool valid = false;

#ifdef IS_HOST_TEST
    dumb_uart_set_redirect(true);
    sprintf(args, "%d getdata", drv);
    cli_put_args("d88", args, argc);
#else
    disk_d88_get_data(drv);
#endif
    while(pos < size) {
#ifdef IS_HOST_TEST
        ret = dumb_uart_getchar_timeout_us(5000000);
#else
        ret = getchar_timeout_us(5000000);
#endif
        if (ret < 0 || ret == '$') {
            valid = false;
            break;
        } else if (ret == '^') {
            valid = true;
            continue;
        } else if (ret <= 0x20) {
            continue;
        }
        if (valid) {
            data[pos] = (char)ret;
            pos++;
        }
    }
#ifdef IS_HOST_TEST
    dumb_uart_set_redirect(false);
#endif
    return (ret < 0 ? -1 : pos);
}

uint8_t str_to_uint8(const char *str)
{
    char buf[4];
    buf[0] = str[0];
    buf[1] = str[1];
    buf[2] = 0;
    long val = strtol(buf, NULL, 16);
    return (uint8_t)val;
}

bool cli_d88_check_data(int drv, const uint8_t *dst, uint32_t trk_num, uint32_t sec_num, uint32_t sec_size, uint32_t sts, uint32_t xaddr)
{
    uint32_t origsize = sizeof(char) * (sec_size + 4) * 2;
    char *origdata = malloc(origsize);
    if (!origdata) {
        return false;
    }
    uint8_t srcdata, dstdata;
    bool rc = true;
    printf("Read check on trk:%u sec:%u siz:%u sts:0x%02x addr:0x%04x\n", trk_num, sec_num, sec_size, sts, xaddr);
    cli_d88_get_data(drv, origdata, origsize);
    for(uint32_t i=0; i<sec_size; i++) {
        srcdata = str_to_uint8(&origdata[i*2]);
        dstdata = dst[i];
        if (srcdata != dstdata) {
            printf("Diff pos:0x%02x s:0x%02x != d:0x%02x\n", i, srcdata, dstdata);
            rc = false;
        }
    }
    free(origdata);
    return rc;

}
