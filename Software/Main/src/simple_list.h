/**
 * @file simple_list.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-18
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#ifndef SIMPLE_LIST_H
#define SIMPLE_LIST_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct st_simple_list_data {
    int     index;  // index
    uint32_t size;  // file size
    uint32_t attr;  // file attribute
    size_t    len;  // file name length
    char    *name;  // file name
} simple_list_data_t;

typedef struct st_simple_list_item {
    struct st_simple_list_data  data;
    struct st_simple_list_item *next;
} simple_list_item_t;

typedef struct st_simple_list {
    int count;
    struct st_simple_list_item *item_list;
} simple_list_t;

void simple_list_data_init(simple_list_data_t *data);
void simple_list_data_clear(simple_list_data_t *data);
void simple_list_data_set_data(simple_list_data_t *data, int index, uint32_t size, uint32_t attr, const char *name, size_t len);
void simple_list_data_ref_data(simple_list_data_t *data, int index, uint32_t size, uint32_t attr, const char *name, size_t len);
void simple_list_data_copy_data(simple_list_data_t *dst, const simple_list_data_t *src);

void simple_list_init(simple_list_t *list);
void simple_list_clear(simple_list_t *list);
bool simple_list_copy(simple_list_t *dst, const simple_list_t *src);
bool simple_list_add_item(simple_list_t *list, const simple_list_data_t *data);
int simple_list_delete_item(simple_list_t *list, int index);
int simple_list_delete_last_item(simple_list_t *list);
int simple_list_delete_items_from(simple_list_t *list, simple_list_item_t *item);
simple_list_data_t *simple_list_get_data_by_index(simple_list_t *list, int index);
simple_list_data_t *simple_list_get_last_data(simple_list_t *list);
simple_list_data_t *simple_list_get_data_by_name(simple_list_t *list, const char *name);
simple_list_data_t *simple_list_get_data_by_subname(simple_list_t *list, const char *name, size_t len);
int simple_list_get_index_by_name(simple_list_t *list, const char *name);

#endif /* SIMPLE_LIST_H */
