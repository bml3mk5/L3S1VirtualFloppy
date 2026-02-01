/**
 * @file config.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-24
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/binary_info.h>
#include "common.h"
#if USE_FATFS
#include <FF.h>

static FIL fd;
#else

static FILE *fp;
#endif

#ifdef _MBS1
#define L3VFD_INI_FILE "/S1VFD.INI"
#else
#define L3VFD_INI_FILE "/L3VFD.INI"
#endif


static struct st_config {
    char    disk_paths[MAX_DRIVES][256];
    uint8_t side_number[MAX_DRIVES];
    uint8_t disk_type;
    uint8_t seek_track;
    uint8_t search_sector;
    uint8_t data_request;
    uint8_t i2c_ssd1306_type;
} g_config;

static void config_clear_paths();

static void init_file();
static bool open_file(const char *path, bool is_write);
static void close_file();
static void read_string(const char *key, char *value, size_t size);
static void write_string(const char *key, const char *value);
static int read_digit(const char *key, int def_val);
static void write_digit(const char *key, int value);

static void config_flash_load();

// ======================================================================

void config_init(void)
{
    init_file();

    config_clear_paths();
    g_config.disk_type = 0;

    config_flash_load();
}

static void config_clear_paths()
{
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        g_config.disk_paths[drv][0] = '\0';
        g_config.side_number[drv] = 0;
    }
}

/// @brief Load the d88 file path last mounted 
/// @return true:success 
bool config_load(void)
{
    char key[12];
    const char *path = L3VFD_INI_FILE;
    if (!open_file(path, false)) {
        // cannot open
        config_clear_paths();
        return false;
    }
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        sprintf(key, "Disk%d", drv);
        read_string(key, g_config.disk_paths[drv], sizeof(g_config.disk_paths[0]));
        sprintf(key, "Disk%dSide", drv);
        g_config.side_number[drv] = (uint8_t)read_digit(key, g_config.side_number[drv]);
    }
#ifdef USE_CONFIG_DISK_TYPE
    g_config.disk_type = (uint8_t)read_digit("DiskType", g_config.disk_type);
#endif
    close_file();
    return true;
}

/// @brief Save the d88 file path last mounted 
/// @return true:success 
bool config_save(void)
{
    char key[12];
    const char *path = L3VFD_INI_FILE;
    if (!open_file(path, true)) {
        // cannot open
        return false;
    }
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        sprintf(key, "Disk%d", drv);
        write_string(key, g_config.disk_paths[drv]);
        sprintf(key, "Disk%dSide", drv);
        write_digit(key, g_config.side_number[drv]);
    }
#ifdef USE_CONFIG_DISK_TYPE
    write_digit("DiskType", g_config.disk_type);
#endif
    close_file();
    return true;
}

void config_set_path(int drv, const char *path)
{
    size_t len = strlen(path);
    if ((len + 1) >= sizeof(g_config.disk_paths[drv])) {
        len = sizeof(g_config.disk_paths[0]) - 1;
    }
    memset(g_config.disk_paths[drv], 0, sizeof(g_config.disk_paths[0]));
    memcpy(g_config.disk_paths[drv], path, len);
}

void config_get_path(int drv, char *path, size_t size)
{
    size_t len = strlen(g_config.disk_paths[drv]);
    if (len + 1 > size) {
        len = size - 1;
    }
    memcpy(path, g_config.disk_paths[drv], len);
    path[len]='\0';
}

char *config_get_path_ptr(int drv)
{
    return g_config.disk_paths[drv];
}

size_t config_get_path_size(void)
{
    return sizeof(g_config.disk_paths[0]);
}

void config_set_side_number(int drv, uint8_t val)
{
    g_config.side_number[drv] = val;
}

uint8_t config_get_side_number(int drv)
{
    return g_config.side_number[drv];
}

void config_set_disk_type(uint8_t val)
{
    g_config.disk_type = val;
}

uint8_t config_get_disk_type(void)
{
    return g_config.disk_type;
}

void config_set_seek_track(uint8_t val)
{
    g_config.seek_track = val;
}

uint8_t __not_in_flash_func(config_get_seek_track)(void)
{
    return g_config.seek_track;
}

void config_set_search_sector(uint8_t val)
{
    g_config.search_sector = val;
}

uint8_t __not_in_flash_func(config_get_search_sector)(void)
{
    return g_config.search_sector;
}

void config_set_data_request(uint8_t val)
{
    g_config.data_request = val;
}

uint8_t __not_in_flash_func(config_get_data_request)(void)
{
    return g_config.data_request;
}

void config_set_i2c_ssd1306_type(uint8_t val)
{
    g_config.i2c_ssd1306_type = val;
}

uint8_t __not_in_flash_func(config_get_i2c_ssd1306_type)(void)
{
    return g_config.i2c_ssd1306_type;
}

// ======================================================================

#define C_TAB '\t'
#define C_CR  '\r'
#define C_LF  '\n'

#ifndef BUFF_SIZE
#define BUFF_SIZE 260
#endif
static char g_buff[BUFF_SIZE];

static char *ltrim(char *str)
{
    char *p = str;
    while (*p != 0 && (*p == 0x20 || *p == C_TAB)) {
        p++;
    }
    return p;
}

static void rtrim(char *str)
{
    char *p = str + strlen(str);
    while(p != str && (*p == '\0' || *p == C_LF || *p == C_CR)) {
        *p = '\0';
        p--;
    }
}

static char *find_string(const char *key, char *st)
{
    char *ed;

    if (!st) return NULL;
    st = ltrim(st);
    if (*st == '#' || *st == ';') return NULL;
    ed = strchr(st, '=');
    if (!ed) return NULL;
    *ed = '\0';
    if (strcmp(key, st) == 0) {
        // match
        ed = ed + 1;
        rtrim(ed);
        return ed;
    }
    return NULL;
}


static const char *find_key(const char *key)
{
    char *p;
    char *match = NULL;

#if USE_FATFS
    f_lseek(&fd, 0);
    while(!f_eof(&fd)) {
        p = f_gets(g_buff, BUFF_SIZE, &fd);
        if (!p) break;
        match = find_string(key, p);
        if (match) break;
    }
#else
    fseek(fp, 0, SEEK_SET);
    while(!feof(fp)) {
        p = fgets(g_buff, BUFF_SIZE, fp);
        if (!p) break;
        match = find_string(key, p);
        if (match) break;
    }
#endif
    return match;
}

static void init_file()
{
#if USE_FATFS
    memset(&fd, 0, sizeof(fd));
#else
    fp = NULL;
#endif
}

static bool open_file(const char *path, bool is_write)
{
#if USE_FATFS
    BYTE mode = is_write ? (FA_WRITE | FA_CREATE_ALWAYS) : FA_READ; 
    return (f_open(&fd, path, mode) == FR_OK);
#else
    const char *mode = is_write ? "wb" : "rb";
    fp = fopen(path, mode);
    return (fp != NULL);
#endif
}

static void close_file()
{
#if USE_FATFS
    f_close(&fd);
    memset(&fd, 0, sizeof(fd));
#else
    if (fp) fclose(fp);
    fp = NULL;
#endif
}

static void read_string(const char *key, char *value, size_t size)
{
    const char *match = find_key(key);
    if (match) {
        size_t len = strlen(match);
        if (size < len) len = size;
        memcpy(value, match, len);
    }
}

static void write_string(const char *key, const char *value)
{
#if USE_FATFS
    f_puts(key, &fd);
    f_putc('=', &fd);
    f_puts(value, &fd);
    f_putc(C_LF, &fd);
#else
    fputs(key, fp);
    fputc('=', fp);
    fputs(value, fp);
    fputc(C_LF, fp);
#endif
}

static int read_digit(const char *key, int def_val)
{
    const char *match = find_key(key);
    int ival = def_val;
    if (match) {
        ival = strtol(match, NULL, 10);
    }
    return ival;
}

static void write_digit(const char *key, int value)
{
    char sval[16];
    sprintf(sval,"%d",(int)value);
    write_string(key, sval);
}

// ======================================================================

#ifdef _MBS1
#define CONFIG_FLASH_IDENT "S1VFDCFG"
#else
#define CONFIG_FLASH_IDENT "L3VFDCFG"
#endif
#define CONFIG_FLASH_VERSION 1
#define CONFIG_FLASH_REVISION 1

//#define USE_CONFIG_FLASH_OFFSET 1

#ifdef USE_CONFIG_FLASH_OFFSET
#define CONFIG_FLASH_OFFSET 0x40000
#endif

typedef union un_config_flash {
    uint8_t b[FLASH_PAGE_SIZE];
    struct {
        char identifier[8];
        uint32_t version;
        uint32_t revision;
        uint8_t disk_type;
        uint8_t seek_track;
        uint8_t search_sector;
        uint8_t data_request;
        uint8_t i2c_ssd1306_type;
    };
} config_flash_t;

#ifndef USE_CONFIG_FLASH_OFFSET
static uint8_t __in_flash("config_flash") config_flash[FLASH_SECTOR_SIZE] __aligned(FLASH_SECTOR_SIZE);
#else
static uint8_t *config_flash = (uint8_t *)(XIP_BASE + CONFIG_FLASH_OFFSET);
#endif

static void config_flash_load()
{
    const config_flash_t *p = (const config_flash_t *)&config_flash;
    if (memcmp(p->identifier, CONFIG_FLASH_IDENT, sizeof(p->identifier)) == 0) {
        if (p->version == CONFIG_FLASH_VERSION) {
            // load from flash memory
            g_config.disk_type = p->disk_type;
            g_config.seek_track = p->seek_track;
            g_config.search_sector = p->search_sector;
            g_config.data_request = p->data_request;
            g_config.i2c_ssd1306_type = p->i2c_ssd1306_type;
        }
    }
}

void config_flash_save()
{
    config_flash_t data;
    memset(&data, 0, sizeof(data));
    memcpy(data.identifier, CONFIG_FLASH_IDENT, sizeof(data.identifier));
    data.version = CONFIG_FLASH_VERSION;
    data.revision = CONFIG_FLASH_REVISION;
    data.disk_type = g_config.disk_type;
    data.seek_track = g_config.seek_track;
    data.search_sector = g_config.search_sector;
    data.data_request = g_config.data_request;
    data.i2c_ssd1306_type = g_config.i2c_ssd1306_type;

   uint32_t offset = (uint32_t)(uint32_t *)config_flash - XIP_BASE;

   // disable interrupt
   uint32_t save = save_and_disable_interrupts();
   // program in flash
   flash_range_erase(offset, FLASH_SECTOR_SIZE);
   flash_range_program(offset, data.b, FLASH_PAGE_SIZE);
   // enable interrupt
   restore_interrupts(save);
}

// ======================================================================

void config_debug_info(void)
{
    printf("g_config:\n");
    struct st_config *st = &g_config;
    printf(" disk_type:%u\n", st->disk_type);
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        printf(" disk_path %d:%s side:%u\n", drv, st->disk_paths[drv], st->side_number[drv]);
    }
    printf("config data in flash:\n");
    const uint8_t *fh = (const uint8_t *)&config_flash;
    for(int i=0; i<16; i++) {
        printf(" %02x", fh[i]);
    }
    printf("\n");
    const config_flash_t *fc = (const config_flash_t *)fh;
    printf(" version:%u\n", fc->version);
    printf(" revision:%u\n", fc->revision);
    printf(" disk_type:%u\n", fc->disk_type);
    printf(" seek_track:%u\n", fc->seek_track);
    printf(" search_sector:%u\n", fc->search_sector);
    printf(" data_request:%u\n", fc->data_request);
    printf(" i2c_ssd1306_type:%u\n", fc->i2c_ssd1306_type);
}
