/** @file shell_cmd.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include <stdio.h>
#include <ctype.h>
#include <hardware/clocks.h>

#define EMBEDDED_CLI_IMPL
#include "shell_cmd.h"

#include "tusb.h"
#include "bsp/board.h"

#include "ff.h"
#include "diskio.h"

#include "disk_drive.h"
#include "disk_d88.h"
#include "fdc_common.h"
#include "main.h"
#include "display_storage.h"
#include "event.h"

#include "shell_cmd_fdc.h"
#include "shell_cmd_d88.h"
#include "shell_cmd_cfg.h"
#include "shell_cmd_dbg.h"

#include "utils.h"

//#include "config.h"

//#include "pio_ctrls.h"

//------------- embedded-cli -------------//
#define CLI_BUFFER_SIZE     768
#define CLI_RX_BUFFER_SIZE  64
#define CLI_CMD_BUFFER_SIZE 64
#define CLI_HISTORY_SIZE    32
#define CLI_BINDING_COUNT   18

EmbeddedCli *_cli;
static CLI_UINT cli_buffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];

static bool cli_init(void);

static void cli_cmd_cat(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_dump(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_cd(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_cp(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_ls(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_pwd(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_mkdir(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_mv(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_rm(EmbeddedCli *cli, char *args, void *context);

static void cli_cmd_clk(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_reset(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_boottest(EmbeddedCli *cli, char *args, void *context);

static void cli_cmd_files(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_load(EmbeddedCli *cli, char *args, void *context);

static void cli_cmd_d88(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_fdc(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_cfg(EmbeddedCli *cli, char *args, void *context);
static void cli_cmd_dbg(EmbeddedCli *cli, char *args, void *context);

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

bool shell_cmd_init(void)
{
    // disable stdout buffered for echoing typing command
    cli_init();

    shell_cmd_fdc_init();

    return true;
}

void shell_cmd_task(void)
{
    if (!_cli) return;

    int ch = board_getchar();
    if ( ch > 0 ) {
        while( ch > 0 ) {
            embeddedCliReceiveChar(_cli, (char) ch);
            ch = board_getchar();
        }
        embeddedCliProcess(_cli);
    }
}

bool shell_cmd_get_bool(const char *args, uint16_t pos)
{
    bool rc = false;
    const char *token = embeddedCliGetToken(args, pos);
    if (token) {
        rc = (strcasecmp(token, "on") == 0 || strcasecmp(token, "true") == 0 || strcmp(token, "1") == 0);
    }
    return rc;
}

bool shell_cmd_get_uint32(const char *args, uint16_t pos, uint32_t *val)
{
    char *err = NULL;
    *val = (uint32_t)(strtol(embeddedCliGetToken(args, pos), &err, 10));
    return (!(err && *err != 0x20 && *err != 0));
}

//--------------------------------------------------------------------+
// CLI Commands
//--------------------------------------------------------------------+

void cli_write_char(EmbeddedCli *cli, char c)
{
    (void) cli;
    putchar((int) c);
}

void cli_cmd_unknown(EmbeddedCli *cli, CliCommand *command)
{
    (void) cli;
    printf("%s: command not found\n", command->name);
}

bool cli_init(void)
{
    EmbeddedCliConfig *config = embeddedCliDefaultConfig();
    config->cliBuffer         = cli_buffer;
    config->cliBufferSize     = CLI_BUFFER_SIZE;
    config->rxBufferSize      = CLI_RX_BUFFER_SIZE;
    config->cmdBufferSize     = CLI_CMD_BUFFER_SIZE;
    config->historyBufferSize = CLI_HISTORY_SIZE;
    config->maxBindingCount   = CLI_BINDING_COUNT;

    if(embeddedCliRequiredSize(config) >= CLI_BUFFER_SIZE) {
        printf("cli_init: embeddedCliRequiredSize failed.\n");
        return false;
    }

    _cli = embeddedCliNew(config);
    //  TU_ASSERT(_cli != NULL);
    if (!_cli) {
        printf("cli_init: embeddedCliNew failed.\n");
        return false;
    }

    _cli->writeChar = cli_write_char;

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "cat",
        "Usage: cat [FILE]...\n\tConcatenate FILE(s) to standard output..",
        true,
        NULL,
        cli_cmd_cat
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "dump",
        "Usage: dump [FILE]...\n\tDump FILE to standard output..",
        true,
        NULL,
        cli_cmd_dump
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "cd",
        "Usage: cd [DIR]...\n\tChange the current directory to DIR.",
        true,
        NULL,
        cli_cmd_cd
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "cp",
        "Usage: cp SOURCE DEST\n\tCopy SOURCE to DEST.",
        true,
        NULL,
        cli_cmd_cp
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "ls",
        "Usage: ls [DIR]...\n\tList information about the FILEs (the current directory by default).",
        true,
        NULL,
        cli_cmd_ls
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "pwd",
        "Usage: pwd\n\tPrint the name of the current working directory.",
        true,
        NULL,
        cli_cmd_pwd
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "mkdir",
        "Usage: mkdir DIR...\n\tCreate the DIRECTORY(ies), if they do not already exist..",
        true,
        NULL,
        cli_cmd_mkdir
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "mv",
        "Usage: mv SOURCE DEST...\n\tRename SOURCE to DEST.",
        true,
        NULL,
        cli_cmd_mv
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "rm",
        "Usage: rm [FILE]...\n\tRemove (unlink) the FILE(s).",
        true,
        NULL,
        cli_cmd_rm
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "clk",
        "Usage: clk\n\tShow system clock.",
        true,
        NULL,
        cli_cmd_clk
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "reset",
        "Usage: reset\n\tSend reset signal.",
        true,
        NULL,
        cli_cmd_reset
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "boottest",
        "Usage: boottest\n\tSimulate a boot sequence.",
        true,
        NULL,
        cli_cmd_boottest
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "d88",
        "Usage: d88 COMMAND [arg]...\n\tAccess to D88 file directly.",
        true,
        NULL,
        cli_cmd_d88
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "fdc",
        "Usage: fdc COMMAND [arg]...\n\tAccess to D88 image using FDC.",
        true,
        NULL,
        cli_cmd_fdc
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "cfg",
        "Usage: cfg COMMAND [arg]...\n\tAccess to config information.",
        true,
        NULL,
        cli_cmd_cfg
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "dbg",
        "Usage: dbg COMMAND [arg]...\n\tAccess to debug information.",
        true,
        NULL,
        cli_cmd_dbg
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "files",
        "Usage: files\n\tList information in the d88 image.",
        true,
        NULL,
        cli_cmd_files
    });

    embeddedCliAddBinding(_cli, (CliCommandBinding) {
        "load",
        "Usage: load [FILE]\n\tSimulate loading the file in the d88 image.",
        true,
        NULL,
        cli_cmd_load
    });

    return true;
}

//======================================================================

void shell_cmd_usage_header(const char *cmd, const char *subcmd, const char *msg)
{
    printf("Usage: ");
    if (cmd) {
        printf(cmd);
    }
    if (subcmd) {
        if (cmd) putchar(' ');
        printf(subcmd);
    }
    if (msg) {
        if (cmd || subcmd) putchar(' ');
        printf(msg);
    }
}

bool shell_cmd_is_help_option(uint16_t pos, const char *args)
{
    const char* arg = embeddedCliGetToken(args, pos);
    return (strcmp(arg, "-h") == 0 || strcmp(arg, "/h") == 0);
}

//--------------------------------------------------------------------+
// CLI Command list
//--------------------------------------------------------------------+

void cli_cmd_cat(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // need at least 1 argument
    if ( argc == 0 ) {
        printf("invalid arguments\n");
        return;
    }

    for(uint16_t i=0; i<argc; i++) {
        FIL fi;
        const char* fpath = embeddedCliGetToken(args, i+1); // token count from 1

        if ( FR_OK != f_open(&fi, fpath, FA_READ) ) {
            printf("%s: No such file or directory\n", fpath);
        } else {
            uint8_t buf[512];
            UINT count = 0;
            while ( (FR_OK == f_read(&fi, buf, sizeof(buf), &count)) && (count > 0) ) {
                for(UINT c = 0; c < count; c++) {
                    const char ch = buf[c];
                    if (isprint(ch) || iscntrl(ch)) {
                        putchar(ch);
                    } else {
                        putchar('.');
                    }
                }
            }
        }

        f_close(&fi);
    }
}

void cli_cmd_dump(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // need 1 argument only
    if ( argc != 1 ) {
        printf("invalid arguments\n");
        return;
    }

    FIL fi;
    const char* fpath = embeddedCliGetToken(args, 1); // token count from 1

    if ( FR_OK != f_open(&fi, fpath, FA_READ) ) {
        printf("%s: No such file or directory\n", fpath);
    } else {
        uint8_t buf[512];
        uint32_t addr = 0;
        UINT count = 0;
        while ( (FR_OK == f_read(&fi, buf, sizeof(buf), &count)) && (count > 0) ) {
            dump_data(buf, count, addr);
            addr += count;
        }
    }

    f_close(&fi);
}

void cli_cmd_cd(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // only support 1 argument
    if ( argc != 1 ) {
        printf("invalid arguments\n");
        return;
    }

    // default is current directory
    const char *dpath = args;
    if (display_storage_change_directory(0, dpath) <= -2) {
        printf("No such file or directory '%s'\n", dpath);
        return;
    }
}

void cli_cmd_cp(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);
    if ( argc != 2 ) {
        printf("invalid arguments\n");
        return;
    }

    // default is current directory
    const char* src = embeddedCliGetToken(args, 1);
    const char* dst = embeddedCliGetToken(args, 2);

    FIL f_src;
    FIL f_dst;

    if ( FR_OK != f_open(&f_src, src, FA_READ) ) {
        printf("cannot stat '%s': No such file or directory\n", src);
        return;
    }

    if ( FR_OK != f_open(&f_dst, dst, FA_WRITE | FA_CREATE_ALWAYS) ) {
        printf("cannot create '%s'\n", dst);
        return;
    } else {
        uint8_t buf[512];
        UINT rd_count = 0;
        while ( (FR_OK == f_read(&f_src, buf, sizeof(buf), &rd_count)) && (rd_count > 0) ) {
            UINT wr_count = 0;
            if ( FR_OK != f_write(&f_dst, buf, rd_count, &wr_count) ) {
                printf("cannot write to '%s'\n", dst);
                break;
            }
        }
    }

    f_close(&f_src);
    f_close(&f_dst);
}

void cli_cmd_ls(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // only support 1 argument
    if ( argc > 1 ) {
        printf("invalid arguments\n");
        return;
    }

    // default is current directory
    const char* dpath = ".";
    if (argc) dpath = args;

    DIR dir;
    if ( FR_OK != f_opendir(&dir, dpath) ) {
        printf("cannot access '%s': No such file or directory\n", dpath);
        return;
    }

    FILINFO fno;
    char ssiz[20];
    while( (f_readdir(&dir, &fno) == FR_OK) && (fno.fname[0] != 0) ) {
        if ( fno.fname[0] == '.' ) {
            // ignore . and .. entry
            continue;
        }
        if ( fno.fattrib & AM_DIR ) {
            // directory
            printf("/%s\n", fno.fname);
        } else {
            sprintf(ssiz, "%16u", (uint32_t)fno.fsize);
            for(int c=12; c>=4; c-=4) {
                if (ssiz[c] >= '0' && ssiz[c] <= '9') {
                    int n=0;
                    for(; n<c; n++) {
                        ssiz[n]=ssiz[n+1];
                    }
                    ssiz[c]=',';
                }
            }
            printf("%-40s %sB\n", fno.fname, ssiz);
        }
    }

    f_closedir(&dir);
}

void cli_cmd_pwd(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;
    uint16_t argc = embeddedCliGetTokenCount(args);

    if (argc != 0) {
        printf("invalid arguments\n");
        return;
    }

    char path[256];
    if (FR_OK != f_getcwd(path, sizeof(path))) {
        printf("cannot get current working directory\n");
    }

    puts(path);
}

void cli_cmd_mkdir(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // only support 1 argument
    if ( argc != 1 ) {
        printf("invalid arguments\n");
        return;
    }

    // default is current directory
    const char* dpath = args;
    if ( FR_OK != f_mkdir(dpath) ) {
        printf("%s: cannot create this directory\n", dpath);
        return;
    }
}

void cli_cmd_mv(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);
    if ( argc != 2 ) {
        printf("invalid arguments\n");
        return;
    }

    // default is current directory
    const char* src = embeddedCliGetToken(args, 1);
    const char* dst = embeddedCliGetToken(args, 2);

    if ( FR_OK != f_rename(src, dst) ) {
        printf("cannot mv %s to %s\n", src, dst);
        return;
    }
}

void cli_cmd_rm(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    uint16_t argc = embeddedCliGetTokenCount(args);

    // need at least 1 argument
    if ( argc == 0 ) {
        printf("invalid arguments\n");
        return;
    }

    for(uint16_t i=0; i<argc; i++) {
        const char* fpath = embeddedCliGetToken(args, i+1); // token count from 1

        if ( FR_OK != f_unlink(fpath) ) {
            printf("cannot remove '%s': No such file or directory\n", fpath);
        }
    }
}

//======================================================================

void cli_cmd_clk(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void)args; (void) context;

    float clk;
    clk = (float)clock_get_hz(clk_sys);
    printf("CLK SYS : %f Hz\n", clk);
    clk = (float)clock_get_hz(clk_usb);
    printf("CLK USB : %f Hz\n", clk);
}

void cli_cmd_reset(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void)args; (void) context;
    printf("Software Reset\n");
    fdc_common_soft_reset();
    printf("Done\n");
}

static void cli_cmd_boottest_usage()
{
    printf("Usage: boottest <boot type>... Simulate a boot\n\tboot type ... l3 or s1\n");
}

void cli_cmd_boottest(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void)args; (void) context;
    uint16_t argc = embeddedCliGetTokenCount(args);
    int boot_type = -1;
    const char *arg = NULL;
    if ( argc >= 1 ) {
        arg = embeddedCliGetToken(args, 1);
        if (arg) {
            if (strcasecmp(arg, "l3") == 0) {
                boot_type = 0;
            } else if (strcasecmp(arg, "s1") == 0) {
                boot_type = 1;
            } 
        }
    }
    if (boot_type < 0) {
        cli_cmd_boottest_usage();
        return;
    }
    printf("Boot Test\n");
    fdc_cmd_boottest(boot_type);
    printf("Done\n");
}

//======================================================================

static void cli_cmd_d88(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    d88_cmd(args);
}

//======================================================================

static void cli_cmd_fdc(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    fdc_cmd(args);
}

//======================================================================

static void cli_cmd_cfg(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    cfg_cmd(args);
}

//======================================================================

static void cli_cmd_dbg(EmbeddedCli *cli, char *args, void *context)
{
    (void) cli; (void) context;

    dbg_cmd(args);
}

//======================================================================

void cli_cmd_files(EmbeddedCli *cli, char *args, void *context)
{
    uint16_t argc = embeddedCliGetTokenCount(args);
    uint8_t drv_num = 0;

    const char *arg = NULL;
    if ( argc >= 1 ) {
        arg = embeddedCliGetToken(args, 1);
        if (arg[0] >= '0' && arg[0] <= '9' && arg[1] == 0) {
            drv_num = arg[0] - '0';
        }
    }
    bas_cmd_files(drv_num);
}

void cli_cmd_load(EmbeddedCli *cli, char *args, void *context)
{
    uint16_t argc = embeddedCliGetTokenCount(args);
    uint8_t drv_num = 0;

//    if (argc != 1) {
//        printf("Usage: load [<drive>] [<file name>]\n");
//        return;
//    }
    const char *arg = NULL;
    const char *file_name = NULL;
    if (argc >= 1) {
        arg = embeddedCliGetToken(args, 1);
        if (arg[0] >= '0' && arg[0] <= '9' && arg[1] == 0) {
            // drive number
            drv_num = arg[0] - '0';
        } else {
            // file name
            file_name = arg;
        }
    }
    if (argc >= 2) {
        file_name = embeddedCliGetToken(args, 2);
    }
    bas_cmd_load(drv_num, file_name);
}
