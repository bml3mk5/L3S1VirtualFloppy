/**
 * @file display_disk.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-10-18
 * 
 * @copyright Copyright (c) Sasaji 2025
 * 
 */

#include "display_disk.h"
#include "common.h"
//#include "main.h"
#include "msc_app.h"
#include "disk_drive.h"
#include "disk_d88.h"
#include "fdc_common.h"
#include "config.h"
#include "utils.h"

const disk_image_exts_t disk_image_exts[2] = {
    { ".d88", 4, "D88 file" },
    { NULL, 0, NULL }
};

static void display_disk_file_append_header(FIL *fp, const char *path, size_t len);

//--------------------------------------------------------------------

static void lcd_disk_init(void);

//--------------------------------------------------------------------

void display_disk_init(void)
{
    display_storage_append_header_cb = display_disk_file_append_header;
    lcd_disk_init();
}

void __not_in_flash_func(display_disk_task)(void)
{
}

//--------------------------------------------------------------------

/// @brief Remount the last d88 file that was mounted previously.
/// @return position in the directory list / -1 : not found or error
int display_disk_file_set_path(int img_drv, simple_list_t *tree, simple_list_t *list)
{
    FIL fd;

    // file exists ?
    memset(&fd, 0, sizeof(fd));
    if (f_open(&fd, config_get_path_ptr(img_drv), FA_READ) != FR_OK) {
        // cannot open
        return -1;
    }
    f_close(&fd);

    return msc_app_trace_path(config_get_path_ptr(img_drv), tree, list);
}

/// @brief Unmount the d88 file
/// @param img_drv : drive number
void display_disk_file_unmount(int img_drv)
{
    disk_drive_unmount(img_drv);
//    display_lcd_drive_number(img_drv);
    lcd_locate_charset(2, 1, 0x20, 14);
}

/// @brief 
/// @param name 
/// @param len 
/// @return 
static int display_disk_file_match_file_ext(const char *name, size_t len)
{
    int match = -1;
    for(int i=0; disk_image_exts[i].ext != NULL; i++) {
        const struct st_disk_image_exts *c = &disk_image_exts[i];
        if (strcasecmp(&name[len - c->len], c->ext) == 0) {
            match = i;
            break;
        }
    }
    return match;
}

/// @brief Mount the d88 file and show the file info on I2C LCD
/// @param img_drv : drive number
/// @param file_path : d88 file name
bool display_disk_file_mount_by_path(int img_drv, const char *file_path)
{
    if (f_stat(file_path, NULL) != FR_OK) {
        // file not found
        return false;
    }
    size_t file_len = strlen(file_path);
    int match = display_disk_file_match_file_ext(file_path, file_len);
    if (match < 0) {
        // not tape file
        return false;
    }
    struct st_file_info *file_info = &storage_info.file_info[img_drv];
    file_info->pos.c = 0;
    file_info->pos.n = 0;
    int pos = msc_app_trace_path(file_path, &file_info->tree, &file_info->list);
    if (pos >= 0) {
        file_info->pos.n = pos;
        file_info->flags |= ST_FLG_NEED_MOUNT;
    } else {
        file_info->pos.c = pos;
    }
    return true;
}

/// @brief 
/// @param file_info 
/// @param data 
/// @return 
bool display_disk_file_check_file(struct st_file_info *file_info, simple_list_data_t *data)
{
    int match = display_disk_file_match_file_ext(data->name, data->len);
    if (match < 0) {
        file_info->flags &= ~ST_FLG_DISK_IMAGE;
        return false;
    }
    char str[20];
    str[0]=' ';
    strcpy(&str[1], disk_image_exts[match].comment);
    lcd_padding(str, strlen(str), 16);
    lcd_locate_substring(1, 1, str, 16);
    file_info->flags |= ST_FLG_DISK_IMAGE;
    return true;
}

/// @brief Mount the d88 file and show the file info on I2C LCD
/// @param img_drv : drive number
/// @param sid : side number
/// @param data : file information
/// @param tree : directory tree in storage
bool display_disk_file_mount_on(int img_drv, uint8_t sid, const simple_list_data_t *data, simple_list_t *tree)
{
    int match = display_disk_file_match_file_ext(data->name, data->len);
    if (match < 0) {
        // not tape image file
        display_disk_file_unmount(img_drv);
        return false;
    }

    char path[256];
    msc_app_make_file_path_from_tree(tree, data, path, sizeof(path));

    if (disk_drive_mount(img_drv, sid, path, 0)) {
        fdc_common_set_side_number(img_drv, sid);
#ifndef USE_CURRENT_DIRECTORY
        display_config_set_path(img_drv, path, sid);
#else
        display_config_make_path(img_drv, path, tree);
#endif
    }
    return true;
}

/// @brief 
/// @param file_info : file information
/// @param data : a file or directory
void display_disk_file_mount_by_info(struct st_file_info *file_info, simple_list_data_t *data)
{
    int img_drv = file_info->drv;

    // d88 file ?
    if (data && (data->attr & (AM_DIR | AM_SYS)) == 0) {
        // try to mount the d88 file
        display_disk_file_mount_on(img_drv, file_info->sid, data, &file_info->tree);
    } else {
        disk_drive_unmount(img_drv);
        display_config_clear_path(img_drv);
    }
}

/// @brief Show the file or directory in storage on I2C LCD
/// @param file_info : file information
/// @param data : a file or directory
void display_lcd_disp_file_and_disk_file_mount(struct st_file_info *file_info, simple_list_data_t *data)
{
    display_lcd_disp_file(file_info, data);
    display_disk_file_mount_by_info(file_info, data);
}

/// @brief Show the file or directory in storage on I2C LCD
/// @param file_info : file information
/// @param data : a file or directory
void display_lcd_disp_file_and_disk_file_info(struct st_file_info *file_info, simple_list_data_t *data)
{
    int img_drv = file_info->drv;

    if (!data) {
        return;        
    }

    display_lcd_disp_file(file_info, data);

    if (disk_d88_is_not_ready(img_drv)) {
        if (data && (data->attr & (AM_DIR | AM_SYS)) == 0) {
            // try to mount the d88 file
            display_disk_file_mount_on(img_drv, file_info->sid, data, &file_info->tree);
        } else {
            disk_drive_unmount(img_drv);
        }
    }  else {
        // already mounted
        disk_d88_disp_lcd(img_drv);
    }
}

/// @brief 
/// @param file_info : file information
void display_disk_file_toggle_side_number(struct st_file_info *file_info)
{
    if (disk_d88_is_not_ready(file_info->drv)) return;
    uint8_t nsid = fdc_common_toggle_side_number(file_info->drv, file_info->sid);
    if (nsid != file_info->sid) {
        display_config_set_side_number(file_info->drv, nsid);
        file_info->sid = nsid;
    }
}

//--------------------------------------------------------------------

void display_disk_file_append_header(FIL *fp, const char *path, size_t len)
{
    int match = display_disk_file_match_file_ext(path, len);
    if (match < 0) {
        return;
    }
    disk_drive_append_header(fp, match);
}

//--------------------------------------------------------------------

static uint8_t led_motor;

/// @brief Initialize data to display the track, side and sector number on I2C LCD
/// @param  
void lcd_disk_init(void)
{
    led_motor = 0;
}

/// @brief Turn on/off the LED lamp on the I2C device
/// (Simulate access lamp on a FDD)
/// @param drv : drive number 
/// @param onoff : 0:OFF !=0:ON
void __not_in_flash_func(lcd_disk_motor)(int drv, int onoff)
{
    uint8_t new_motor;
    if (drv >= 0) {
        new_motor = (onoff ? (1 << (7-drv)) : 0);
    } else {
        new_motor = (onoff ? ~((1 << (8-MAX_DRIVES)) - 1) : 0);
    }
    if (led_motor != new_motor) {
        led_motor = new_motor;
        display_led_force(led_motor, 0xc0);
    }
}

/// @brief Show the track, side and sector number on the I2C LCD
/// @param drv : drive number 
/// @param trk : track number
/// @param sid : side number
/// @param sec : sector number
void __not_in_flash_func(lcd_disk_trk_sid_sec_number)(int drv, int trk, int sid, int sec)
{
    char str[8];
    if (drv == display_filelist_get_current_drive()) {
        dec_str_2(trk, &str[0]);
        str[2] = ':';
        str[3] = (sid & 1) + '0';
        str[4] = ':';
        dec_str_2(sec, &str[5]);
        str[7] = 0;
        lcd_locate_substring(2, 1, str, 7);
    }
}

/// @brief Show the track, side and sector number on the I2C LCD
/// @param drv : drive number 
/// @param trk : track number
/// @param sid : side number
void __not_in_flash_func(lcd_disk_trk_sid_number)(int drv, int trk, int sid)
{
    char str[8];
    if (drv == display_filelist_get_current_drive()) {
        dec_str_2(trk, &str[0]);
        str[2] = ':';
        str[3] = (sid & 1) + '0';
        str[4] = 0;
        lcd_locate_substring(2, 1, str, 4);
    }
}

/// @brief Show the side and sector number on the I2C LCD
/// @param drv : drive number 
/// @param sid : side number
/// @param sec : sector number
void __not_in_flash_func(lcd_disk_sid_sec_number)(int drv, int sid, int sec)
{
    char str[8];
    if (drv == display_filelist_get_current_drive()) {
        str[0] = (sid & 1) + '0';
        str[1] = ':';
        dec_str_2(sec, &str[2]);
        str[4] = 0;
        lcd_locate_substring(5, 1, str, 4);
    }
}

/// @brief Show the side number on the I2C LCD
/// @param drv : drive number 
/// @param sid : side number
void __not_in_flash_func(lcd_disk_sid_number)(int drv, int sid)
{
    char str[4];
    if (drv == display_filelist_get_current_drive()) {
        str[0] = (sid & 1) + '0';
        str[1] = 0;
        lcd_locate_substring(5, 1, str, 1);
    }
}

/// @brief Show the sector number on the I2C LCD
/// @param drv : drive number 
/// @param sec : sector number
void __not_in_flash_func(lcd_disk_sec_number)(int drv, int sec)
{
    char str[4];
    if (drv == display_filelist_get_current_drive()) {
        dec_str_2(sec, &str[0]);
        str[2] = 0;
        lcd_locate_substring(7, 1, str, 2);
    }
}

/// @brief 
/// @param drv 
/// @param tracks_per_side 
/// @param sides_per_disk 
/// @param valid 
/// @param protect 
void __not_in_flash_func(lcd_disk_status)(int drv, int tracks_per_side, int sides_per_disk, bool valid, bool protect)
{
    char str[8];
    if (drv == display_filelist_get_current_drive()) {
//        sprintf(str, " %02d:%d  ", tracks_per_side, sides_per_disk);
        str[0] = ' ';
        dec_str_2(tracks_per_side, &str[1]);
        str[3] = ':';
        str[4] = (sides_per_disk & 3) + '0';
        str[5] = ' ';
        if (valid) {
            if (!protect) {
                str[6] = 'N';
            } else {
                str[6] = 'P';
            }
        } else {
            str[6] = 'E';
        }
        str[7] = 0;
        lcd_locate_substring(9, 1, str, 7);
    }
}
