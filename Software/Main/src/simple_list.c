/**
 * @file simple_list.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-18
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "simple_list.h"
#include <malloc.h>

// ----------------------------------------------------------------------

void simple_list_data_init(simple_list_data_t *data)
{
    data->index = 0;
    data->size = 0;
    data->attr = 0;
    data->len = 0;
    data->name = NULL;
}

static void simple_list_data_free(simple_list_data_t *data)
{
    if (data->name) {
        free(data->name);
    }
}

void simple_list_data_clear(simple_list_data_t *data)
{
    simple_list_data_free(data);
    simple_list_data_init(data);
}

/// @brief set data item
void simple_list_data_set_data(simple_list_data_t *data, int index, uint32_t size, uint32_t attr, const char *name, size_t len)
{
    data->index = index;
    data->size = size;
    data->attr = attr;
    if (name && len > 0) {
        data->len = len;
        data->name = (char *)malloc(sizeof(char) * (len + 1));
        strncpy(data->name, name, len);
    }
}

/// @brief set data item for referencing data
void simple_list_data_ref_data(simple_list_data_t *data, int index, uint32_t size, uint32_t attr, const char *name, size_t len)
{
    data->index = index;
    data->size = size;
    data->attr = attr;
    data->len = len;
    data->name = (char *)name;
}

void simple_list_data_copy_data(simple_list_data_t *dst, const simple_list_data_t *src)
{
    if (!src) return;
    simple_list_data_clear(dst);

    dst->size = src->size;
    dst->attr = src->attr;
    if (src->name) {
        dst->len = src->len;
        dst->name = (char *)malloc(sizeof(char) * (src->len + 1));
        strcpy(dst->name, src->name);
    }
}

// ----------------------------------------------------------------------

void simple_list_item_init(simple_list_item_t *item)
{
    item->next = NULL;
    simple_list_data_init(&item->data);
}

static void simple_list_item_free(simple_list_item_t *item)
{
    simple_list_data_free(&item->data);
    free(item);
}

static simple_list_item_t *simple_list_item_alloc(void)
{
    simple_list_item_t *item = (simple_list_item_t *)malloc(sizeof(simple_list_item_t));
    simple_list_item_init(item);
    return item;
}

static void simple_list_item_clear(simple_list_item_t *item)
{
    while (item) {
        simple_list_item_t *next = item->next;
        simple_list_item_free(item);
        item = next;
    }
}

// ----------------------------------------------------------------------

void simple_list_init(simple_list_t *list)
{
    list->count = 0;
    list->item_list = NULL;
}

void simple_list_clear(simple_list_t *list)
{
    simple_list_item_t *item = list->item_list;
    simple_list_item_clear(item);
    list->item_list = NULL;
    list->count = 0;
}

static simple_list_item_t *simple_list_get_last(simple_list_t *list)
{
    simple_list_item_t *item = list->item_list;
    while(item) {
        if (!item->next) {
            return item;
        }
        item=item->next;
    }
    return NULL;
}

bool simple_list_copy(simple_list_t *dst, const simple_list_t *src)
{
    simple_list_clear(dst);
    const simple_list_item_t *src_item = src->item_list;
    simple_list_item_t **dst_item = &dst->item_list;
    while(src_item) {
        simple_list_item_t *new_item = simple_list_item_alloc();
        if (!new_item) goto err;
        simple_list_data_copy_data(&new_item->data, &src_item->data);
        *dst_item = new_item;
        src_item = src_item->next;
        dst_item = &new_item->next;
    }
    dst->count = src->count;
    return true;
err:
    simple_list_clear(dst);
    return false;
}

bool simple_list_add_item(simple_list_t *list, const simple_list_data_t *data)
{
    simple_list_item_t *item = simple_list_get_last(list);
    if (!item) {
        item = simple_list_item_alloc();
        list->item_list = item;
    } else {
        item->next = simple_list_item_alloc();
        item = item->next;
    }
    if (!item) {
        // cannot allocate memory
        return false;
    }
    simple_list_data_copy_data(&item->data, data);
    item->data.index = list->count;
    list->count++;
    return true;
}

int simple_list_delete_item(simple_list_t *list, int index)
{
    int n = 0;
    int found = -1;
    simple_list_item_t **prev = &list->item_list;
    simple_list_item_t *item = list->item_list;
    while(item) {
        if (n == index) {
            *prev = item->next;
            simple_list_item_free(item);
            found = n;
            list->count--;
            break;
        }
        prev = &item->next;
        item = item->next;
        n++;
    }
    return found;
}

int simple_list_delete_last_item(simple_list_t *list)
{
    return simple_list_delete_item(list, list->count - 1);
}

int simple_list_delete_items_from(simple_list_t *list, simple_list_item_t *item)
{
    int n = 0;
    simple_list_item_t *list_item = list->item_list;
    while(list_item) {
        if (list_item == item) {
            simple_list_item_clear(list_item);
        } else {
            n++;
            list_item = list_item->next;
        }
    }
    list->count = n;
    return n;
}

simple_list_data_t *simple_list_get_data_by_index(simple_list_t *list, int index)
{
    int n = 0;
    simple_list_item_t *item = list->item_list;
    while(item) {
        if (n == index) {
            break;
        }
        item = item->next;
        n++;
    }
    return item ? &item->data : NULL;
}

simple_list_data_t *simple_list_get_last_data(simple_list_t *list)
{
    return simple_list_get_data_by_index(list, list->count - 1);
}

simple_list_data_t *simple_list_get_data_by_name(simple_list_t *list, const char *name)
{
    int n = 0;
    simple_list_item_t *item = list->item_list;
    while(item) {
        if (strcasecmp(item->data.name, name) == 0) {
            break;
        }
        item = item->next;
        n++;
    }
    return item ? &item->data : NULL;
}

simple_list_data_t *simple_list_get_data_by_subname(simple_list_t *list, const char *name, size_t len)
{
    int n = 0;
    simple_list_item_t *item = list->item_list;
    while(item) {
        if (strncasecmp(item->data.name, name, len) == 0) {
            break;
        }
        item = item->next;
        n++;
    }
    return item ? &item->data : NULL;
}

int simple_list_get_index_by_name(simple_list_t *list, const char *name)
{
    int n = 0;
    simple_list_item_t *item = list->item_list;
    while(item) {
        if (strcasecmp(item->data.name, name) == 0) {
            break;
        }
        item = item->next;
        n++;
    }
    return item ? n : -1;
}
