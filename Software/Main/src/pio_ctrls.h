/** @file pio_ctrls.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */
/**
  PIN Number | Desc.
 ------------|------------------------------------------
   2 -  3    | FD Type (Output)
 ------------|------------------------------------------
   6 - 13    | D0 - D7 (In/Out)
  14 - 17    | A0 - A3 (Input)
  18         | /WE (Input)
  19         | /RE (Input)
 ------------|------------------------------------------
  27         | BUZZER (unused)
 ------------|------------------------------------------
  28         | DEBUG (Output)
 */

#ifndef PIO_CTRLS_H
#define PIO_CTRLS_H

#include <stdint.h>
#include <stdbool.h>
#include <pico/types.h>

#define PIO_PARALLEL_REGS   16

#define PIO_PARALLEL_FDTYPE0_PIN 2
#define PIO_PARALLEL_FDTYPE_COUNT 2
#define PIO_PARALLEL_FDTYPE_MASK 0x000c

#define PIO_PARALLEL_DEBUG_PIN 28
//#define PIO_PARALLEL_DEBUG_USE_SIDESET 1

typedef struct st_pio_parallel_read {
    uint irq;
    uint irq_idx;
    uint8_t odata[PIO_PARALLEL_REGS];
} pio_parallel_read_t;

typedef struct st_pio_parallel_write {
    uint irq;
    uint irq_idx;
    uint8_t idata;
} pio_parallel_write_t;

extern pio_parallel_read_t g_pio_parallel_read;
extern pio_parallel_write_t g_pio_parallel_write;

void pio_ctrls_init();

void pio_ctrls_set_disk_type(uint8_t type);
void pio_ctrls_set_disk_type_shift(bool shift_on);
bool pio_ctrls_get_disk_type_shift();

void pio_parallel_set_data(uint8_t addr, uint8_t data);
void pio_parallel_notice(uint32_t data);

void pio_ctrls_debug_read_regs();

#ifdef USE_PIO_BUZZER
void pio_buzzer_out();
#endif

#endif /* PIO_CTRLS_H */
