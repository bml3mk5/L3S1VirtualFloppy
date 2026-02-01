/** @file msc_app.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 * @copyright Copyright (c) 2019 Ha Thach (tinyusb.org)
 */
/**
  PIN Number | Desc.
 ------------|------------------------------------------
  27         | ENABLE (Output)(mounting an USB memory)
 */

#ifndef MSC_APP_H
#define MSC_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "simple_list.h"

#define MSC_APP_ENABLE_PIN      27

bool msc_app_init(void);
void msc_app_task(void);

typedef void (*msc_app_storage_mounted_t)(void);
typedef void (*msc_app_storage_unmounted_t)(void);

extern msc_app_storage_mounted_t msc_app_storage_mounted_cb;
extern msc_app_storage_unmounted_t msc_app_storage_unmounted_cb;

bool msc_app_make_list_in_directory(const char *path, uint8_t except_attr, simple_list_t *list);
int msc_app_change_directory(simple_list_data_t *newdir, simple_list_t *tree, simple_list_t *list);
int msc_app_trace_path(const char *file_path, simple_list_t *tree, simple_list_t *list);
int msc_app_change_directory_from_tree(simple_list_t *tree, simple_list_t *list);
size_t msc_app_make_dir_path_from_tree(const simple_list_t *tree, char *path, size_t size);
size_t msc_app_make_file_path_from_tree(const simple_list_t *tree, const simple_list_data_t *file, char *path, size_t size);

#endif /* MSC_APP_H */
