/** @file shell_cmd_d88.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "shell_cmd_d88.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "embedded_cli.h"
#include "disk_drive.h"
#include "disk_d88.h"
#include "fdc_common.h"
#include "main.h"
#include "display_storage.h"
#include "event.h"
#include "common.h"
#include "shell_cmd.h"

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

static const char *c_d88_commands[] = {
    "mount",
    "umount",
    "restore",
    "track",
    "sector",
    "info",
    "cat",
    "getdata",
    "getcrc",
    "verbose",
    NULL
};
enum en_d88_commands {
    D88_COMMAND_MOUNT = 0,
    D88_COMMAND_UMOUNT,
    D88_COMMAND_RESTORE,
    D88_COMMAND_TRACK,
    D88_COMMAND_SECTOR,
    D88_COMMAND_INFO,
    D88_COMMAND_CAT,
    D88_COMMAND_GETDATA,
    D88_COMMAND_GETCRC,
    D88_COMMAND_VERBOSE,
};

static void d88_cmd_usage_header(const char *msg)
{
    printf("Usage: d88 [<drive>] ");
    if (msg) printf(msg);
}

static void d88_cmd_usage()
{
    d88_cmd_usage_header("<command> [<arg> ...]\n  Supported Commands:\n");
    for(int i=0; c_d88_commands[i]; i++) {
        printf("    %s\n", c_d88_commands[i]);
    }
}

static void d88_cmd_mount(int drv, uint16_t argc, const char *args)
{
    if ( argc < 2 || shell_cmd_is_help_option(2, args)) {
        d88_cmd_usage_header("mount <d88 file path>\n");
        return;
    }
    const char* fpath = embeddedCliGetToken(args, 2);
    if (!display_d88_mount(drv, fpath)) {
        printf("Cannot mount d88 file '%s' on drive %d.\n", fpath, drv);
        return;
    }
    printf("Mounted d88 file.\n");
}

static void d88_cmd_unmount(int drv)
{
    display_d88_unmount(drv);
    printf("Unmounted d88 file.\n");
}

static void d88_cmd_restore(int drv)
{
    int rc = disk_d88_restore(drv);
    if (rc == FR_OK) {
        printf("Restored.\n");
    } else {
        printf("Cannot restore: code:%d\n", rc);
    }
}

static void d88_cmd_track(int drv, uint16_t argc, const char *args)
{
    int rc;
    if ( argc < 2 || shell_cmd_is_help_option(2, args)) {
        d88_cmd_usage_header("track <track number> [<side number>]\n");
        return;
    }
    int trk = (int)strtol(embeddedCliGetToken(args, 2), NULL, 10);
    int sid = 0;
    if ( argc < 3 ) {
        sid = (int)strtol(embeddedCliGetToken(args, 3), NULL, 10);
    }
    rc = disk_d88_step(drv, trk, sid);
    if (rc == FR_OK) {
        printf("Stepped to %d-%d.\n", trk, sid);
    } else {
        printf("Cannot step: code:%d\n", rc);
    }
}

static void d88_cmd_sector(int drv, uint16_t argc, const char *args)
{
    int rc;
    if ( argc < 2 || shell_cmd_is_help_option(2, args)) {
        d88_cmd_usage_header("sector <sector number> [<calc CRC>]\n");
        return;
    }
    int sec = (int)strtol(embeddedCliGetToken(args, 2), NULL, 10);
    bool calc_crc = false;
    if (argc >= 3) {
        calc_crc = shell_cmd_get_bool(args, 3);
    }
    int sec_pos = 0;
    rc = disk_d88_read_sector(drv, -1, -1, sec, -1, -1, calc_crc, &sec_pos);
    if (rc == FR_OK) {
        printf("Read sector %d (pos:%d) (crc:%s).\n", sec, sec_pos, calc_crc ? "on" : "off");
    } else {
        printf("Cannot read sector: code:%d\n", rc);
    }
}

static void d88_cmd_verbose(int drv, uint16_t argc, const char *args)
{
    int rc;
    if ( argc < 2 || shell_cmd_is_help_option(2, args)) {
        d88_cmd_usage_header("verbose <on 1/off 0>\n");
        return;
    }
    disk_d88_verbose_mode(shell_cmd_get_bool(args, 2));
}

void d88_cmd(char *args)
{
    int drv = 0;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // need at least 1 argument
    if ( argc == 0 ) {
        d88_cmd_usage();
        return;
    }
    const char *ccmd = embeddedCliGetToken(args, 1);
    const char *cargs = args;
    if (ccmd[0] >= 0x30 && ccmd[0] < MAX_DRIVES + 0x30) {
        // drive number
        if ( argc == 1 ) {
            d88_cmd_usage();
            return;
        }
        drv = ccmd[0] - 0x30;
        ccmd = embeddedCliGetToken(args, 2);
        cargs = ccmd; 
        argc--;
    }

    int match = -1;
    for(int i=0; c_d88_commands[i]; i++) {
        if (strcasecmp(ccmd, c_d88_commands[i]) != 0) {
            continue;
        }
        match = i;
        switch(i) {
        case D88_COMMAND_MOUNT:
            // mount
            d88_cmd_mount(drv, argc, cargs);
            break;

        case D88_COMMAND_UMOUNT:
            // unmount
            d88_cmd_unmount(drv);
            break;

        case D88_COMMAND_RESTORE:
            // restore
            d88_cmd_restore(drv);
            break;

        case D88_COMMAND_TRACK:
            // stepping track
            d88_cmd_track(drv, argc, cargs);
            break;

        case D88_COMMAND_SECTOR:
            // sector read
            d88_cmd_sector(drv, argc, cargs);
            break;

        case D88_COMMAND_INFO:
            // disk information
            disk_d88_info(drv);
            break;

        case D88_COMMAND_CAT:
            // cat sector data
            disk_d88_cat(drv);
            break;

        case D88_COMMAND_GETDATA:
            disk_d88_get_data(drv);
            break;

        case D88_COMMAND_GETCRC:
            disk_d88_get_crc(drv);
            break;

        case D88_COMMAND_VERBOSE:
            // verbose mode
            d88_cmd_verbose(drv, argc, args);
            break;

        default:
            break;
        }
    }
    if (match < 0) {
        d88_cmd_usage();
    }
}
