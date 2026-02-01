/** @file simple_fifo.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */


#ifndef SIMPLE_FIFO_H
#define SIMPLE_FIFO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *buf;
    uint16_t wpos;
    uint16_t rpos;
    uint16_t size;
    uint16_t unit;  ///< element size
} simple_fifo_t;

void fifo_init(simple_fifo_t *f, void *buffer, uint16_t size_count, uint16_t unit_size);
void fifo_term(simple_fifo_t *f);
void fifo_clear(simple_fifo_t *f);
void fifo_push8(simple_fifo_t *f, uint8_t data);
void fifo_push16(simple_fifo_t *f, uint16_t data);
void fifo_push32(simple_fifo_t *f, uint32_t data);
void fifo_push_data(simple_fifo_t *f, void *data);
uint8_t fifo_pop8(simple_fifo_t *f);
uint16_t fifo_pop16(simple_fifo_t *f);
uint32_t fifo_pop32(simple_fifo_t *f);
void fifo_pop_data(simple_fifo_t *f, void *data);
uint8_t fifo_peek8(simple_fifo_t *f);
uint16_t fifo_peek16(simple_fifo_t *f);
uint32_t fifo_peek32(simple_fifo_t *f);
void *fifo_peek_data(simple_fifo_t *f);
uint8_t fifo_latest8(simple_fifo_t *f);
uint16_t fifo_latest16(simple_fifo_t *f);
uint32_t fifo_latest32(simple_fifo_t *f);
void *fifo_latest_data(simple_fifo_t *f);
bool fifo_is_empty(simple_fifo_t *f);
bool fifo_is_not_empty(simple_fifo_t *f);
bool fifo_is_full(simple_fifo_t *f);
bool fifo_is_not_full(simple_fifo_t *f);
void fifo_inc_wpos(simple_fifo_t *f);
void fifo_inc_rpos(simple_fifo_t *f);
void fifo_add_wpos(simple_fifo_t *f, uint16_t cnt);
void fifo_add_rpos(simple_fifo_t *f, uint16_t cnt);
int fifo_count(simple_fifo_t *f);
int fifo_remain(simple_fifo_t *f);


#endif /* SIMPLE_FIFO_H */
