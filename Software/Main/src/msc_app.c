/** @file msc_app.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 * @copyright Copyright (c) 2019 Ha Thach (tinyusb.org)
 */

#include <ctype.h>
#include <hardware/gpio.h>
#include <pico/binary_info.h>
#include "main.h"
#include "tusb.h"
#include "bsp/board.h"
#include "msc_app.h"
#include "disk_ctl.h"
#include "shell_cmd.h"
#include "common.h"
#ifndef USE_CURRENT_DIRECTORY
#include <malloc.h>
#endif

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

msc_app_storage_mounted_t msc_app_storage_mounted_cb = NULL;
msc_app_storage_unmounted_t msc_app_storage_unmounted_cb = NULL;

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

bool msc_app_init(void)
{
    for(size_t i=0; i<CFG_TUH_DEVICE_MAX; i++) _disk_busy[i] = false;

    // enable pin
    gpio_init(MSC_APP_ENABLE_PIN);
    gpio_pull_down(MSC_APP_ENABLE_PIN);
    gpio_set_dir(MSC_APP_ENABLE_PIN, true);
    gpio_put(MSC_APP_ENABLE_PIN, false);
    bi_decl(bi_pin_mask_with_name(1u << MSC_APP_ENABLE_PIN, "ENABLE (Out)"));

    return true;
}

void msc_app_task(void)
{
}

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

static bool inquiry_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const * cb_data)
{
    msc_cbw_t const* cbw = cb_data->cbw;
    msc_csw_t const* csw = cb_data->csw;

    if (csw->status != 0)
    {
        printf("Inquiry failed\n");
        return false;
    }

    // Print out Vendor ID, Product ID and Rev
    debug_printf("%.8s %.16s rev %.4s\n", inquiry_resp.vendor_id, inquiry_resp.product_id, inquiry_resp.product_rev);

    // Get capacity of device
    uint32_t const block_count = tuh_msc_get_block_count(dev_addr, cbw->lun);
    uint32_t const block_size = tuh_msc_get_block_size(dev_addr, cbw->lun);

    debug_printf("Disk Size: %lu MB\n", block_count / ((1024*1024)/block_size));
    // printf("Block Count = %lu, Block Size: %lu\n", block_count, block_size);

    // For simplicity: we only mount 1 LUN per device
    uint8_t const drive_num = dev_addr-1;
    char drive_path[3] = "0:";
    drive_path[0] += drive_num;

    if ( f_mount(&fatfs[drive_num], drive_path, 1) != FR_OK )
    {
        printf("mount failed\n");
        return false;
    }

    // change to newly mounted drive
    f_chdir(drive_path);

    if (msc_app_storage_mounted_cb) {
        msc_app_storage_mounted_cb();
    }

    return true;
}

//--------------------------------------------------------------------+
// callback from msc_host on tinyusb
//--------------------------------------------------------------------+

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    debug_printf("A MassStorage device is mounted\n");
    gpio_put(MSC_APP_ENABLE_PIN, true);
//    halt_signal_off(HALT_SIGNAL_POR);

    uint8_t const lun = 0;
    tuh_msc_inquiry(dev_addr, lun, &inquiry_resp, inquiry_complete_cb, 0);
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
    debug_printf("A MassStorage device is unmounted\n");
    gpio_put(MSC_APP_ENABLE_PIN, false);

    uint8_t const drive_num = dev_addr-1;
    char drive_path[3] = "0:";
    drive_path[0] += drive_num;

    f_unmount(drive_path);

    if (msc_app_storage_unmounted_cb) {
        msc_app_storage_unmounted_cb();
    }
}

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

/// @brief Change current directory and make the file list on it
/// @param[in] path directory path
/// @param[in] except_attr except files matching specified attributes
/// @param[out] list file list 
/// @return true if success
bool msc_app_make_list_in_directory(const char *path, uint8_t except_attr, simple_list_t *list)
{
    // default is current directory
    const char* dpath = path ? path : ".";

#ifdef USE_CURRENT_DIRECTORY
    if (f_chdir(dpath) != FR_OK) {
        debug_printf("cannot change directory '%s'\n", dpath);
        return false;
    }
#endif

    DIR dir;
    // get file names in current directory
#ifdef USE_CURRENT_DIRECTORY
    if (f_opendir(&dir, ".") != FR_OK)
#else
    if (f_opendir(&dir, dpath) != FR_OK)
#endif
    {
        debug_printf("cannot access '%s': No such file or directory\n", dpath);
        return false;
    }

    simple_list_data_t data;
    FILINFO fno;
    while((f_readdir(&dir, &fno) == FR_OK) && (fno.fname[0] != 0)) {
        if ( fno.fname[0] == '.' && fno.fname[1] == '\0') {
            // ignore current directory
            continue;
        }
        if (fno.fattrib & except_attr) {
            // ignore file
            continue;
        }
        simple_list_data_ref_data(&data, 0, fno.fsize, fno.fattrib, fno.fname, strlen(fno.fname));
        simple_list_add_item(list, &data);
    }

    f_closedir(&dir);

    return true;
}

/// @brief Change the sub directory or parent
/// @param[in] newdir which change to 
/// @param[in,out] tree directory tree
/// @param[in,out] list file list in newdir 
/// @return -1: directory not found
int msc_app_change_directory(simple_list_data_t *newdir, simple_list_t *tree, simple_list_t *list)
{
    int index = -1;
    // go to sub directory or parent directory
    simple_list_t newlist;
    simple_list_init(&newlist);
#ifndef USE_CURRENT_DIRECTORY
    char dir_path[256];
    size_t len = msc_app_make_file_path_from_tree(tree, newdir, dir_path, sizeof(dir_path));
    debug_printf("cd: %s\n",dir_path);
    if (!msc_app_make_list_in_directory(dir_path, 0, &newlist))
#else
    if (!msc_app_make_list_in_directory(newdir->name, 0, &newlist))
#endif
    {
        simple_list_clear(&newlist);
    } else {
        // update list
        index = 0;
        if (newdir->name[0] == '.' && newdir->name[1] == '.') {             
            // ".." parent directory 
            simple_list_data_t *lastdata = simple_list_get_last_data(tree);
            if (lastdata) {
                index = simple_list_get_index_by_name(&newlist, lastdata->name);
                if (index < 0) {
                    index = 0;
                }
            }
            simple_list_delete_last_item(tree);
        } else {
            // sub directory (child)
            if (newlist.count > 1) {
                index++;
            }
            simple_list_add_item(tree, newdir);
        }
        simple_list_clear(list);
        *list = newlist;
    }
    return index;
}

/// @brief Re-make the file list from directory tree
/// @param[in] tree directory tree
/// @param[in,out] list file list in newdir 
/// @return -1: directory not found
int msc_app_reload_directory(simple_list_t *tree, simple_list_t *list)
{
    bool is_root;
    int index = -1;
    simple_list_t newlist;
    simple_list_init(&newlist);
#ifndef USE_CURRENT_DIRECTORY
    char dir_path[256];
    size_t len = msc_app_make_dir_path_from_tree(tree, dir_path, sizeof(dir_path));
    is_root = (len == 2);
    debug_printf("rd: %s\n",dir_path);
    if (!msc_app_make_list_in_directory(dir_path, 0, &newlist))
#else
    simple_list_data_t *dir = simple_list_get_last_data(tree);
    is_root = (dir == NULL);
    if (!msc_app_make_list_in_directory(dir ? dir->name : "/", 0, &newlist))
#endif
    {
        simple_list_clear(&newlist);
    } else {
        // update list
        if (!is_root && newlist.count > 1) {
            index++;
        }
        simple_list_clear(list);
        *list = newlist;
    }
    return index;
}

/// @brief Trace sub directories from the file path
/// @param[in] path : path such as "/foo/bar/baz/" (slash separator)
/// @param[out] tree directory tree
static const char *msc_app_make_tree_from_path(const char *path, simple_list_t *tree)
{
    const char *s = path;
    const char *p;

    if (s[0] >= '0' && s[0] <= '9' && s[1] == ':') {
        s = &s[2];
    }

    simple_list_clear(tree);

    while(1) {
        p = strchr(s, '/');
        if (!p) {
            // file
            break;
        } else {
            // directory
            if (s < p) {
                // sub directory
                simple_list_data_t data;
                simple_list_data_ref_data(&data, 0, 0, AM_DIR, s, p - s);
                simple_list_add_item(tree, &data);
            }
            s = p + 1;
        }
    }
    return s;
}


/// @brief Trace sub directories from the file path
/// @param[in] path : path such as "/foo/bar/baz/file.txt" (slash separator)
/// @param[in,out] tree directory tree
/// @param[in,out] list file list in current directory 
/// @return Position the file in the directory / -1 means directory / -2 file or directory not found
static int msc_app_make_tree_list_from_path(const char *path, simple_list_t *tree, simple_list_t *list)
{
    int index = -1;
    const char *s = path;
    const char *p;

    while(1) {
        p = strchr(s, '/');
        if (!p) {
            // file
            simple_list_data_t *data = simple_list_get_data_by_name(list, s);
            if (!data) {
                // file or directory not found
                index = -2;
                break;
            }
            if (data->attr & AM_DIR) {
                // a directory
                msc_app_change_directory(data, tree, list);
            } else {
                // a file
                index = data->index;
            }
            break;
        } else {
            // directory
            if (s < p) {
                // goto sub directory
                simple_list_data_t *data = simple_list_get_data_by_subname(list, s, p - s);
                if (!data) {
                    // directory not found
                    index = -2;
                    break;
                }
                msc_app_change_directory(data, tree, list);
            } else if (path == p) {
                // root directory (absolute path)
                if (tree->count > 0) {
                    simple_list_clear(list);
                    simple_list_clear(tree);
                    msc_app_make_list_in_directory("/", 0, list);
                }
            }
            s = p + 1;
        }
    }
    return index;
}

/// @param[in,out] tree directory tree
/// @param[in,out] list file list in current directory 
static void msc_app_trace_current_directory(simple_list_t *tree, simple_list_t *list)
{
    char path[256];
    if (f_getcwd(path, sizeof(path)) == FR_OK) {
        if (strlen(path) < sizeof(path)) {
            strcpy(path, "/");
        }
        msc_app_make_tree_from_path(path, tree);
        msc_app_make_list_in_directory(path, 0, list);
    }
}

/// @brief Trace sub directories from the file path
/// @param[in] file_path : path such as "/foo/bar/baz/file.txt" (slash separator)
/// @param[in,out] tree directory tree
/// @param[in,out] list file list in current directory 
/// @return Position the file in the directory / -1 means directory / -2 file or directory not found
int msc_app_trace_path(const char *file_path, simple_list_t *tree, simple_list_t *list)
{
    int index = -1;
    if (file_path[0] >= '0' && file_path[0] <= '9' && file_path[1] == ':') {
        // absolute
        file_path = &file_path[2];
    } else if (file_path[0] == '/') {
        // absolute
    } else {
        // relative
#ifndef USE_CURRENT_DIRECTORY
        msc_app_trace_current_directory(tree, list);
#endif
    }
    index = msc_app_make_tree_list_from_path(file_path, tree, list);
    return index;
}

/// @brief Trace sub directories from the file path
/// @param[in] tree directory tree
/// @param[out] path : path such as "/foo/bar/baz/" (slash separator)
/// @param[in] size : buffer size of path
/// @return length of path
size_t msc_app_make_dir_path_from_tree(const simple_list_t *tree, char *path, size_t size)
{
    size_t pos = 0;
    const simple_list_item_t *item = tree->item_list;
//    path[0] = 0;
    while(pos + 2 < size) {
        // separator
        path[pos] = '/';
        pos++;
        path[pos] = '\0';
        debug_printf("mp: %s\n", path);
        if (!item) {
            break;
        }
        // sub directory
        if (!item->data.name) {
            // invalid data
            break;
        }
        if (pos + item->data.len >= size) {
            // buffer full
            break;
        }
//        strcat(path, item->data.name);
        debug_printf("mp: i:%s (%d)\n", item->data.name, item->data.len);
        strcpy(&path[pos], item->data.name);
        pos += item->data.len;
        item = item->next;
    }
    return pos;
}

/// @brief Trace sub directories from the file path and add file name
/// @param[in] tree directory tree
/// @param[in] file file information
/// @param[out] path : path such as "/foo/bar/baz/aaa.txt" (slash separator)
/// @param[in] size : buffer size of path
/// @return length of path
size_t msc_app_make_file_path_from_tree(const simple_list_t *tree, const simple_list_data_t *file, char *path, size_t size)
{
    size_t pos = msc_app_make_dir_path_from_tree(tree, path, size);
    if (pos + file->len < size) {
        strcpy(&path[pos], file->name);
        pos += file->len;
    }
    return pos;
}

/// @brief 
/// @param tree 
/// @param list 
/// @return 0:OK  1:OK but list is changed  -1:not found
int msc_app_change_directory_from_tree(simple_list_t *tree, simple_list_t *list)
{
    DIR dir;
    char path[256];
    msc_app_make_dir_path_from_tree(tree, path, sizeof(path));
    if (f_opendir(&dir, path) == FR_OK) {
        // exist directory
        f_closedir(&dir);
#ifdef USE_CURRENT_DIRECTORY
        // change directory
        f_chdir(path);
#endif
        return 0;
    }

    // root directory
    simple_list_clear(list);
    simple_list_clear(tree);
    bool rc = msc_app_make_list_in_directory("/", 0, list);
    if (rc) {
        return 1;
    }
    return -1;
}