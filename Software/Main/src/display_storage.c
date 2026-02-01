/**
 * @file display_storage.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-24
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "display_storage.h"
#include "main.h"
#include "display_disk.h"
#include "display_menu.h"
//#include "display_setting.h"
#include "event.h"
#include "msc_app.h"
//#include "disk_drive.h"
//#include "fdc_common.h"
//#include "disk_d88.h"
#include "config.h"

struct st_display_storage storage_info;

static void display_storage_mounted_cb(void);
static void display_storage_unmounted_cb(void);
display_storage_append_header_t display_storage_append_header_cb = NULL;
display_storage_progress_t display_storage_progress_cb = NULL;

void display_storage_init(void)
{
    // relate each components
    msc_app_storage_mounted_cb = display_storage_mounted_cb;
    msc_app_storage_unmounted_cb = display_storage_unmounted_cb;

    storage_info.state = ST_STATE_UNMOUNT;
//    storage_info.flags = 0;
    storage_info.curr_drv.c = 0;
    storage_info.curr_drv.n = 0;
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        struct st_file_info *file_info = &storage_info.file_info[drv];
        file_info->drv = (uint16_t)drv;
        file_info->sid = 0;
        file_info->pos.c = 0;
        file_info->pos.n = 0;
        simple_list_init(&file_info->list);
        simple_list_init(&file_info->tree);
        text_shift_init(&file_info->shift);
        file_info->flags = 0;
    }
//    memset(&display_lcd, 0, sizeof(display_lcd));
    storage_info.id = -1;
}

void display_storage_mounted_cb(void)
{
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        storage_info.file_info[drv].flags = 0;
    }
    storage_info.state = ST_STATE_MOUNT;
}

void display_storage_unmounted_cb(void)
{
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        storage_info.file_info[drv].flags = 0;
    }
    storage_info.state = ST_STATE_UNMOUNT;
}

/// @brief Change the sub directory or parent
/// @param file_info : file information
/// @param data : current directory
static void display_directory_change(struct st_file_info *file_info, simple_list_data_t *data)
{
    // go to sub directory or parent directory
    int npos = msc_app_change_directory(data, &file_info->tree, &file_info->list);
    if (npos < 0) {
        // directory not found
        file_info->flags |= ST_FLG_DIR_NOT_FOUND;
    } else {
        // changed directory
        file_info->flags &= ~ST_FLG_NOT_FOUND_MASK;
        file_info->pos.c = -1;
        file_info->pos.n = npos;
    }
}

/// @brief 
/// @param[in] name file name without path tree
/// @param[in,out] tree directory tree
/// @param[in,out] list file list in current directory 
/// @return true if success
bool display_storage_create_file_in_current_dir(const char *name, simple_list_t *tree, simple_list_t *list)
{
    FIL fp;
    char path[256];
    size_t path_len = 0;
    size_t name_len = strlen(name);

    if (name_len == 0) {
        return false;
    }

    memset(&fp, 0, sizeof(fp));

    path_len = msc_app_make_dir_path_from_tree(tree, path, sizeof(path));
    if (path_len + name_len + 1 > sizeof(path)) {
        return false;
    }
    strcat(path, "/");
    strcat(path, name);

    if ( FR_OK != f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) ) {
        debug_printf("Cannot create a file: '%s'\n", path);
        return false;
    }

    if (display_storage_append_header_cb) {
        display_storage_append_header_cb(&fp, path, strlen(path));
    }

    f_close(&fp);

    msc_app_reload_directory(tree, list);
    return true;
}

//--------------------------------------------------------------------

#if 0
/// @brief 
/// @param dst 
/// @param dst_len : size of dst
/// @param dst_fillen : length to fill space of dst
/// @param src 
/// @param src_len : size of src 
static void display_lcd_strncpy(char *dst, size_t dst_len, size_t dst_fillen, const char *src, size_t src_len)
{
}
#endif

/// @brief Make the file name to show the I2C LCD
/// @param dst : formatted string
/// @param dst_len : size of dst
/// @param dst_fillen : length to fill space of dst
/// @param src : a file name
static void display_lcd_make_file_name(char *dst, size_t dst_len, size_t dst_fillen, const char *src)
{
    size_t src_len = strlen(src);
    if (dst_len > dst_fillen) {
        memset(dst, 0x20, dst_fillen);
        memset(&dst[dst_fillen], 0, dst_len - dst_fillen);
    } else {
        memset(dst, 0x20, dst_len);
    }
    if (src_len + 1 > dst_len) src_len = dst_len - 1;
    memcpy(dst, src, src_len);
}

/// @brief Make the directory name to show the I2C LCD
/// @param dst : formatted string
/// @param dst_len : size of dst
/// @param dst_fillen : length to fill space of dst
/// @param src : a directory name
static void display_lcd_make_dir_name(char *dst, size_t dst_len, size_t dst_fillen, const char *src)
{
    size_t len = strlen(src);
    memset(dst, 0x20, dst_fillen);
    memset(&dst[dst_fillen], 0x0, FILE_TITLE_SIZE + 1 - dst_fillen);
    dst[0] = '[';
    if (len + 2 > dst_fillen) len = dst_fillen;
    memcpy(&dst[1], src, len);
    dst[len+1] = ']';
}

#define DISPLAY_LCD_FILE_SIZE 14

/// @brief 
/// @param file_info 
static void display_lcd_filelist_default(struct st_file_info *file_info, const char *message)
{
    if (storage_info.curr_drv.c != file_info->drv) return;

    display_lcd_drive_number(file_info->drv);
    lcd_locate_substring(2, 0, file_info->shift.text, DISPLAY_LCD_FILE_SIZE);
    lcd_locate_charset(0, 1, 0x20, 16);
    if (message) {
        lcd_locate_string(0, 1, message);
    }
}

/// @brief Show the drive number on the I2C LCD
/// @param img_drv : drive number 
void display_lcd_drive_number(int img_drv)
{
    char str[4];
    str[0] = (img_drv & 3) + '0';
    str[1] = ':';
    str[2] = 0;
    lcd_locate_substring(0, 0, str, 2);
}

//--------------------------------------------------------------------

/// @brief Wait a milli sec after clearing monitor on I2C LCD 
/// @param id : unused
/// @param user_data : unused
/// @return always 0 
static int64_t display_storage_wait_cb(alarm_id_t id, void *user_data)
{
    (void)id; (void)user_data;
    storage_info.id = -1;
    storage_info.state++;
    return 0;
}

/// @brief Processing to unmount the storage (USB memory)
static void display_storage_unmount(void)
{
//    i2c_lcd_1602_clear(&i2c_lcd);
    event_cancel_event(&storage_info.id);
    storage_info.state++;
    storage_info.id = event_register_event(1000, display_storage_wait_cb, 0);
}

/// @brief Finish unmounting the storage (USB memory)
static void display_storage_unmount_done(void)
{
    char str[20];

    strcpy(str, APPLICATION);
    lcd_padding(str, strlen(str), 16);
    lcd_locate_substring(0, 0, str, 16);

    strcpy(str, VERSION);
    strcat(str, " Mount USB");
    lcd_padding(str, strlen(str), 16);
    lcd_locate_substring(0, 1, str, 16);

    storage_info.state = ST_STATE_IDLE;
}

/// @brief Processing to mount the storage (USB memory)
static void display_storage_mount(void)
{
    uint64_t cur_time = from_us_since_boot(get_absolute_time());
 
//    i2c_lcd_1602_clear(&i2c_lcd);

    event_cancel_event(&storage_info.id);

    storage_info.curr_drv.c = 0;
    storage_info.curr_drv.n = 0;

    // make file list on root direcoty
    simple_list_clear(&storage_info.file_info[0].tree);
    simple_list_clear(&storage_info.file_info[0].list);
    if (!msc_app_make_list_in_directory("/", 0, &storage_info.file_info[0].list)) {
        storage_info.file_info[0].flags |= ST_FLG_DIR_NOT_FOUND;
        simple_list_clear(&storage_info.file_info[0].list);
    }
    for(int drv=1; drv<MAX_DRIVES; drv++) {
        // copy file list
        struct st_file_info *file_info = &storage_info.file_info[drv];
        simple_list_clear(&file_info->tree);
        simple_list_copy(&file_info->list, &storage_info.file_info[0].list);
        file_info->flags = storage_info.file_info[0].flags;
    }

    cur_time -= from_us_since_boot(get_absolute_time());
    if (cur_time < 950) {
        storage_info.state++;
        storage_info.id = event_register_event(1000 - cur_time, display_storage_wait_cb, 0);
    } else {
        storage_info.state = ST_STATE_MOUNT_DONE;
    }
}

/// @brief Finish mounting the storage (USB memory)
static void display_storage_mount_done(void)
{
#ifdef USE_CURRENT_DIRECTORY
    char path[256];
#endif

    config_load();

    for(int drv=0; drv<MAX_DRIVES; drv++) {
        struct st_file_info *file_info = &storage_info.file_info[drv];

        file_info->pos.c = 0;
        file_info->pos.n = 0;

        if (!(file_info->flags & ST_FLG_DIR_NOT_FOUND)) {
#ifdef USE_CURRENT_DIRECTORY
            f_chdir("/");
#endif
            file_info->sid = config_get_side_number(drv);
            int pos = display_disk_file_set_path(drv, &file_info->tree, &file_info->list);
            if (pos >= 0) {
                file_info->pos.n = pos;
                file_info->flags |= ST_FLG_NEED_MOUNT;
            } else {
                file_info->pos.c = pos;
            }
        }
    }
#ifdef USE_CURRENT_DIRECTORY
    if (msc_app_make_dir_path_from_tree(&storage_info.file_info[0].tree, path, sizeof(path))) {
        f_chdir(path);
    }
#endif
    halt_signal_off(HALT_SIGNAL_POR);

    storage_info.state = ST_STATE_MOUNTING;
}

/// @brief Change directory
/// @param img_drv : drive number
/// @param path : path such as "/foo/bar/baz/file.txt" (slash separator)
/// @return Position the file in the directory / -2 means file not found
int display_storage_change_directory(int img_drv, const char *path)
{
#ifndef USE_CURRENT_DIRECTORY
    if (f_chdir(path) != FR_OK) {
        return -2;
    }
    return 0;
#else
    if (f_stat(path, NULL) != FR_OK) {
        // file not found
        return -2;
    }
    struct st_file_info *file_info = &storage_info.file_info[img_drv];
    file_info->pos.c = 0;
    file_info->pos.n = 0;
    int pos = msc_app_trace_path(path, &file_info->tree, &file_info->list);
    if (pos >= 0) file_info->pos.n = pos;
    else file_info->pos.c = pos;
    return pos;
#endif
}

/// @brief Show the file or directory in storage on I2C LCD
/// @param file_info : file information
/// @param data : a file or directory
void display_lcd_disp_file(struct st_file_info *file_info, simple_list_data_t *data)
{
//    int img_drv = file_info->drv;
    if (data) {
        // data found
        char *name = data->name;
        uint8_t attr = data->attr;
        if (attr & AM_DIR) {
            display_lcd_make_dir_name(file_info->shift.text, FILE_TITLE_SIZE + 1, DISPLAY_LCD_FILE_SIZE, name);
        } else {
            display_lcd_make_file_name(file_info->shift.text, FILE_TITLE_SIZE + 1, DISPLAY_LCD_FILE_SIZE, name);
        }
    } else {
        // no data found
        display_lcd_make_file_name(file_info->shift.text, FILE_TITLE_SIZE + 1, DISPLAY_LCD_FILE_SIZE, "(no file)");   
    }
    int flen = (int)strlen(file_info->shift.text);
    file_info->shift.phase = (flen > DISPLAY_LCD_FILE_SIZE ? 1 : 0);

    // Shift the file name on LCD, when file name is more then 16.
    if (file_info->shift.phase) {
        file_info->shift.ms = g_c0_current_time_ms;
        file_info->shift.pos_max = flen - DISPLAY_LCD_FILE_SIZE + 1;
    }

    // Update to display on I2C LCD
    display_lcd_filelist_default(file_info, NULL);
}

/// @brief Processing with storage (USB memory) mounted
static void __not_in_flash_func(display_storage_mounting)(void)
{
    struct st_file_info *file_info;
    if (storage_info.curr_drv.c != storage_info.curr_drv.n) {
        // change drive number
        storage_info.curr_drv.c = storage_info.curr_drv.n;
        // current path
        file_info = &storage_info.file_info[storage_info.curr_drv.c];
        int npos = msc_app_change_directory_from_tree(&file_info->tree, &file_info->list);
        if (npos > 0) {
            // list is changed
            file_info->flags = 0;
            file_info->pos.n = 0;
            file_info->pos.c = -1;
        } else {
            // changed directory
//            file_info->flags = 0;
//            file_info->pos.c = 0;
            simple_list_data_t *data = simple_list_get_data_by_index(&file_info->list, file_info->pos.c);
            display_lcd_disp_file_and_disk_file_info(file_info, data);
        }
    } else {
        file_info = &storage_info.file_info[storage_info.curr_drv.c];
    }

    if (file_info->pos.c != file_info->pos.n) {
        // change file position on current list
        file_info->pos.c = file_info->pos.n;
        simple_list_data_t *data = simple_list_get_data_by_index(&file_info->list, file_info->pos.c);
        display_lcd_disp_file_and_disk_file_mount(file_info, data);
        file_info->flags &= ~ST_FLG_NEED_MOUNT;
    }
    text_shift_task(&file_info->shift);

    for(int drv=0; drv<MAX_DRIVES; drv++) {
        file_info = &storage_info.file_info[drv];
        // when set a disk file on command line
        if (file_info->flags & ST_FLG_NEED_MOUNT) {
            file_info->pos.c = file_info->pos.n;
            simple_list_data_t *data = simple_list_get_data_by_index(&file_info->list, file_info->pos.c);
            display_disk_file_mount_by_info(file_info, data);
            file_info->flags &= ~ST_FLG_NEED_MOUNT;
        }
    }
}

/// @brief Main task to display a file
void __not_in_flash_func(display_storage_task)(void)
{
    if (storage_info.id >= 0) {
        return;
    }
    switch(storage_info.state) {
    case ST_STATE_UNMOUNT:
        // storage unmount
        display_storage_unmount();
        break;
    case ST_STATE_UNMOUNT_DONE:
        // storage unmount done
        display_storage_unmount_done();
        break;
    case ST_STATE_MOUNT:
        // storage mounted
        display_storage_mount();
        break;
    case ST_STATE_MOUNT_DONE:
        // storage mounted
        display_storage_mount_done();
        break;
    case ST_STATE_MOUNTING:
        // now mounting
        display_storage_mounting();
        break;
    default:
        break;
    }
}

void display_storage_change_phase(void)
{
    display_info.phase = PHASE_STORAGE;
    if (storage_info.state == ST_STATE_IDLE) {
        storage_info.state = ST_STATE_UNMOUNT_DONE;
    }
    struct st_file_info *file_info = &storage_info.file_info[storage_info.curr_drv.c];
    // update screen 
    file_info->pos.n = file_info->pos.c;
    file_info->pos.c = -1;
}

//--------------------------------------------------------------------

/// @brief Move position of selected file in the directory
/// @param dir : direction 1 or -1
void display_filelist_move(int dir)
{
    struct st_file_info *file_info = &storage_info.file_info[storage_info.curr_drv.c];
    file_info->pos.n = display_change_choice(dir, file_info->pos.n, file_info->list.count);
}

/// @brief Process when press the OK and move button
void display_filelist_confirm_move(int dir)
{
    storage_info.curr_drv.n = display_change_choice(dir, storage_info.curr_drv.n, MAX_DRIVES);
}

/// @brief Process when press the OK button
void display_filelist_confirm(void)
{
    struct st_file_info *file_info = &storage_info.file_info[storage_info.curr_drv.c];
    simple_list_data_t *data = simple_list_get_data_by_index(&file_info->list, file_info->pos.c);
    if (!data) return;

    if (!(data->attr & AM_DIR)) {
        // file
        display_disk_file_toggle_side_number(file_info);
    } else {
        // directory
        display_directory_change(file_info, data);
    }
}

void display_filelist_confirm_long(void)
{
    display_menu_change_phase();
}

//--------------------------------------------------------------------

int display_filelist_find_by_name(const char *name)
{
    int match = -1;
    struct st_display_storage *si = &storage_info;
    if (si->curr_drv.c < 0) {
        return match;
    }
    struct st_file_info *fi = &si->file_info[si->curr_drv.c];
    simple_list_data_t *data = simple_list_get_data_by_name(&fi->list, name);
    if (data) {
        match = data->index;
    }
    return match;
}

int display_filelist_find_by_subname(const char *name, size_t len)
{
    int match = -1;
    struct st_display_storage *si = &storage_info;
    if (si->curr_drv.c < 0) {
        return match;
    }
    struct st_file_info *fi = &si->file_info[si->curr_drv.c];
    simple_list_data_t *data = simple_list_get_data_by_subname(&fi->list, name, len);
    if (data) {
        match = data->index;
    }
    return match;
}

//--------------------------------------------------------------------

void display_storage_lcd_debug_info(void)
{
    printf("storage_info:\n");
    struct st_display_storage *si = &storage_info;
    printf(" state:%u alarm_id:%x curr_drv.c:%d n:%d\n",
        si->state, si->id, si->curr_drv.c, si->curr_drv.n
    );
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        struct st_file_info *fi = &si->file_info[drv];
        printf(" file_info: drv:%u sid:%u\n", fi->drv, fi->sid);
        printf("  flags:%x pos.c:%d n:%d\n",
            fi->flags, fi->pos.c, fi->pos.n
        );
        simple_list_t *l = &fi->tree;
        printf("  tree:%d\n", l->count);
        simple_list_item_t *itm = l->item_list;
        while(itm) {
            printf("   \"%s\"\n", itm->data.name ? itm->data.name : "(null)");
            itm = itm->next;
        }
        l = &fi->list;
        printf("  list:%d\n", l->count);
        itm = l->item_list;
        while(itm) {
            printf("   \"%s\"\n", itm->data.name ? itm->data.name : "(null)");
            itm = itm->next;
        }
        printf("  shift: phase:%d pos:%d max:%d\n", fi->shift.phase, fi->shift.pos, fi->shift.pos_max);
        printf("   \"%s\"\n", fi->shift.text);
    }
}
