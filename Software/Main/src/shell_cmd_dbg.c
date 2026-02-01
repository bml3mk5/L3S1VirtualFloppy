/** @file shell_cmd_dbg.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "shell_cmd_dbg.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "embedded_cli.h"
//#include "disk_drive.h"
//#include "disk_d88.h"
//#include "fdc_common.h"
//#include "main.h"
#include "common.h"
#include "shell_cmd.h"
#include "pio_ctrls.h"
#include "config.h"
#include "display_storage.h"
#include "event.h"

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

static const char *c_dbg_commands[] = {
    "lcd",
    "cfg",
    "regs",
    "event",
    NULL
};
enum en_dbg_commands {
    DBG_COMMAND_LCD = 0,
    DBG_COMMAND_CFG,
    DBG_COMMAND_REGS,
    DBG_COMMAND_EVENT,
};

static void dbg_cmd_usage()
{
    printf("Usage: dbg <command> [<arg> ...]\n  Supported Commands:\n");
    for(int i=0; c_dbg_commands[i]; i++) {
        printf("    %s\n", c_dbg_commands[i]);
    }
}

static void dbg_cmd_lcd(uint16_t argc, const char *args)
{
    display_storage_lcd_debug_info();
}

static void dbg_cmd_cfg(uint16_t argc, const char *args)
{
    config_debug_info();
}

static void dbg_cmd_regs(uint16_t argc, const char *args)
{
    pio_ctrls_debug_read_regs();
}

static void dbg_cmd_event(uint16_t argc, const char *args)
{
    event_info();
}

void dbg_cmd(char *args)
{
    uint16_t argc = embeddedCliGetTokenCount(args);

    // need at least 1 argument
    if ( argc == 0 ) {
        dbg_cmd_usage();
        return;
    }
    const char *ccmd = embeddedCliGetToken(args, 1);

    int match = -1;
    for(int i=0; c_dbg_commands[i]; i++) {
        if (strcasecmp(ccmd, c_dbg_commands[i]) != 0) {
            continue;
        }
        match = i;
        switch(i) {
        case DBG_COMMAND_LCD:
            // lcd info
            dbg_cmd_lcd(argc, args);
            break;

        case DBG_COMMAND_CFG:
            // config info
            dbg_cmd_cfg(argc, args);
            break;

        case DBG_COMMAND_REGS:
            // register on core1
            dbg_cmd_regs(argc, args);
            break;

        case DBG_COMMAND_EVENT:
            // event info
            dbg_cmd_event(argc, args);
            break;

        default:
            break;
        }
    }
    if (match < 0) {
        dbg_cmd_usage();
    }
}
