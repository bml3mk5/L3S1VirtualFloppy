/** @file fdc_5inch.h
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
  20         | /RESET (Input)
  21         | /IRQ (Output)
  26         | BUSY (Output)(processing a command)
 */

#ifndef FDC_5INCH_H
#define FDC_5INCH_H

#include <stdint.h>
#include <stdbool.h>
#include "disk_drive.h"

void fdc_5inch_init();
void fdc_5inch_reset();
void fdc_5inch_unitsel_reset();

void fdc_5inch_write_io(uint32_t addr, uint8_t data);
//uint8_t fdc_5inch_read_io(uint32_t addr);
void fdc_5inch_post_read(uint32_t addr, uint8_t data);
//bool fdc_5inch_post_read_tightly(uint8_t addr);
//void fdc_5inch_post_write_tightly(uint8_t addr);

void fdc_5inch_set_callback();

void fdc_5inch_get_info();

uint8_t fdc_5inch_get_side_number();

#endif /* FDC_5INCH_H */
