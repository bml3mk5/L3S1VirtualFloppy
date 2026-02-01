/** @file simple_fifo.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "simple_fifo.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pico/sync.h>

/// @brief 
/// @param f 
/// @param buffer : buffer pointer to allocated memory
/// @param size_count : need 2^n
/// @param unit_size : sizeof(uint8_t) or sizeof(uint16_t) 
void fifo_init(simple_fifo_t *f, void *buffer, uint16_t size_count, uint16_t unit_size)
{
    if (!buffer) {
        assert("fifo_init: need buffer pointer.\n");
        return;
    }
    f->buf = buffer;
    f->wpos = 0;
    f->rpos = 0;
    f->size = size_count;
    f->unit = unit_size;
}

void fifo_term(simple_fifo_t *f)
{
    f->size = 0;
    f->buf = NULL;
}

void fifo_clear(simple_fifo_t *f)
{
    f->wpos = 0;
    f->rpos = 0;
}

#if 0
void __not_in_flash_func(fifo_push8)(simple_fifo_t *f, uint8_t data)
{
    ((uint8_t *)f->buf)[f->wpos] = data;
    f->wpos = ((f->wpos + 1) & (f->size - 1));
}

void __not_in_flash_func(fifo_push16)(simple_fifo_t *f, uint16_t data)
{
    ((uint16_t *)f->buf)[f->wpos] = data;
    f->wpos = ((f->wpos + 1) & (f->size - 1));
}
#endif

void __not_in_flash_func(fifo_push32)(simple_fifo_t *f, uint32_t data)
{
    ((uint32_t *)f->buf)[f->wpos] = data;
    f->wpos = ((f->wpos + 1) & (f->size - 1));
}

void __not_in_flash_func(fifo_push_data)(simple_fifo_t *f, void *data)
{
    memcpy(&((uint8_t *)f->buf)[f->wpos * f->unit], data, f->unit);
    f->wpos = ((f->wpos + 1) & (f->size - 1));
}

#if 0
uint8_t __not_in_flash_func(fifo_pop8)(simple_fifo_t *f)
{
    uint8_t data = ((uint8_t *)f->buf)[f->rpos];
    f->rpos = ((f->rpos + 1) & (f->size - 1));
    return data;
}

uint16_t __not_in_flash_func(fifo_pop16)(simple_fifo_t *f)
{
    uint16_t data = ((uint16_t *)f->buf)[f->rpos];
    f->rpos = ((f->rpos + 1) & (f->size - 1));
    return data;
}
#endif

uint32_t __not_in_flash_func(fifo_pop32)(simple_fifo_t *f)
{
    uint32_t data = ((uint32_t *)f->buf)[f->rpos];
    f->rpos = ((f->rpos + 1) & (f->size - 1));
    return data;
}

void __not_in_flash_func(fifo_pop_data)(simple_fifo_t *f, void *data)
{
    if (!data) return;
    memcpy(data, (void *)&((uint8_t *)f->buf)[f->rpos * f->unit], f->unit);
    f->rpos = ((f->rpos + 1) & (f->size - 1));
}

uint8_t fifo_peek8(simple_fifo_t *f)
{
    return ((uint8_t *)f->buf)[f->rpos];
}

uint16_t fifo_peek16(simple_fifo_t *f)
{
    return ((uint16_t *)f->buf)[f->rpos];
}

uint32_t fifo_peek32(simple_fifo_t *f)
{
    return ((uint32_t *)f->buf)[f->rpos];
}

void *fifo_peek_data(simple_fifo_t *f)
{
    return (void *)&((uint8_t *)f->buf)[f->rpos * f->unit];
}

uint8_t fifo_latest8(simple_fifo_t *f)
{
    uint16_t curr = ((f->wpos + f->size - 1) & (f->size - 1));
    return ((uint8_t *)f->buf)[curr];
}

uint16_t fifo_latest16(simple_fifo_t *f)
{
    uint16_t curr = ((f->wpos + f->size - 1) & (f->size - 1));
    return ((uint16_t *)f->buf)[curr];
}

uint32_t fifo_latest32(simple_fifo_t *f)
{
    uint32_t curr = ((f->wpos + f->size - 1) & (f->size - 1));
    return ((uint32_t *)f->buf)[curr];
}

void *fifo_latest_data(simple_fifo_t *f)
{
    uint16_t curr = ((f->wpos + f->size - 1) & (f->size - 1));
    return (void *)&((uint8_t *)f->buf)[curr * f->unit];
}

#if 0
bool fifo_is_empty(simple_fifo_t *f)
{
    return (f->wpos == f->rpos);
}

bool fifo_is_not_empty(simple_fifo_t *f)
{
    return (f->wpos != f->rpos);
}

bool fifo_is_full(simple_fifo_t *f)
{
    return (((f->wpos + 1) & (f->size - 1)) == f->rpos);
}

bool fifo_is_not_full(simple_fifo_t *f)
{
    return (((f->wpos + 1) & (f->size - 1)) != f->rpos);
}
#endif

void fifo_inc_wpos(simple_fifo_t *f)
{
    f->wpos = ((f->wpos + 1) & (f->size - 1));
}

void fifo_inc_rpos(simple_fifo_t *f)
{
    f->rpos = ((f->rpos + 1) & (f->size - 1));
}

void fifo_add_wpos(simple_fifo_t *f, uint16_t cnt)
{
    f->wpos = ((f->wpos + cnt) & (f->size - 1));
}

void fifo_add_rpos(simple_fifo_t *f, uint16_t cnt)
{
    f->rpos = ((f->rpos + cnt) & (f->size - 1));
}

#if 0
int fifo_count(simple_fifo_t *f)
{
    return (int)((f->wpos + f->size - f->rpos) & (f->size - 1));
}
#endif

#if 0
int fifo_remain(simple_fifo_t *f)
{
    return (int)((f->rpos + f->size - f->wpos - 1) & (f->size - 1));
}
#endif
