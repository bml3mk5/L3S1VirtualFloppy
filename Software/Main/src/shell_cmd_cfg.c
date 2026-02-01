/** @file shell_cmd_cfg.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "shell_cmd_cfg.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "embedded_cli.h"
//#include "disk_drive.h"
//#include "disk_d88.h"
//#include "fdc_common.h"
//#include "main.h"
#include "common.h"
#include "shell_cmd.h"
#include "shell_cmd_fdc.h"
//#include "pio_ctrls.h"
#ifndef IS_HOST_TEST
#include "config.h"
#endif
//#include "display_storage.h"
//#include "event.h"

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

static const char *c_cfg_commands[] = {
    "disk_type",
    "seek_track",
    "search_sector",
    "data_request",
    NULL
};
enum en_cfg_commands {
    CFG_COMMAND_DISK_TYPE = 0,
    CFG_COMMAND_SEEK_TRACK,
    CFG_COMMAND_SEARCH_SECTOR,
    CFG_COMMAND_DATA_REQUEST,
};

#ifndef IS_HOST_TEST
static const char *setting_list_calc_time[] = {
    "Calculate time",
    "Certain time",
    NULL
};

static const char *setting_list_data_reqtime[] = {
    "Calculate time",
    "As fast as    ",
    NULL
};
#endif

static void cfg_cmd_usage_header(const char *cmd, const char *msg)
{
    shell_cmd_usage_header("cfg", cmd, msg);
}

static void cfg_cmd_usage()
{
    cfg_cmd_usage_header("<command>", "[<arg> ...]\n  Supported Commands:\n");
    for(int i=0; c_cfg_commands[i]; i++) {
        printf("    %s\n", c_cfg_commands[i]);
    }
}

void cfg_cmd_disk_type_usage()
{
    cfg_cmd_usage_header(c_cfg_commands[CFG_COMMAND_DISK_TYPE], "<fdc type>\n");
}

static void cfg_cmd_disk_type(uint16_t argc, const char *args)
{
    fdc_cmd_type_main(argc, args, 1);
}

#ifndef IS_HOST_TEST
static void cfg_cmd_seek_track_usage()
{
    cfg_cmd_usage_header(c_cfg_commands[CFG_COMMAND_SEEK_TRACK], "<type>\nConsumed time to step in or out\n Type is:\n");
    for(int i=0; setting_list_calc_time[i]; i++) {
        printf("  %d...%s\n", setting_list_calc_time[i]);
    }
}
#endif

static void cfg_cmd_seek_track(uint16_t argc, const char *args)
{
#ifndef IS_HOST_TEST
    uint32_t val = config_get_seek_track();
    if (argc < 2) {
        printf("Current type is %d : %s\n", val, setting_list_calc_time[val]);
        return;
    }
    if (shell_cmd_is_help_option(2, args)) {
        cfg_cmd_seek_track_usage();
        return;
    }
    bool rc = shell_cmd_get_uint32(args, 2, &val);
    if (!(rc && val < 2)) {
        cfg_cmd_seek_track_usage();
        return;
    }
    config_set_seek_track(val);
    config_flash_save();
#else
    cli_put_args("cfg", args, argc);
#endif
}

#ifndef IS_HOST_TEST
static void cfg_cmd_search_sector_usage()
{
    cfg_cmd_usage_header(c_cfg_commands[CFG_COMMAND_SEARCH_SECTOR], "<type>\nConsumed time to search a sector\n Type is:\n");
    for(int i=0; setting_list_calc_time[i]; i++) {
        printf("  %d...%s\n", setting_list_calc_time[i]);
    }
}
#endif

static void cfg_cmd_search_sector(uint16_t argc, const char *args)
{
#ifndef IS_HOST_TEST
    uint32_t val = config_get_search_sector();
    if (argc < 2) {
        printf("Current type is %d : %s\n", val, setting_list_calc_time[val]);
        return;
    }
    if (shell_cmd_is_help_option(2, args)) {
        cfg_cmd_search_sector_usage();
        return;
    }
    bool rc = shell_cmd_get_uint32(args, 2, &val);
    if (!(rc && val < 2)) {
        cfg_cmd_search_sector_usage();
        return;
    }
    config_set_search_sector(val);
    config_flash_save();
#else
    cli_put_args("cfg", args, argc);
#endif
}

#ifndef IS_HOST_TEST
static void cfg_cmd_data_request_usage()
{
    cfg_cmd_usage_header(c_cfg_commands[CFG_COMMAND_DATA_REQUEST], "<type>\nConsumed time to request a data\n Type is:\n");
    for(int i=0; setting_list_data_reqtime[i]; i++) {
        printf("  %d...%s\n", setting_list_data_reqtime[i]);
    }
}
#endif

static void cfg_cmd_data_request(uint16_t argc, const char *args)
{
#ifndef IS_HOST_TEST
    uint32_t val = config_get_data_request();
    if (argc < 2) {
        printf("Current type is %d : %s\n", val, setting_list_data_reqtime[val]);
        return;
    }
    if (shell_cmd_is_help_option(2, args)) {
        cfg_cmd_data_request_usage();
        return;
    }
    bool rc = shell_cmd_get_uint32(args, 2, &val);
    if (!(rc && val < 2)) {
        cfg_cmd_data_request_usage();
        return;
    }
    config_set_data_request(val);
    config_flash_save();
#else
    cli_put_args("cfg", args, argc);
#endif
}

void cfg_cmd(char *args)
{
    uint16_t argc = embeddedCliGetTokenCount(args);

    // need at least 1 argument
    if ( argc == 0 ) {
        cfg_cmd_usage();
        return;
    }
    const char *ccmd = embeddedCliGetToken(args, 1);

    int match = -1;
    for(int i=0; c_cfg_commands[i]; i++) {
        if (strcasecmp(ccmd, c_cfg_commands[i]) != 0) {
            continue;
        }
        match = i;
        switch(i) {
        case CFG_COMMAND_DISK_TYPE:
            cfg_cmd_disk_type(argc, args);
            break;

        case CFG_COMMAND_SEEK_TRACK:
            cfg_cmd_seek_track(argc, args);
            break;

        case CFG_COMMAND_SEARCH_SECTOR:
            cfg_cmd_search_sector(argc, args);
            break;

        case CFG_COMMAND_DATA_REQUEST:
            cfg_cmd_data_request(argc, args);
            break;

        default:
            break;
        }
    }
    if (match < 0) {
        cfg_cmd_usage();
    }
}
