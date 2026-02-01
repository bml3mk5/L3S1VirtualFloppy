/** @file fdc_3inch.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-01-01
 * 
 * @copyright Copyright (c) Sasaji 2025
 */

#ifndef FDC_3INCH_H
#define FDC_3INCH_H

#include <stdint.h>
#include <stdbool.h>
#include "disk_drive.h"

// #define _DEBUG_MC6843

void fdc_3inch_init();
void fdc_3inch_reset();
void fdc_3inch_unitsel_reset();

void fdc_3inch_set_callback();

void fdc_3inch_write_io(uint32_t addr, uint8_t data);
uint8_t fdc_3inch_read_io(uint32_t addr);
void fdc_3inch_post_read(uint32_t addr, uint8_t data);
bool fdc_3inch_post_read_tightly(uint8_t addr);
void fdc_3inch_post_write_tightly(uint8_t addr);

void fdc_3inch_set_callback();

void fdc_3inch_get_info();

uint8_t fdc_3inch_toggle_side_number(uint8_t drv, uint8_t sid);
void fdc_3inch_set_side_number(uint8_t drv, uint8_t sid);
uint8_t fdc_3inch_get_side_number(uint8_t drv);

#endif /* FDC_3INCH_H */
